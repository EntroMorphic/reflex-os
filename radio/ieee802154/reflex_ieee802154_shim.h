/**
 * @file reflex_ieee802154_shim.h
 * @brief Shim layer mapping ESP-IDF primitives to Reflex OS equivalents.
 *
 * This header is included INSTEAD of the ESP-IDF headers by the
 * extracted 802.15.4 driver. The driver source stays unmodified;
 * we just provide its dependencies through our own primitives.
 *
 * Dependencies satisfied:
 *   portMUX_TYPE / portENTER_CRITICAL  → interrupt disable/enable
 *   esp_timer_get_time()               → reflex_hal_time_us()
 *   esp_intr_alloc/free                → direct RISC-V interrupt setup
 *   esp_log                            → printf stubs
 *   IRAM_ATTR                          → section attribute
 *   PM/coex                            → stubbed out
 */

#ifndef REFLEX_IEEE802154_SHIM_H
#define REFLEX_IEEE802154_SHIM_H

#include "reflex_hal.h"
#include <stdint.h>
#include <stdbool.h>
#include <string.h>

/* --- Spinlock (replaces FreeRTOS portMUX) --- */

typedef struct { uint32_t _dummy; } portMUX_TYPE;
#define portMUX_INITIALIZER_UNLOCKED { 0 }

static inline void portENTER_CRITICAL(portMUX_TYPE *m) {
    (void)m;
    __asm__ volatile ("csrci mstatus, 0x8");
}

static inline void portEXIT_CRITICAL(portMUX_TYPE *m) {
    (void)m;
    __asm__ volatile ("csrsi mstatus, 0x8");
}

/* --- Timer (replaces esp_timer) --- */

static inline int64_t esp_timer_get_time(void) {
    return (int64_t)reflex_hal_time_us();
}

/* --- Interrupt allocation (replaces esp_intr_alloc) --- */

typedef void *intr_handle_t;
typedef void (*intr_handler_t)(void *);

#define ESP_INTR_FLAG_IRAM 0

extern int esp_intr_alloc(int source, int flags, intr_handler_t handler,
                          void *arg, intr_handle_t *handle);
extern int esp_intr_free(intr_handle_t handle);

/* --- Logging (replaces esp_log) --- */

#define ESP_LOGE(tag, fmt, ...) ((void)0)
#define ESP_LOGW(tag, fmt, ...) ((void)0)
#define ESP_LOGI(tag, fmt, ...) ((void)0)
#define ESP_LOGD(tag, fmt, ...) ((void)0)
#define ESP_EARLY_LOGE(tag, fmt, ...) ((void)0)
#define ESP_LOG_LEVEL_LOCAL(level, tag, fmt, ...) ((void)0)

/* --- Attributes (replaces esp_attr.h) --- */

#define IRAM_ATTR __attribute__((section(".iram1")))
#define FORCE_INLINE_ATTR __attribute__((always_inline))
#define IEEE802154_STATIC static

/* --- PM/Sleep stubs --- */

#define CONFIG_PM_ENABLE 0
#define CONFIG_IEEE802154_SLEEP_ENABLE 0

/* --- Coexistence stubs --- */

#define CONFIG_ESP_COEX_SW_COEXIST_ENABLE 0
#define CONFIG_EXTERNAL_COEX_ENABLE 0
#define CONFIG_IEEE802154_TEST 0

static inline void esp_ieee802154_coex_pti_set(int type) { (void)type; }

/* --- Error codes --- */

#define ESP_OK 0
#define ESP_FAIL -1
#define ESP_ERR_INVALID_STATE 0x103

typedef int esp_err_t;

#endif /* REFLEX_IEEE802154_SHIM_H */
