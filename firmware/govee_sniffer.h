#pragma once
#include "esp_attr.h"
#include "esp_cpu.h"
#include "esp_log.h"
#include "driver/gpio.h"
#include "soc/gpio_struct.h"
#include <stdint.h>
#include <string.h>

#define GOVEE_PIN_MASK 0x08 // GPIO3
#define GOVEE_GPIO_IN ((volatile uint32_t *)0x6000403C)

#define GOVEE_MAX_LEDS 37
static uint8_t s_capturedLeds[GOVEE_MAX_LEDS * 3] = {0};
static volatile int s_capturedCount = 0;
static volatile bool s_newFrameReady = false;
static bool s_isrInstalled = false;

static void IRAM_ATTR govee_ws2811_isr(void *arg) {
    uint32_t tRise = esp_cpu_get_cycle_count();

    // Filtro glitch: verificar que no sea un pico de ruido (< 125ns = 20 ciclos a 160MHz)
    while (esp_cpu_get_cycle_count() - tRise < 20);
    if (!(*GOVEE_GPIO_IN & GOVEE_PIN_MASK)) {
        GPIO.status_w1tc.val = GOVEE_PIN_MASK;
        return; // Ruido espurio
    }

    // Desactivar temporalmente la interrupción para evitar disparos repetidos durante los 888 bits
    gpio_intr_disable(GPIO_NUM_3);

    uint8_t temp[GOVEE_MAX_LEDS * 3];
    int ledsRead = 0;
    bool frameDone = false;

    for (int i = 0; i < GOVEE_MAX_LEDS; i++) {
        for (int c = 0; c < 3; c++) {
            uint8_t byteVal = 0;
            for (int bit = 7; bit >= 0; bit--) {
                if (!(i == 0 && c == 0 && bit == 7)) {
                    // Esperar subida del bit
                    uint32_t tWaitRise = esp_cpu_get_cycle_count();
                    while (!(*GOVEE_GPIO_IN & GOVEE_PIN_MASK)) {
                        if (esp_cpu_get_cycle_count() - tWaitRise > 3200) { // 20us timeout fin trama
                            frameDone = true;
                            break;
                        }
                    }
                    if (frameDone) break;

                    tRise = esp_cpu_get_cycle_count();
                    while (esp_cpu_get_cycle_count() - tRise < 15);
                    if (!(*GOVEE_GPIO_IN & GOVEE_PIN_MASK)) continue;
                }

                // Punto medio del bit (85 ciclos = ~530ns desde el flanco)
                while (esp_cpu_get_cycle_count() - tRise < 85);

                if (*GOVEE_GPIO_IN & GOVEE_PIN_MASK) {
                    byteVal |= (1 << bit);
                }

                // Esperar caída a BAJO
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
            temp[i * 3 + c] = byteVal;
        }
        if (frameDone) break;
        ledsRead++;
    }

    // Esperar silencio de reset final (línea en bajo continuo > 50us = 8000 ciclos), con timeout máximo de seguridad de 200us
    uint32_t tReset = esp_cpu_get_cycle_count();
    uint32_t tMaxReset = tReset;
    while (esp_cpu_get_cycle_count() - tReset < 8000) {
        if (*GOVEE_GPIO_IN & GOVEE_PIN_MASK) tReset = esp_cpu_get_cycle_count();
        if (esp_cpu_get_cycle_count() - tMaxReset > 32000) break; // 200us max
    }

    // Limpiar bandera de interrupción y re-activar para la siguiente trama (~32ms después)
    GPIO.status_w1tc.val = GOVEE_PIN_MASK;
    gpio_intr_enable(GPIO_NUM_3);

    if (ledsRead >= 37) {
        memcpy(s_capturedLeds, temp, 37 * 3);
        s_capturedCount = ledsRead;
        s_newFrameReady = true;
    }
}

inline void init_govee_sniffer() {
    if (!s_isrInstalled) {
        gpio_install_isr_service(0);
        gpio_set_intr_type(GPIO_NUM_3, GPIO_INTR_POSEDGE);
        gpio_isr_handler_add(GPIO_NUM_3, govee_ws2811_isr, NULL);
        s_isrInstalled = true;
    }
}

// Función no bloqueante para el bucle de ESPHome: 50 nanosegundos (cero bloqueo)
inline int capture_govee_frame(uint8_t *ledData, int maxLeds) {
    init_govee_sniffer();

    if (s_newFrameReady && s_capturedCount >= 37) {
        s_newFrameReady = false;
        int toCopy = (s_capturedCount < maxLeds) ? s_capturedCount : maxLeds;
        memcpy(ledData, s_capturedLeds, toCopy * 3);
        return toCopy;
    }
    return 0;
}
