#pragma once
#include "esp_attr.h"
#include "esp_cpu.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/portmacro.h"
#include <stdint.h>

#define GOVEE_PIN_MASK 0x08 // GPIO3
#define GOVEE_GPIO_IN ((volatile uint32_t *)0x6000403C)

// Decodificador WS2811 de Ultra-Precision para Govee T1 (H6199) - 37 LEDs (888 bits)
// Espera 100% libre de bloqueos para WiFi + bloqueo de interrupciones en 1 ciclo RISC-V (6.25ns) en el flanco
IRAM_ATTR inline int capture_govee_frame(uint8_t *ledData, int maxLeds) {
    uint32_t tStart = esp_cpu_get_cycle_count();

    // 1. Detectar el silencio de reset WS2811 (linea en BAJO continuo durante > 60us = 9600 ciclos)
    bool resetDetected = false;
    while (esp_cpu_get_cycle_count() - tStart < 1600000) { // 10ms max
        while (*GOVEE_GPIO_IN & GOVEE_PIN_MASK) {
            if (esp_cpu_get_cycle_count() - tStart > 1600000) return 0;
        }
        uint32_t tLow = esp_cpu_get_cycle_count();
        while (!(*GOVEE_GPIO_IN & GOVEE_PIN_MASK)) {
            if (esp_cpu_get_cycle_count() - tLow > 9600) { // >60us en bajo continuo = Reset confirmado
                resetDetected = true;
                break;
            }
        }
        if (resetDetected) break;
    }

    if (!resetDetected) return 0;

    // 2. Esperar al flanco de subida del primer bit con interrupciones HABILITADAS (WiFi 100% libre).
    // Al detectar ALTO, deshabilitamos interrupciones en 1 solo ciclo de reloj RISC-V (csrrci mstatus, 8 = 6.25ns),
    // evitando las ~200+ ciclos de sobrecarga de taskENTER_CRITICAL que desplazaban el Bit 7 del LED 0.
    uint32_t tRise = 0;
    uint32_t mstatus = 0;
    uint32_t tWaitFirst = esp_cpu_get_cycle_count();
    while (1) {
        uint32_t tLastLow = esp_cpu_get_cycle_count();
        while (!(*GOVEE_GPIO_IN & GOVEE_PIN_MASK)) {
            tLastLow = esp_cpu_get_cycle_count();
            if (tLastLow - tWaitFirst > 1600000) { // 10ms timeout
                return 0;
            }
        }
        tRise = esp_cpu_get_cycle_count();
        // Deshabilitar interrupciones globalmente en la CPU RISC-V en 1 ciclo (6.25ns):
        __asm__ __volatile__("csrrci %0, mstatus, 8" : "=r"(mstatus) : : "memory");

        // Si una interrupcion de WiFi retraso la deteccion del flanco (> 200ns = 32 ciclos),
        // abortamos este intento para jamas leer una trama desplazada:
        if (tRise - tLastLow > 32) {
            __asm__ __volatile__("csrw mstatus, %0" : : "r"(mstatus) : "memory");
            return 0;
        }

        // Filtro antirruido de 125ns (20 ciclos) ya con interrupciones desactivadas:
        while (esp_cpu_get_cycle_count() - tRise < 20);
        if (*GOVEE_GPIO_IN & GOVEE_PIN_MASK) {
            break; // Pulso valido confirmado: Bit 7 de LED 0 en ciclo 20
        }

        // Era un micro-pico de ruido (< 125ns) en el cable sin masa: restaurar interrupciones y seguir esperando
        __asm__ __volatile__("csrw mstatus, %0" : : "r"(mstatus) : "memory");
    }

    // 3. Muestreo WS2811 por punto medio (530ns tras flanco) - identico al HITO da7c289
    int ledsRead = 0;
    bool frameDone = false;

    for (int i = 0; i < maxLeds; i++) {
        for (int c = 0; c < 3; c++) {
            uint8_t byteVal = 0;
            for (int bit = 7; bit >= 0; bit--) {
                if (!(i == 0 && c == 0 && bit == 7)) {
                    uint32_t tWaitRise = esp_cpu_get_cycle_count();
                    while (1) {
                        while (!(*GOVEE_GPIO_IN & GOVEE_PIN_MASK)) {
                            if (esp_cpu_get_cycle_count() - tWaitRise > 3200) { // 20us timeout
                                frameDone = true;
                                break;
                            }
                        }
                        if (frameDone) break;
                        tRise = esp_cpu_get_cycle_count();
                        while (esp_cpu_get_cycle_count() - tRise < 15); // 95ns glitch filter
                        if (*GOVEE_GPIO_IN & GOVEE_PIN_MASK) {
                            break; // Flanco real confirmado
                        }
                    }
                    if (frameDone) break;
                }

                // Punto medio del bit (85 ciclos = ~530ns)
                while (esp_cpu_get_cycle_count() - tRise < 85);

                // Muestrear valor
                if (*GOVEE_GPIO_IN & GOVEE_PIN_MASK) {
                    byteVal |= (1 << bit);
                }

                // Esperar caida a BAJO
                uint32_t tWaitFall = esp_cpu_get_cycle_count();
                while (*GOVEE_GPIO_IN & GOVEE_PIN_MASK) {
                    if (esp_cpu_get_cycle_count() - tWaitFall > 3200) { // 20us timeout
                        frameDone = true;
                        break;
                    }
                }
                if (frameDone) break;
            }
            if (frameDone) break;
            ledData[i * 3 + c] = byteVal;
        }
        if (frameDone) break;
        ledsRead++;
    }

    // Restaurar interrupciones tras el 1.1ms de lectura de los 37 LEDs:
    __asm__ __volatile__("csrw mstatus, %0" : : "r"(mstatus) : "memory");
    return ledsRead;
}
