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

static uint32_t g_dbg_high[8] = {0};
static uint32_t g_dbg_low[8] = {0};
static int g_dbg_err = 0;

IRAM_ATTR inline int capture_govee_frame(uint8_t *ledData, int maxLeds) {
    uint32_t tSearchStart = esp_cpu_get_cycle_count();
    bool inResetSilence = false;

    // Buscar silencio en bajo (>100us = 16000 ciclos)
    while (esp_cpu_get_cycle_count() - tSearchStart < 5600000) {
        if (!(*GOVEE_GPIO_IN & GOVEE_PIN_MASK)) {
            uint32_t tLowStart = esp_cpu_get_cycle_count();
            while (!(*GOVEE_GPIO_IN & GOVEE_PIN_MASK)) {
                if (esp_cpu_get_cycle_count() - tLowStart > 16000) {
                    inResetSilence = true;
                    break;
                }
            }
            if (inResetSilence) break;
        }
    }

    if (!inResetSilence) return -100;

    taskENTER_CRITICAL(&goveeMutex);

    // Esperar subida
    uint32_t tWaitRise = esp_cpu_get_cycle_count();
    while (!(*GOVEE_GPIO_IN & GOVEE_PIN_MASK)) {
        if (esp_cpu_get_cycle_count() - tWaitRise > 320000) {
            taskEXIT_CRITICAL(&goveeMutex);
            return -101;
        }
    }

    int ledsRead = 0;
    int bitCount = 0;
    g_dbg_err = 0;

    for (int i = 0; i < maxLeds; i++) {
        for (int c = 0; c < 3; c++) {
            uint8_t byteVal = 0;
            for (int bit = 7; bit >= 0; bit--) {
                uint32_t tStartHigh = esp_cpu_get_cycle_count();
                while (*GOVEE_GPIO_IN & GOVEE_PIN_MASK) {
                    if (esp_cpu_get_cycle_count() - tStartHigh > 50000) { // 312us
                        g_dbg_err = -103; // Stuck in HIGH
                        break;
                    }
                }
                uint32_t highCycles = esp_cpu_get_cycle_count() - tStartHigh;
                if (bitCount < 8) g_dbg_high[bitCount] = highCycles;

                if (g_dbg_err) break;

                // Threshold ~ 95 cycles (590ns)
                if (highCycles > 95) {
                    byteVal |= (1 << bit);
                }

                uint32_t tStartLow = esp_cpu_get_cycle_count();
                while (!(*GOVEE_GPIO_IN & GOVEE_PIN_MASK)) {
                    if (esp_cpu_get_cycle_count() - tStartLow > 50000) { // 312us
                        g_dbg_err = -104; // Stuck in LOW
                        break;
                    }
                }
                uint32_t lowCycles = esp_cpu_get_cycle_count() - tStartLow;
                if (bitCount < 8) g_dbg_low[bitCount] = lowCycles;

                bitCount++;
                if (g_dbg_err) break;
            }
            if (g_dbg_err) break;
            ledData[i * 3 + c] = byteVal;
        }
        if (g_dbg_err) break;
        ledsRead++;
    }

    taskEXIT_CRITICAL(&goveeMutex);
    return (ledsRead > 0) ? ledsRead : g_dbg_err;
}
