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

// Decodificador WS2811 síncrono y seguro para ESPHome (bloqueo máximo estricto < 15ms)
IRAM_ATTR inline int capture_govee_frame(uint8_t *ledData, int maxLeds) {
    uint32_t tStart = esp_cpu_get_cycle_count();

    // 1. Detectar silencio de reset (linea en BAJO continuo durante > 60us = 9600 ciclos).
    // Timeout máximo de 2.0ms (320.000 ciclos): suficiente para que termine cualquier trama en curso.
    bool resetDetected = false;
    while (esp_cpu_get_cycle_count() - tStart < 320000) {
        while (*GOVEE_GPIO_IN & GOVEE_PIN_MASK) {
            if (esp_cpu_get_cycle_count() - tStart > 320000) return 0;
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

    // 2. Esperar al flanco de subida del primer bit (Bit 7 de LED 0).
    // Timeout máximo de 12.0ms (1.920.000 ciclos): garantiza que el tiempo total de la función sea < 15ms.
    uint32_t tRise = 0;
    uint32_t tWaitFirst = esp_cpu_get_cycle_count();
    while (1) {
        while (!(*GOVEE_GPIO_IN & GOVEE_PIN_MASK)) {
            if (esp_cpu_get_cycle_count() - tWaitFirst > 1920000) {
                return 0; // Sin señal o intervalo fuera de fase
            }
        }
        tRise = esp_cpu_get_cycle_count();
        while (esp_cpu_get_cycle_count() - tRise < 20); // 125ns glitch filter
        if (*GOVEE_GPIO_IN & GOVEE_PIN_MASK) {
            break; // Flanco real confirmado
        }
    }

    // 3. SECCION CRITICA: Solo durante la lectura de los 37 LEDs (~1.1ms)
    taskENTER_CRITICAL(&goveeMutex);

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
                            if (esp_cpu_get_cycle_count() - tWaitRise > 3200) { // 20us timeout fin trama
                                frameDone = true;
                                break;
                            }
                        }
                        if (frameDone) break;
                        tRise = esp_cpu_get_cycle_count();
                        while (esp_cpu_get_cycle_count() - tRise < 15); // 95ns glitch filter
                        if (*GOVEE_GPIO_IN & GOVEE_PIN_MASK) {
                            break;
                        }
                    }
                    if (frameDone) break;
                }

                // Punto medio del bit (85 ciclos = ~530ns a 160MHz)
                while (esp_cpu_get_cycle_count() - tRise < 85);

                if (*GOVEE_GPIO_IN & GOVEE_PIN_MASK) {
                    byteVal |= (1 << bit);
                }

                // Esperar caida a BAJO
                uint32_t tWaitFall = esp_cpu_get_cycle_count();
                while (*GOVEE_GPIO_IN & GOVEE_PIN_MASK) {
                    if (esp_cpu_get_cycle_count() - tWaitFall > 3200) {
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
