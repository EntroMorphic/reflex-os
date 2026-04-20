/**
 * @file mock_hal.c
 * @brief Mock implementation of reflex_hal.h for host-side testing.
 */

#include "reflex_hal.h"
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

static uint64_t s_mock_time_us = 0;
static uint8_t s_gpio_state[32] = {0};

uint64_t reflex_hal_time_us(void) { return s_mock_time_us; }
void reflex_hal_advance_time(uint64_t us) { s_mock_time_us += us; }

uint32_t reflex_hal_cpu_cycles(void) { return (uint32_t)s_mock_time_us; }
void reflex_hal_delay_us(uint32_t us) { s_mock_time_us += us; }

reflex_err_t reflex_hal_gpio_init_output(uint32_t pin) {
    if (pin >= 32) return REFLEX_ERR_INVALID_ARG;
    return REFLEX_OK;
}

reflex_err_t reflex_hal_gpio_init_input(uint32_t pin, bool pullup) {
    (void)pullup;
    if (pin >= 32) return REFLEX_ERR_INVALID_ARG;
    return REFLEX_OK;
}

reflex_err_t reflex_hal_gpio_set_level(uint32_t pin, int level) {
    if (pin >= 32) return REFLEX_ERR_INVALID_ARG;
    s_gpio_state[pin] = (uint8_t)(level ? 1 : 0);
    return REFLEX_OK;
}

int reflex_hal_gpio_get_level(uint32_t pin) {
    if (pin >= 32) return 0;
    return s_gpio_state[pin];
}

reflex_err_t reflex_hal_gpio_connect_out(uint32_t p, uint32_t s, bool i, bool e) {
    (void)p; (void)s; (void)i; (void)e;
    return REFLEX_OK;
}

void reflex_hal_reboot(void) { }
int reflex_hal_sleep_wakeup_cause(void) { return 0; }
void reflex_hal_sleep_enter(uint64_t us) { s_mock_time_us += us; }

void reflex_hal_random_fill(uint8_t *buf, size_t len) {
    for (size_t i = 0; i < len; i++) buf[i] = (uint8_t)(rand() & 0xFF);
}

reflex_err_t reflex_hal_mac_read(uint8_t mac[6]) {
    memcpy(mac, "\xAA\xBB\xCC\xDD\xEE\xFF", 6);
    return REFLEX_OK;
}

reflex_err_t reflex_hal_temp_init(reflex_temp_handle_t *out) {
    if (out) *out = (reflex_temp_handle_t)1;
    return REFLEX_OK;
}

reflex_err_t reflex_hal_temp_read(reflex_temp_handle_t h, float *c) {
    (void)h;
    if (c) *c = 25.0f;
    return REFLEX_OK;
}

reflex_err_t reflex_hal_intr_alloc(int s, int f, reflex_intr_handler_t h, void *a,
                                   reflex_intr_handle_t *o) {
    (void)s; (void)f; (void)h; (void)a;
    if (o) *o = (reflex_intr_handle_t)1;
    return REFLEX_OK;
}

reflex_err_t reflex_hal_intr_free(reflex_intr_handle_t h) {
    (void)h;
    return REFLEX_OK;
}

void reflex_hal_log(int level, const char *tag, const char *fmt, ...) {
    (void)level;
    (void)tag;
    (void)fmt;
}
