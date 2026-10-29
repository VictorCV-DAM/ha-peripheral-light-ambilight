#pragma once
#include "esp_attr.h"
#include "esp_cpu.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/portmacro.h"
#include <stdint.h>

#define GOVEE_PIN_MASK 0x08 // GPIO3
#define GOVEE_GPIO_IN ((volatile uint32_t *)0x6000403C)

static portMUX_TYPE goveeMutex = portMUX_INITIALIZER_UNLOCKED;

// Decodificador WS2811 de Ultra-Precision para Govee T1 (H6199) - 37 LEDs (912/888 bits)
IRAM_ATTR inline int capture_govee_frame(uint8_t *ledData, int maxLeds) {
    uint32_t tStart = esp_cpu_get_cycle_count();

    // 1. Detectar el silencio de RESET (linea mantenida en BAJO durante > 60us)
    // Damos hasta 60ms (9.600.000 ciclos a 160MHz) para capturar el ciclo completo
    bool resetDetected = false;
    while (esp_cpu_get_cycle_count() - tStart < 9600000) {
        // Si la linea esta en ALTO (en medio de una transmision), esperamos a que caiga a BAJO
        while (*GOVEE_GPIO_IN & GOVEE_PIN_MASK) {
            if (esp_cpu_get_cycle_count() - tStart > 9600000) return 0;
        }
        // Medir si permanece en BAJO de forma continua por mas de 60us (9600 ciclos)
        uint32_t tLow = esp_cpu_get_cycle_count();
        while (!(*GOVEE_GPIO_IN & GOVEE_PIN_MASK)) {
            if (esp_cpu_get_cycle_count() - tLow > 9600) { // >60us en bajo continuo = Reset WS2811
                resetDetected = true;
                break;
            }
        }
        if (resetDetected) break;
    }

    if (!resetDetected) return 0;

    // 2. Esperar el PRIMER FLANCO DE SUBIDA (Inicio de la nueva trama: Bit 7 de LED 0)
    // Se espera FUERA de seccion critica para no interferir con FreeRTOS ni WiFi
    uint32_t tWaitFirst = esp_cpu_get_cycle_count();
    uint32_t tRise = 0;
    while (1) {
        while (!(*GOVEE_GPIO_IN & GOVEE_PIN_MASK)) {
            if (esp_cpu_get_cycle_count() - tWaitFirst > 9600000) { // 60ms timeout
                return 0;
            }
        }
        tRise = esp_cpu_get_cycle_count();
        while (esp_cpu_get_cycle_count() - tRise < 20); // 125ns filtro glitch
        if (*GOVEE_GPIO_IN & GOVEE_PIN_MASK) {
            break; // Flanco de subida confirmado!
        }
    }

    // 3. AHORA QUE LA TRAMA HA COMENZADO: Entramos en seccion critica
    // Solo bloquea durante los 1.1ms que tarda la transmision de los 37 LEDs
    taskENTER_CRITICAL(&goveeMutex);

    // 4. Muestreo WS2811 por punto medio (530ns tras flanco)
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
                            if (esp_cpu_get_cycle_count() - tWaitRise > 4800) { // 30us timeout fin de trama
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
                    if (esp_cpu_get_cycle_count() - tWaitFall > 4800) { // 30us timeout
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

    taskEXIT_CRITICAL(&goveeMutex);
    return ledsRead;
}
