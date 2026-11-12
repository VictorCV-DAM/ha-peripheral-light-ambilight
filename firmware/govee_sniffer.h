#pragma once
#include "esp_attr.h"
#include "esp_cpu.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/portmacro.h"
#include <stdint.h>
#include <string.h>

#define GOVEE_PIN_MASK 0x08 // GPIO3
#define GOVEE_GPIO_IN ((volatile uint32_t *)0x6000403C)

static portMUX_TYPE goveeMutex = portMUX_INITIALIZER_UNLOCKED;

#define SNIFFER_MAX_LEDS 37
static uint8_t s_sharedLedData[SNIFFER_MAX_LEDS * 3] = {0};
static volatile int s_sharedLedsCount = 0;
static volatile uint32_t s_lastFrameTimestamp = 0;
static volatile bool s_newFrameAvailable = false;
static TaskHandle_t s_snifferTaskHandle = NULL;

// Decodificador WS2811 de Ultra-Precision para Govee T1 (H6199) - 37 LEDs
IRAM_ATTR inline int capture_govee_raw(uint8_t *ledData, int maxLeds, uint32_t maxWaitCycles) {
    uint32_t tStart = esp_cpu_get_cycle_count();

    // 1. Detectar silencio de reset (linea en BAJO continuo durante > 60us = 9600 ciclos)
    bool resetDetected = false;
    while (esp_cpu_get_cycle_count() - tStart < maxWaitCycles) {
        while (*GOVEE_GPIO_IN & GOVEE_PIN_MASK) {
            if (esp_cpu_get_cycle_count() - tStart > maxWaitCycles) return 0;
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

    // 2. Esperar al flanco de subida del primer bit (Bit 7 de LED 0)
    uint32_t tRise = 0;
    uint32_t tWaitFirst = esp_cpu_get_cycle_count();
    while (1) {
        while (!(*GOVEE_GPIO_IN & GOVEE_PIN_MASK)) {
            if (esp_cpu_get_cycle_count() - tWaitFirst > maxWaitCycles) {
                return 0;
            }
        }
        tRise = esp_cpu_get_cycle_count();
        while (esp_cpu_get_cycle_count() - tRise < 20); // 125ns glitch filter
        if (*GOVEE_GPIO_IN & GOVEE_PIN_MASK) {
            break; // Flanco confirmado
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
                            if (esp_cpu_get_cycle_count() - tWaitRise > 3200) { // 20us timeout
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

                // Punto medio del bit (85 ciclos = ~530ns)
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

// Tarea en segundo plano FreeRTOS (Phase-Locked Loop)
static void govee_sniffer_task(void *pvParameters) {
    uint8_t tempBuffer[SNIFFER_MAX_LEDS * 3];
    bool inSync = false;
    while (true) {
        // Si ya estamos sincronizados, la trama vendrá en 10-18ms (esperamos max 25ms = 4.000.000 ciclos).
        // Si no estamos sincronizados (arranque o pérdida), damos 50ms (8.000.000 ciclos) para engancharla.
        uint32_t waitCycles = inSync ? 4000000 : 8000000;

        int count = capture_govee_raw(tempBuffer, SNIFFER_MAX_LEDS, waitCycles);
        if (count >= SNIFFER_MAX_LEDS) {
            taskENTER_CRITICAL(&goveeMutex);
            memcpy(s_sharedLedData, tempBuffer, SNIFFER_MAX_LEDS * 3);
            s_sharedLedsCount = count;
            s_newFrameAvailable = true;
            taskEXIT_CRITICAL(&goveeMutex);

            s_lastFrameTimestamp = esp_log_timestamp();
            inSync = true;

            // La trama acaba de terminar. La siguiente vendrá en ~32-35ms.
            // Dormimos 18ms cediendo el 100% de CPU a WiFi y ESPHome antes de preparar la siguiente captura.
            vTaskDelay(pdMS_TO_TICKS(18));
        } else {
            inSync = false;
            // Sin señal o fuera de fase. Dormimos 10ms antes del siguiente enganche
            vTaskDelay(pdMS_TO_TICKS(10));
        }
    }
}

inline void init_govee_sniffer() {
    if (s_snifferTaskHandle == NULL) {
        xTaskCreatePinnedToCore(
            govee_sniffer_task,
            "gov_sniffer",
            4096,
            NULL,
            1, // Prioridad 1
            &s_snifferTaskHandle,
            0  // Core 0 (ESP32-C3)
        );
    }
}

// Función no bloqueante para el intervalo de ESPHome (se ejecuta en nanosegundos)
inline int get_latest_govee_frame(uint8_t *dest, int maxLeds, bool *isNew) {
    taskENTER_CRITICAL(&goveeMutex);
    int count = s_sharedLedsCount;
    *isNew = s_newFrameAvailable;
    s_newFrameAvailable = false;
    if (count > 0) {
        int copyLeds = (count < maxLeds) ? count : maxLeds;
        memcpy(dest, s_sharedLedData, copyLeds * 3);
    }
    taskEXIT_CRITICAL(&goveeMutex);
    return count;
}

inline int capture_govee_frame(uint8_t *ledData, int maxLeds) {
    bool isNew = false;
    return get_latest_govee_frame(ledData, maxLeds, &isNew);
}
