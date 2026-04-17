/**
 * @file reflex_hal_esp32c6.c
 * @brief Reflex HAL — ESP32-C6 / ESP-IDF backend.
 */

#include "reflex_hal.h"
#include "driver/temperature_sensor.h"

#include <stdarg.h>
#include <stdio.h>
/* SYSTIMER direct read (Gap C — replaces esp_timer_get_time) */
#define SYSTIMER_BASE_ADDR      0x60004000
#define SYSTIMER_UNIT0_OP       (SYSTIMER_BASE_ADDR + 0x04)
#define SYSTIMER_UNIT0_VAL_HI   (SYSTIMER_BASE_ADDR + 0x40)
#define SYSTIMER_UNIT0_VAL_LO   (SYSTIMER_BASE_ADDR + 0x44)
#define REG32_HAL(a) (*(volatile uint32_t *)(a))
#include "esp_sleep.h"
#include "esp_rom_sys.h"

/* Hardware RNG register */
#define RNG_DATA_REG     0x600B2808

/* eFuse MAC address registers */
#define EFUSE_MAC0_REG   0x600B0844
#define EFUSE_MAC1_REG   0x600B0848

/* GPIO registers (Gap G — replaces driver/gpio.h) */
#define GPIO_BASE_ADDR        0x60091000
#define GPIO_OUT_W1TS         (GPIO_BASE_ADDR + 0x08)
#define GPIO_OUT_W1TC         (GPIO_BASE_ADDR + 0x0C)
#define GPIO_ENABLE_W1TS      (GPIO_BASE_ADDR + 0x24)
#define GPIO_ENABLE_W1TC      (GPIO_BASE_ADDR + 0x28)
#define GPIO_IN_REG_ADDR      (GPIO_BASE_ADDR + 0x3C)
#define GPIO_FUNC_OUT_BASE    (GPIO_BASE_ADDR + 0x554)
/* IO MUX — per-pin config (function select, pull-up/down, input enable) */
#define IO_MUX_BASE_ADDR      0x60090000
#define IO_MUX_PIN_REG(n)     (IO_MUX_BASE_ADDR + 4 + (n) * 4)
#define IO_MUX_FUN_PU_BIT     (1 << 8)
#define IO_MUX_FUN_PD_BIT     (1 << 7)
#define IO_MUX_FUN_IE_BIT     (1 << 9)
#define IO_MUX_MCU_SEL_BITS   (0x7 << 12)

/* ROM function for GPIO matrix signal routing */
extern void esp_rom_gpio_connect_out_signal(uint32_t gpio, uint32_t signal, bool invert, bool oen_inv);

uint64_t reflex_hal_time_us(void) {
    /* Trigger UNIT0 value latch and read. XTAL = 40 MHz, divide by 40.
     * Bounded spin (max 20 iterations) — the latch completes in ~2 cycles. */
    REG32_HAL(SYSTIMER_UNIT0_OP) = (1U << 30);
    for (volatile int i = 0; i < 20; i++) {
        if (REG32_HAL(SYSTIMER_UNIT0_OP) & (1U << 29)) break;
    }
    uint32_t hi = REG32_HAL(SYSTIMER_UNIT0_VAL_HI);
    uint32_t lo = REG32_HAL(SYSTIMER_UNIT0_VAL_LO);
    return (((uint64_t)hi << 32) | lo) / 40;
}

uint32_t reflex_hal_cpu_cycles(void) {
    uint32_t cycles;
    __asm__ volatile ("csrr %0, 0x7E2" : "=r"(cycles)); /* C6 PCCR (machine mode) */
    return cycles;
}

void reflex_hal_delay_us(uint32_t us) {
    esp_rom_delay_us(us);
}

reflex_err_t reflex_hal_gpio_init_output(uint32_t pin) {
    if (pin >= 31) return REFLEX_ERR_INVALID_ARG;
    /* IO MUX: GPIO function (MCU_SEL=1), no pulls, no input */
    uint32_t mux = REG32_HAL(IO_MUX_PIN_REG(pin));
    mux &= ~(IO_MUX_MCU_SEL_BITS | IO_MUX_FUN_IE_BIT | IO_MUX_FUN_PU_BIT | IO_MUX_FUN_PD_BIT);
    mux |= (1 << 12);  /* MCU_SEL = 1 → GPIO function */
    REG32_HAL(IO_MUX_PIN_REG(pin)) = mux;
    /* Enable output + set func_out_sel to 0x80 (simple GPIO output) */
    REG32_HAL(GPIO_ENABLE_W1TS) = (1 << pin);
    REG32_HAL(GPIO_FUNC_OUT_BASE + pin * 4) = 0x80;
    return REFLEX_OK;
}

