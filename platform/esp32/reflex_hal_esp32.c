/**
 * @file reflex_hal_esp32.c
 * @brief Reflex HAL — ESP32 (original, Xtensa) backend.
 *
 * Uses ESP-IDF APIs where direct register access differs from C6.
 * This is a mesh-peer platform, not the primary target.
 */

#include "reflex_hal.h"
#include <string.h>
#include "esp_intr_alloc.h"
#include <stdio.h>

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

/* No Boot0 on this target: the ESP32 uses ESP-IDF's stock second-stage
 * bootloader, which keeps no such counter. */
void reflex_hal_boot_mark_stable(void) {}

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

/* The classic ESP32 borrows ESP-IDF's interrupt allocator wholesale, so there
 * is no Reflex-owned routing to report. Zeroed rather than guessed. */
void reflex_hal_intr_describe(int source, reflex_intr_route_t *out) {
    (void)source;
    if (out) memset(out, 0, sizeof(*out));
}

/* The classic ESP32 has no USB-serial-JTAG; its console is a UART behind
 * ESP-IDF's driver, which shell.c still uses on that target. Declared here so
 * the interface is whole, and honest about not implementing it. */
reflex_err_t reflex_hal_intr_set_enabled(reflex_intr_handle_t handle, bool enabled) {
    (void)handle;
    (void)enabled;
    return REFLEX_ERR_NOT_SUPPORTED; /* ESP-IDF owns interrupt allocation here */
}

reflex_err_t reflex_hal_console_init(void) {
    return REFLEX_ERR_NOT_SUPPORTED;
}
bool reflex_hal_console_read(uint8_t *out) {
    (void)out;
    return false;
}
uint32_t reflex_hal_console_dropped(void) {
    return 0;
}

/* No Reflex-owned receive path on this target, so there is nothing to report.
 * Zeroed rather than left undefined so a caller written for the C6 compiles
 * and reads a consistent "nothing happened" here. */
/* ESP-IDF still owns LEDC on this target, so these report "not mine" rather
 * than half-implementing a peripheral nobody here drives. */
reflex_err_t reflex_hal_pwm_init(uint32_t freq_hz, uint8_t duty_res_bits, uint32_t duty) {
    (void)freq_hz;
    (void)duty_res_bits;
    (void)duty;
    return REFLEX_ERR_NOT_SUPPORTED;
}

reflex_err_t reflex_hal_pcnt_start(uint32_t edge_pin, uint32_t level_pin, int16_t low_limit,
                                   int16_t high_limit) {
    (void)edge_pin;
    (void)level_pin;
    (void)low_limit;
    (void)high_limit;
    return REFLEX_ERR_NOT_SUPPORTED;
}

int reflex_hal_pcnt_read(void) {
    return 0;
}

void reflex_hal_pcnt_stop(void) {}

void reflex_hal_pcnt_release(void) {}

void reflex_hal_rmt_snapshot(reflex_rmt_snapshot_t *out) {
    if (!out) return;
    memset(out, 0, sizeof *out);
}

/* Not implemented on this target: the sleep net is a C6 concern, because the
 * C6 is where Reflex intends to own the sleep entry. */
reflex_err_t reflex_hal_wdt_arm(uint32_t timeout_ms) {
    (void)timeout_ms;
    return REFLEX_ERR_NOT_SUPPORTED;
}

void reflex_hal_wdt_feed(void) {}
void reflex_hal_wdt_disarm(void) {}
void reflex_hal_stack_guard_disable(void) {}
void reflex_hal_wdt_disable_timg(void) {}
uint32_t reflex_hal_intr_quiesce_except(uint32_t keep_mask) {
    (void)keep_mask;
    return 0;
}
void reflex_hal_intr_restore(uint32_t mask) {
    (void)mask;
}
bool reflex_hal_wdt_armed(void) {
    return false;
}

void reflex_hal_wdt_regs(uint32_t *c0, uint32_t *c1) {
    if (c0) *c0 = 0;
    if (c1) *c1 = 0;
}

void reflex_hal_pcnt_snapshot(reflex_pcnt_snapshot_t *out) {
    if (!out) return;
    memset(out, 0, sizeof *out);
}

void reflex_hal_pwm_snapshot(reflex_pwm_snapshot_t *out) {
    if (!out) return;
    memset(out, 0, sizeof *out);
}

void reflex_hal_console_debug(reflex_console_debug_t *out) {
    if (!out) return;
    memset(out, 0, sizeof *out);
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
    /* Use the real prototype rather than a local extern. The hand-written
     * declaration here disagreed with esp_intr_alloc.h on both the return type
     * and the handle parameter, so the Xtensa build failed outright with
     * "conflicting types". Same failure mode as the hand-declared
     * heap_caps_get_free_size in goose_metabolic.c: re-declaring an SDK symbol
     * locally buys nothing and silently drifts from the header. */
    return (reflex_err_t)esp_intr_alloc(source, flags & REFLEX_INTR_FLAG_IRAM ? 1 : 0,
                                        handler, arg, (intr_handle_t *)out_handle);
}

reflex_err_t reflex_hal_intr_free(reflex_intr_handle_t handle) {
    return (reflex_err_t)esp_intr_free((intr_handle_t)handle);
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

void reflex_hal_write_raw(const char *data, int len) {
    /* ESP32 uses standard UART — printf/fwrite goes through. */
    fwrite(data, 1, (size_t)len, stdout);
    fflush(stdout);
}
