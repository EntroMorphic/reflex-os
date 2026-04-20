/**
 * @file reflex_hal_esp32.c
 * @brief Reflex HAL — ESP32 (original, Xtensa) backend.
 *
 * Uses ESP-IDF APIs where direct register access differs from C6.
 * This is a mesh-peer platform, not the primary target.
 */

#include "reflex_hal.h"

#include <stdarg.h>
#include <stdio.h>
#include "esp_timer.h"
#include "esp_rom_sys.h"
#include "esp_log.h"
#include "esp_sleep.h"
#include "esp_random.h"
#include "esp_mac.h"
#include "driver/gpio.h"
#include "esp_system.h"

uint64_t reflex_hal_time_us(void) {
    return (uint64_t)esp_timer_get_time();
}

uint32_t reflex_hal_cpu_cycles(void) {
    uint32_t cycles;
    __asm__ volatile ("rsr %0, ccount" : "=r"(cycles));
    return cycles;
}

void reflex_hal_delay_us(uint32_t us) {
    esp_rom_delay_us(us);
}

reflex_err_t reflex_hal_gpio_init_output(uint32_t pin) {
    gpio_config_t cfg = {
        .pin_bit_mask = (1ULL << pin),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    return (reflex_err_t)gpio_config(&cfg);
}

reflex_err_t reflex_hal_gpio_init_input(uint32_t pin, bool pullup) {
    gpio_config_t cfg = {
        .pin_bit_mask = (1ULL << pin),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = pullup ? GPIO_PULLUP_ENABLE : GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    return (reflex_err_t)gpio_config(&cfg);
}

reflex_err_t reflex_hal_gpio_set_level(uint32_t pin, int level) {
    return (reflex_err_t)gpio_set_level((gpio_num_t)pin, level);
}

int reflex_hal_gpio_get_level(uint32_t pin) {
    return gpio_get_level((gpio_num_t)pin);
}

reflex_err_t reflex_hal_gpio_connect_out(uint32_t out_pin, uint32_t signal,
                                         bool invert, bool enable) {
    (void)out_pin; (void)signal; (void)invert; (void)enable;
    return REFLEX_ERR_NOT_SUPPORTED;
}

void reflex_hal_reboot(void) {
    esp_restart();
}

int reflex_hal_sleep_wakeup_cause(void) {
    return (int)esp_sleep_get_wakeup_cause();
}

void reflex_hal_sleep_enter(uint64_t duration_us) {
    esp_sleep_enable_timer_wakeup(duration_us);
    esp_deep_sleep_start();
}

void reflex_hal_random_fill(uint8_t *buf, size_t len) {
    esp_fill_random(buf, len);
}

reflex_err_t reflex_hal_mac_read(uint8_t mac[6]) {
    return (reflex_err_t)esp_read_mac(mac, ESP_MAC_WIFI_STA);
}

reflex_err_t reflex_hal_temp_init(reflex_temp_handle_t *out) {
    (void)out;
    return REFLEX_ERR_NOT_SUPPORTED;
}

reflex_err_t reflex_hal_temp_read(reflex_temp_handle_t h, float *celsius) {
    (void)h; (void)celsius;
    return REFLEX_ERR_NOT_SUPPORTED;
}

reflex_err_t reflex_hal_intr_alloc(int source, int flags,
                                   reflex_intr_handler_t handler, void *arg,
                                   reflex_intr_handle_t *out_handle) {
    extern int esp_intr_alloc(int, int, void (*)(void*), void*, void**);
    return (reflex_err_t)esp_intr_alloc(source, flags & REFLEX_INTR_FLAG_IRAM ? 1 : 0,
                                        handler, arg, (void**)out_handle);
}

reflex_err_t reflex_hal_intr_free(reflex_intr_handle_t handle) {
    extern int esp_intr_free(void*);
    return (reflex_err_t)esp_intr_free(handle);
}

void reflex_hal_log(int level, const char *tag, const char *fmt, ...) {
    esp_log_level_t esp_level;
    switch (level) {
        case REFLEX_LOG_LEVEL_ERROR: esp_level = ESP_LOG_ERROR; break;
        case REFLEX_LOG_LEVEL_WARN:  esp_level = ESP_LOG_WARN;  break;
        case REFLEX_LOG_LEVEL_INFO:  esp_level = ESP_LOG_INFO;  break;
        case REFLEX_LOG_LEVEL_DEBUG: esp_level = ESP_LOG_DEBUG;  break;
        default:                     esp_level = ESP_LOG_INFO;   break;
    }
    va_list args;
    va_start(args, fmt);
    esp_log_writev(esp_level, tag, fmt, args);
    va_end(args);
}