reflex_err_t reflex_hal_gpio_init_input(uint32_t pin, bool pullup) {
    if (pin >= 31) return REFLEX_ERR_INVALID_ARG;
    uint32_t mux = REG32_HAL(IO_MUX_PIN_REG(pin));
    mux &= ~(IO_MUX_MCU_SEL_BITS | IO_MUX_FUN_PU_BIT | IO_MUX_FUN_PD_BIT);
    mux |= (1 << 12) | IO_MUX_FUN_IE_BIT;  /* GPIO func + input enable */
    if (pullup) mux |= IO_MUX_FUN_PU_BIT;
    REG32_HAL(IO_MUX_PIN_REG(pin)) = mux;
    /* Disable output */
    REG32_HAL(GPIO_ENABLE_W1TC) = (1 << pin);
    return REFLEX_OK;
}

reflex_err_t reflex_hal_gpio_set_level(uint32_t pin, int level) {
    if (pin >= 31) return REFLEX_ERR_INVALID_ARG;
    if (level) REG32_HAL(GPIO_OUT_W1TS) = (1 << pin);
    else       REG32_HAL(GPIO_OUT_W1TC) = (1 << pin);
    return REFLEX_OK;
}

int reflex_hal_gpio_get_level(uint32_t pin) {
    return (REG32_HAL(GPIO_IN_REG_ADDR) >> pin) & 1;
}

reflex_err_t reflex_hal_gpio_connect_out(uint32_t out_pin, uint32_t signal,
                                         bool invert, bool enable) {
    esp_rom_gpio_connect_out_signal(out_pin, signal, invert, !enable);
    return REFLEX_OK;
}

void reflex_hal_reboot(void) {
    extern void software_reset(void);  /* ROM function at 0x40000090 */
    software_reset();
}

int reflex_hal_sleep_wakeup_cause(void) {
    return (int)esp_sleep_get_wakeup_cause();
}

void reflex_hal_sleep_enter(uint64_t duration_us) {
    esp_sleep_enable_timer_wakeup(duration_us);
    esp_deep_sleep_start();
}

void reflex_hal_random_fill(uint8_t *buf, size_t len) {
    for (size_t i = 0; i < len; i += 4) {
        uint32_t rnd = REG32_HAL(RNG_DATA_REG);
        size_t n = (len - i < 4) ? len - i : 4;
        for (size_t j = 0; j < n; j++) buf[i + j] = (uint8_t)(rnd >> (j * 8));
    }
}

reflex_err_t reflex_hal_mac_read(uint8_t mac[6]) {
    uint32_t w0 = REG32_HAL(EFUSE_MAC0_REG);
    uint32_t w1 = REG32_HAL(EFUSE_MAC1_REG);
    mac[0] = (uint8_t)(w1 >> 8);
    mac[1] = (uint8_t)(w1);
    mac[2] = (uint8_t)(w0 >> 24);
    mac[3] = (uint8_t)(w0 >> 16);
    mac[4] = (uint8_t)(w0 >> 8);
    mac[5] = (uint8_t)(w0);
    return REFLEX_OK;
}

reflex_err_t reflex_hal_temp_init(reflex_temp_handle_t *out) {
    temperature_sensor_config_t cfg = TEMPERATURE_SENSOR_CONFIG_DEFAULT(10, 50);
    temperature_sensor_handle_t h = NULL;
    esp_err_t rc = temperature_sensor_install(&cfg, &h);
    if (rc != ESP_OK) return (reflex_err_t)rc;
    rc = temperature_sensor_enable(h);
    if (rc != ESP_OK) return (reflex_err_t)rc;
    *out = (reflex_temp_handle_t)h;
    return REFLEX_OK;
}

reflex_err_t reflex_hal_temp_read(reflex_temp_handle_t h, float *celsius) {
    return (reflex_err_t)temperature_sensor_get_celsius((temperature_sensor_handle_t)h, celsius);
}

void reflex_hal_log(int level, const char *tag, const char *fmt, ...) {
    const char *prefix = (level == REFLEX_LOG_LEVEL_ERROR) ? "E" :
                         (level == REFLEX_LOG_LEVEL_WARN)  ? "W" :
                         (level == REFLEX_LOG_LEVEL_DEBUG) ? "D" : "I";
    char buf[256];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    esp_rom_printf("%s (%lu) %s: %s\n", prefix,
                   (unsigned long)(reflex_hal_time_us() / 1000), tag, buf);
}
