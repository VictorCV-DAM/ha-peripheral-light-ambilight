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

    // 1. Asegurar que la linea este activa
    while (!(*GOVEE_GPIO_IN & GOVEE_PIN_MASK)) {
        if (esp_cpu_get_cycle_count() - tStart > 3200000) return 0; // 20ms
    }

    // 2. Esperar al silencio de reset: la linea debe mantenerse en BAJO continuo
    // durante al menos 40.000 ciclos (250 us). Si hay cualquier pulso, se reinicia la cuenta.
    bool resetDetected = false;
    while (esp_cpu_get_cycle_count() - tStart < 8000000) { // 50ms max
        while (*GOVEE_GPIO_IN & GOVEE_PIN_MASK) {
            if (esp_cpu_get_cycle_count() - tStart > 8000000) return 0;
        }
        uint32_t tLow = esp_cpu_get_cycle_count();
        while (!(*GOVEE_GPIO_IN & GOVEE_PIN_MASK)) {
            if (esp_cpu_get_cycle_count() - tLow > 40000) { // 250us en bajo
                resetDetected = true;
                break;
            }
        }
        if (resetDetected) break;
    }

    if (!resetDetected) return 0;

    // 3. Entrar en seccion critica: desactiva interrupciones durante la transmision de ~1.1ms
    taskENTER_CRITICAL(&goveeMutex);

    // 4. Esperar al flanco de subida del primer bit con filtro antirruido
    uint32_t tRise = 0;
    uint32_t tWaitFirst = esp_cpu_get_cycle_count();
    while (1) {
        while (!(*GOVEE_GPIO_IN & GOVEE_PIN_MASK)) {
            if (esp_cpu_get_cycle_count() - tWaitFirst > 320000) { // 2ms timeout
                taskEXIT_CRITICAL(&goveeMutex);
                return 0;
            }
        }
        tRise = esp_cpu_get_cycle_count();
        while (esp_cpu_get_cycle_count() - tRise < 20); // 125ns glitch filter
        if (*GOVEE_GPIO_IN & GOVEE_PIN_MASK) {
            break; // Pulso valido: Bit 7 de LED 0
        }
    }

    // 5. Muestreo WS2811 por punto medio (530ns tras flanco)
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
