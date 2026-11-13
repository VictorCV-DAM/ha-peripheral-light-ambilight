#pragma once
#include "esp_attr.h"
#include "esp_cpu.h"
#include "esp_log.h"
#include "esp_rom_sys.h"
#include "freertos/FreeRTOS.h"
#include "freertos/portmacro.h"
#include <stdint.h>

#define GOVEE_PIN_MASK 0x08 // GPIO3
#define GOVEE_GPIO_IN ((volatile uint32_t *)0x6000403C)

static portMUX_TYPE goveeMutex = portMUX_INITIALIZER_UNLOCKED;

// Decodificador WS2811 de Ultra-Precision para Govee T1 (H6199) - 37 LEDs (912/888 bits)
// Basado fielmente en el HITO da7c289 con ventana de respiracion WiFi verificada por silencio de 3us
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

    // 2. Entrar en seccion critica ANTES del flanco (exactamente como en el HITO da7c289)
    taskENTER_CRITICAL(&goveeMutex);

    // 3. Esperar al flanco de subida del primer bit con filtro antirruido
    uint32_t tRise = 0;
    uint32_t tWaitFirst = esp_cpu_get_cycle_count();
    uint32_t tSlice = tWaitFirst;
    while (1) {
        while (!(*GOVEE_GPIO_IN & GOVEE_PIN_MASK)) {
            uint32_t cNow = esp_cpu_get_cycle_count();
            if (cNow - tWaitFirst > 1600000) { // 10ms timeout total
                taskEXIT_CRITICAL(&goveeMutex);
                return 0;
            }
            if (cNow - tSlice > 240000) { // Cada 1.5ms en BAJO: abrir ventana de 15us para WiFi
                taskEXIT_CRITICAL(&goveeMutex);
                esp_rom_delay_us(15);
                taskENTER_CRITICAL(&goveeMutex);
                // Verificar 3us (480 ciclos) continuos en BAJO tras reentrar.
                // Como WS2811 emite un pulso ALTO cada 1.25us (200 ciclos), 3us continuos en BAJO
                // garantiza al 100% que NO ha empezado ninguna trama durante la ventana de 15us.
                uint32_t tVerifyLow = esp_cpu_get_cycle_count();
                while (esp_cpu_get_cycle_count() - tVerifyLow < 480) {
                    if (*GOVEE_GPIO_IN & GOVEE_PIN_MASK) {
                        taskEXIT_CRITICAL(&goveeMutex);
                        return 0;
                    }
                }
                tSlice = esp_cpu_get_cycle_count();
            }
        }
        tRise = esp_cpu_get_cycle_count();
        while (esp_cpu_get_cycle_count() - tRise < 20); // 125ns glitch filter
        if (*GOVEE_GPIO_IN & GOVEE_PIN_MASK) {
            break; // Pulso valido: Bit 7 de LED 0
        }
    }

    // 5. Muestreo WS2811 por punto medio (530ns tras flanco) - 100% identico al HITO da7c289
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

    taskEXIT_CRITICAL(&goveeMutex);
    return ledsRead;
}
