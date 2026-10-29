#pragma once
#include "esp_attr.h"
#include "esp_cpu.h"
#include <stdint.h>

#define GOVEE_PIN_MASK 0x08 // GPIO3
#define GOVEE_GPIO_IN ((volatile uint32_t *)0x6000403C)

// Exact 6aa3e61 mid-point sniffer with reset threshold adjusted to 80,000 cycles (500us)
IRAM_ATTR inline int capture_govee_frame(uint8_t *ledData, int maxLeds) {
    uint32_t tSearchStart = esp_cpu_get_cycle_count();
    bool inReset = false;

    // Buscar reset durante máx 60ms (9.6 millones de ciclos)
    while (esp_cpu_get_cycle_count() - tSearchStart < 9600000) {
        if (!(*GOVEE_GPIO_IN & GOVEE_PIN_MASK)) {
            uint32_t tLowStart = esp_cpu_get_cycle_count();
            while (!(*GOVEE_GPIO_IN & GOVEE_PIN_MASK)) {
                if (esp_cpu_get_cycle_count() - tLowStart > 80000) { // >500us en bajo
                    inReset = true;
                    break;
                }
            }
            if (inReset) break;
        }
    }

    if (!inReset) return 0;

    int ledsRead = 0;
    bool frameDone = false;

    // 2. Muestreo de bits por punto medio (mid-point sampling a 90 ciclos)
    for (int i = 0; i < maxLeds; i++) {
        for (int c = 0; c < 3; c++) {
            uint8_t byteVal = 0;
            for (int bit = 7; bit >= 0; bit--) {
                // A. Esperar flanco de subida (0 -> 1)
                uint32_t tWaitRise = esp_cpu_get_cycle_count();
                while (!(*GOVEE_GPIO_IN & GOVEE_PIN_MASK)) {
                    if (esp_cpu_get_cycle_count() - tWaitRise > 200000) {
                        frameDone = true;
                        break;
                    }
                }
                if (frameDone) break;

                uint32_t tRise = esp_cpu_get_cycle_count();

                // B. Esperar exactamente al punto medio del bit (90 ciclos = 560ns)
                while (esp_cpu_get_cycle_count() - tRise < 90);

                // C. Muestrear el pin en el punto medio
                if (*GOVEE_GPIO_IN & GOVEE_PIN_MASK) {
                    byteVal |= (1 << bit);
                }

                // D. Esperar a que el pin vuelva a nivel BAJO
                uint32_t tWaitFall = esp_cpu_get_cycle_count();
                while (*GOVEE_GPIO_IN & GOVEE_PIN_MASK) {
                    if (esp_cpu_get_cycle_count() - tWaitFall > 2500) {
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

    return ledsRead;
}
