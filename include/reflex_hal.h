#ifndef REFLEX_HAL_H
#define REFLEX_HAL_H

/**
 * @file reflex_hal.h
 * @brief Reflex OS Hardware Abstraction Layer.
 *
 * Every function the substrate needs from the platform is declared
 * here. Implementations live in platform/<target>/. Nothing outside
 * platform/ may include a vendor header directly.
 */

#include "reflex_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* --- RTC Memory Attribute ---
 * Platform-specific. On ESP32, this places data in RTC SLOW memory
 * that survives deep sleep. On other platforms, falls back to regular
 * static storage. */
#ifndef REFLEX_RTC_DATA_ATTR
#define REFLEX_RTC_DATA_ATTR
#endif

/* --- Time --- */

uint64_t reflex_hal_time_us(void);
uint32_t reflex_hal_cpu_cycles(void);
void     reflex_hal_delay_us(uint32_t us);

/* --- GPIO --- */

reflex_err_t reflex_hal_gpio_init_output(uint32_t pin);
reflex_err_t reflex_hal_gpio_init_input(uint32_t pin, bool pullup);
reflex_err_t reflex_hal_gpio_set_level(uint32_t pin, int level);
int          reflex_hal_gpio_get_level(uint32_t pin);
reflex_err_t reflex_hal_gpio_connect_out(uint32_t out_pin, uint32_t signal,
                                         bool invert, bool enable);

/* --- System --- */

void reflex_hal_reboot(void);

#define REFLEX_SLEEP_WAKEUP_UNDEFINED 0

int  reflex_hal_sleep_wakeup_cause(void);
void reflex_hal_sleep_enter(uint64_t duration_us);
void reflex_hal_random_fill(uint8_t *buf, size_t len);
reflex_err_t reflex_hal_mac_read(uint8_t mac[6]);

/* --- Temperature Sensor --- */

typedef void *reflex_temp_handle_t;
reflex_err_t reflex_hal_temp_init(reflex_temp_handle_t *out);
reflex_err_t reflex_hal_temp_read(reflex_temp_handle_t h, float *celsius);

/* --- Interrupts --- */

typedef void *reflex_intr_handle_t;
typedef void (*reflex_intr_handler_t)(void *arg);

#define REFLEX_INTR_FLAG_IRAM  (1 << 0)

reflex_err_t reflex_hal_intr_alloc(int source, int flags,
                                   reflex_intr_handler_t handler, void *arg,
                                   reflex_intr_handle_t *out_handle);
reflex_err_t reflex_hal_intr_free(reflex_intr_handle_t handle);

/* --- Log --- */

#define REFLEX_LOG_LEVEL_ERROR   1
#define REFLEX_LOG_LEVEL_WARN    2
#define REFLEX_LOG_LEVEL_INFO    3
#define REFLEX_LOG_LEVEL_DEBUG   4

void reflex_hal_log(int level, const char *tag, const char *fmt, ...)
    __attribute__((format(printf, 3, 4)));

#define REFLEX_LOGE(tag, fmt, ...) reflex_hal_log(REFLEX_LOG_LEVEL_ERROR, tag, fmt, ##__VA_ARGS__)
#define REFLEX_LOGW(tag, fmt, ...) reflex_hal_log(REFLEX_LOG_LEVEL_WARN,  tag, fmt, ##__VA_ARGS__)
#define REFLEX_LOGI(tag, fmt, ...) reflex_hal_log(REFLEX_LOG_LEVEL_INFO,  tag, fmt, ##__VA_ARGS__)
#define REFLEX_LOGD(tag, fmt, ...) reflex_hal_log(REFLEX_LOG_LEVEL_DEBUG, tag, fmt, ##__VA_ARGS__)

/* Override the types.h fallback now that HAL logging is available. */
#undef REFLEX_LOG_ERROR_IMPL
#define REFLEX_LOG_ERROR_IMPL(tag, fmt, ...) \
    reflex_hal_log(REFLEX_LOG_LEVEL_ERROR, tag, fmt, ##__VA_ARGS__)

#ifdef __cplusplus
}
#endif

#endif /* REFLEX_HAL_H */
