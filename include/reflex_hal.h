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
 * Defined per target by the platform CMakeLists, NOT here.
 *
 *   ESP32-C6  __attribute__((section(".rtc.data")))  -> LP RAM, survives sleep
 *   ESP32     (empty)                                -> ordinary DRAM, does NOT
 *   host      (empty)
 *
 * The classic ESP32 is empty because the data does not fit, not as an
 * oversight. fabric_cells[] alone is 12288 bytes (256 x 48) against 8168 bytes
 * of usable RTC slow memory on that chip — RTC fast is the same 8 KB and is
 * PRO-CPU-only besides. Setting it there would fail the link on IDF's
 * "RTC_SLOW segment data does not fit" assert rather than corrupt anything.
 *
 * An earlier version of this comment claimed the opposite ("On ESP32, this
 * places data in RTC SLOW memory that survives deep sleep"), which was true of
 * neither ESP32 target.
 *
 * Note that even on the C6 this buys less than it appears: goonies_registry[]
 * holds the cell *names* and is not RTC-attributed on any target, so names are
 * rebuilt from the atlas on every wake. */
#ifndef REFLEX_RTC_DATA_ATTR
#define REFLEX_RTC_DATA_ATTR
#endif

/**
 * @brief Format one log line into a caller-supplied buffer.
 *
 * Writes "<prefix> (<tag>) <formatted>\n", truncating rather than overflowing.
 * Pure and platform-independent so the host suite can exercise it — the C6 HAL
 * previously inlined this and mis-accumulated snprintf's return value, which
 * let it read past its own buffer and emit adjacent memory over the wire.
 *
 * @return bytes placed in @p buf, always <= @p buf_len. Pass this length
 *         directly to the platform write primitive; the result is not
 *         NUL-terminated when it exactly fills the buffer.
 */
size_t reflex_log_format(char *buf, size_t buf_len,
                         const char *prefix, const char *tag,
                         const char *fmt, va_list args);

/* --- Time --- */

/** @return Microseconds since boot (monotonic). */
uint64_t reflex_hal_time_us(void);
/** @return Raw CPU cycle counter (wraps). */
uint32_t reflex_hal_cpu_cycles(void);
void     reflex_hal_delay_us(uint32_t us);

/* --- GPIO --- */

reflex_err_t reflex_hal_gpio_init_output(uint32_t pin);
reflex_err_t reflex_hal_gpio_init_input(uint32_t pin, bool pullup);
reflex_err_t reflex_hal_gpio_set_level(uint32_t pin, int level);
/**
 * @brief Read a GPIO input level.
 * @return 1 or 0 — and **0 for an out-of-range pin**, which is
 *         indistinguishable from a genuine low. Validate the pin number
 *         before calling if the difference matters.
 */
int          reflex_hal_gpio_get_level(uint32_t pin);
reflex_err_t reflex_hal_gpio_connect_out(uint32_t out_pin, uint32_t signal,
                                         bool invert, bool enable);

/* --- System --- */

void reflex_hal_reboot(void);

#define REFLEX_SLEEP_WAKEUP_UNDEFINED 0

/** @brief Why the last boot happened — used to tell a cold boot from a
 *  deep-sleep wake, which is what decides whether the fabric is re-seeded. */
int  reflex_hal_sleep_wakeup_cause(void);
/**
 * @brief Enter deep sleep for @p duration_us. **Does not return.**
 *
 * On the ESP32-C6 this is a true deep sleep with an LP-timer wakeup. On
 * targets without one it stores the duration in an always-on register and
 * reboots, so either way execution does not continue past this call.
 */
void reflex_hal_sleep_enter(uint64_t duration_us);
/** @brief Fill @p buf with hardware entropy. Used for the per-board Aura key
 *  and for arc nonces, so it must be a real RNG rather than a PRNG seeded at
 *  a predictable point in boot. */
void reflex_hal_random_fill(uint8_t *buf, size_t len);
/** @brief Read the factory MAC. Doubles as this board's mesh identity, so it
 *  is what self-arc suppression and the peer table compare against. */
reflex_err_t reflex_hal_mac_read(uint8_t mac[6]);

/* --- Temperature Sensor --- */

typedef void *reflex_temp_handle_t;
reflex_err_t reflex_hal_temp_init(reflex_temp_handle_t *out);
reflex_err_t reflex_hal_temp_read(reflex_temp_handle_t h, float *celsius);

/* --- Interrupts --- */

typedef void *reflex_intr_handle_t;
typedef void (*reflex_intr_handler_t)(void *arg);

#define REFLEX_INTR_FLAG_IRAM  (1 << 0)

/** @param source Platform interrupt source number.
 *  @param flags REFLEX_INTR_FLAG_IRAM to place handler in IRAM.
 *  @param out_handle Receives the allocated interrupt handle. */
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

/** Direct serial write bypassing stdio buffering. Used by telemetry. */
void reflex_hal_write_raw(const char *data, int len);

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
