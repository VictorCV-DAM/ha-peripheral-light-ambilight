#pragma once
#include "esp_attr.h"
#include "esp_cpu.h"
#include "freertos/FreeRTOS.h"
#include "freertos/portmacro.h"
#include <stdint.h>

#define GOVEE_PIN_MASK 0x08 // GPIO3
#define GOVEE_GPIO_IN ((volatile uint32_t *)0x6000403C)

static portMUX_TYPE goveeMutex = portMUX_INITIALIZER_UNLOCKED;

// Ejecuta 100% en SRAM interna (IRAM) - Cero fallos de caché, cero jitter
IRAM_ATTR inline int capture_govee_frame(uint8_t *ledData, int maxLeds) {
    // 1. Esperar silencio inter-trama de RESET (>30us en nivel bajo = 4800 ciclos a 160MHz)
    // Se ejecuta con interrupciones activas y timeout corto (20ms) para no bloquear FreeRTOS ni WiFi
    uint32_t tSearchStart = esp_cpu_get_cycle_count();
    bool inResetSilence = false;

    // 20ms a 160MHz = 3.200.000 ciclos
    while (esp_cpu_get_cycle_count() - tSearchStart < 3200000) {
        if (!(*GOVEE_GPIO_IN & GOVEE_PIN_MASK)) {
            uint32_t tLowStart = esp_cpu_get_cycle_count();
            while (!(*GOVEE_GPIO_IN & GOVEE_PIN_MASK)) {
                if (esp_cpu_get_cycle_count() - tLowStart > 4800) { // >30 µs
                    inResetSilence = true;
                    break;
                }
            }
            if (inResetSilence) break;
        }
    }

    if (!inResetSilence) return 0;

    // 2. Sección Crítica: ya estamos dentro del silencio de reset
    // Bloquea interrupciones ÚNICAMENTE durante los ~1 ms de captura de los LEDs
    taskENTER_CRITICAL(&goveeMutex);

    // Esperar al flanco de subida que marca el inicio del nuevo fotograma
    uint32_t tWaitRise = esp_cpu_get_cycle_count();
    while (!(*GOVEE_GPIO_IN & GOVEE_PIN_MASK)) {
        if (esp_cpu_get_cycle_count() - tWaitRise > 320000) { // máx 2ms
            taskEXIT_CRITICAL(&goveeMutex);
            return 0;
        }
    }

    // 3. Captura precisa bit a bit por ancho de pulso
    int ledsRead = 0;
    bool streamDone = false;

    for (int i = 0; i < maxLeds; i++) {
        for (int colorByte = 0; colorByte < 3; colorByte++) {
            uint8_t byteVal = 0;
            for (int bit = 7; bit >= 0; bit--) {
                // Medir duración del nivel ALTO
                uint32_t tStartHigh = esp_cpu_get_cycle_count();
                while (*GOVEE_GPIO_IN & GOVEE_PIN_MASK) {
                    if (esp_cpu_get_cycle_count() - tStartHigh > 2500) {
                        streamDone = true;
                        break;
                    }
                }
                if (streamDone) break;

                uint32_t highCycles = esp_cpu_get_cycle_count() - tStartHigh;

                // Bit 0 ~ 58 ciclos (360ns), Bit 1 ~ 135 ciclos (840ns) -> Umbral: 95 ciclos (590ns)
                if (highCycles > 95) {
                    byteVal |= (1 << bit);
                }

                // Esperar al flanco de subida del siguiente bit
                // Si el nivel bajo dura más de 20us (3200 ciclos), fin de la transmisión
                uint32_t tWaitNext = esp_cpu_get_cycle_count();
                while (!(*GOVEE_GPIO_IN & GOVEE_PIN_MASK)) {
                    if (esp_cpu_get_cycle_count() - tWaitNext > 3200) {
                        streamDone = true;
                        break;
                    }
                }
                if (streamDone) break;
            }
            if (streamDone) break;
            ledData[i * 3 + colorByte] = byteVal;
        }
        if (streamDone) break;
        ledsRead++;
    }

    taskEXIT_CRITICAL(&goveeMutex);
    return ledsRead;
}
