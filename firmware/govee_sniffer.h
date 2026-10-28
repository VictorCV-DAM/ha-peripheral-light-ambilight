#pragma once
#include "esp_attr.h"
#include "esp_cpu.h"
#include "freertos/FreeRTOS.h"
#include "freertos/portmacro.h"
#include <stdint.h>

#define GOVEE_PIN_MASK 0x08 // GPIO3
#define GOVEE_GPIO_IN ((volatile uint32_t *)0x6000403C)

static portMUX_TYPE goveeMutex = portMUX_INITIALIZER_UNLOCKED;

IRAM_ATTR inline int capture_govee_frame(uint8_t *ledData, int maxLeds) {
    uint32_t tSearchStart = esp_cpu_get_cycle_count();
    bool inReset = false;

    // 1. Esperar silencio inter-trama en bajo (>400us = 64000 ciclos a 160MHz)
    // El silencio de reset real de Govee es de ~1.2ms (192.000 ciclos).
    // Las pausas entre esquinas son de maximo 52us (8320 ciclos).
    // Con 400us garantizamos al 100% que SOLO enganchamos el inicio real de trama.
    while (esp_cpu_get_cycle_count() - tSearchStart < 6400000) { // max 40ms
        if (!(*GOVEE_GPIO_IN & GOVEE_PIN_MASK)) {
            uint32_t tLow = esp_cpu_get_cycle_count();
            while (!(*GOVEE_GPIO_IN & GOVEE_PIN_MASK)) {
                if (esp_cpu_get_cycle_count() - tLow > 64000) { // >400us
                    inReset = true;
                    break;
                }
            }
            if (inReset) break;
        }
    }

    if (!inReset) return 0;

    taskENTER_CRITICAL(&goveeMutex);

    // 2. Esperar al fin del silencio de reset (flanco de subida)
    uint32_t tWaitRise = esp_cpu_get_cycle_count();
    while (!(*GOVEE_GPIO_IN & GOVEE_PIN_MASK)) {
        if (esp_cpu_get_cycle_count() - tWaitRise > 320000) { // max 2ms
            taskEXIT_CRITICAL(&goveeMutex);
            return 0;
        }
    }

    // 3. Descartar el pulso de preámbulo de Govee
    // Tras el reset de 1.2ms, Govee siempre emite un pulso ALTO de ~28us (4500 ciclos),
    // seguido de un bajo de ~10us antes de que comiencen los bits de datos.
    uint32_t tStartFirstPulse = esp_cpu_get_cycle_count();
    while (*GOVEE_GPIO_IN & GOVEE_PIN_MASK) {
        if (esp_cpu_get_cycle_count() - tStartFirstPulse > 32000) break; // max 200us
    }
    uint32_t firstPulseHigh = esp_cpu_get_cycle_count() - tStartFirstPulse;

    if (firstPulseHigh > 800) { // >5us: Es el preámbulo
        // Esperar a que caiga a bajo y luego al flanco de subida del primer bit de datos
        uint32_t tWaitData = esp_cpu_get_cycle_count();
        while (!(*GOVEE_GPIO_IN & GOVEE_PIN_MASK)) {
            if (esp_cpu_get_cycle_count() - tWaitData > 32000) { // max 200us
                taskEXIT_CRITICAL(&goveeMutex);
                return 0;
            }
        }
    } else {
        // Si no fue preámbulo (>5us), esperar flanco de subida
        uint32_t tWaitRise2 = esp_cpu_get_cycle_count();
        while (!(*GOVEE_GPIO_IN & GOVEE_PIN_MASK)) {
            if (esp_cpu_get_cycle_count() - tWaitRise2 > 32000) {
                taskEXIT_CRITICAL(&goveeMutex);
                return 0;
            }
        }
    }

    // 4. Lectura precisa bit a bit por ancho de pulso WS2811
    int ledsRead = 0;
    bool streamDone = false;

    for (int i = 0; i < maxLeds; i++) {
        for (int c = 0; c < 3; c++) {
            uint8_t byteVal = 0;
            for (int bit = 7; bit >= 0; bit--) {
                uint32_t tStartHigh = esp_cpu_get_cycle_count();
                while (*GOVEE_GPIO_IN & GOVEE_PIN_MASK) {
                    if (esp_cpu_get_cycle_count() - tStartHigh > 50000) { // >312us en alto
                        streamDone = true;
                        break;
                    }
                }
                if (streamDone) break;

                uint32_t highCycles = esp_cpu_get_cycle_count() - tStartHigh;

                // Bit 0 ~ 58 ciclos (360ns), Bit 1 ~ 135 ciclos (840ns). Umbral: 95 ciclos (590ns)
                if (highCycles > 95) {
                    byteVal |= (1 << bit);
                }

                if (i == maxLeds - 1 && c == 2 && bit == 0) break;

                // Esperar a que el pin vuelva a subir para el siguiente bit
                // Cruza pausas de esquinas (hasta 52us) con timeout de 312us (50000 ciclos)
                uint32_t tWaitNext = esp_cpu_get_cycle_count();
                while (!(*GOVEE_GPIO_IN & GOVEE_PIN_MASK)) {
                    if (esp_cpu_get_cycle_count() - tWaitNext > 50000) { // >312us en bajo
                        streamDone = true;
                        break;
                    }
                }
                if (streamDone) break;
            }
            if (streamDone) break;
            ledData[i * 3 + c] = byteVal;
        }
        if (streamDone) break;
        ledsRead++;
    }

    taskEXIT_CRITICAL(&goveeMutex);
    return ledsRead;
}
