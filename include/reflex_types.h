#ifndef REFLEX_TYPES_H
#define REFLEX_TYPES_H

/**
 * @file reflex_types.h
 * @brief Reflex OS portable error codes and return macros.
 *
 * This header replaces esp_err.h and esp_check.h for all Reflex OS
 * code outside the platform/ directory. Platform backends map these
 * to the native error system (e.g., esp_err_t on ESP-IDF).
 */

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef int reflex_err_t;

#define REFLEX_OK                    0
#define REFLEX_FAIL                 -1
#define REFLEX_ERR_NO_MEM           0x101
#define REFLEX_ERR_INVALID_ARG      0x102
#define REFLEX_ERR_INVALID_STATE    0x103
#define REFLEX_ERR_INVALID_SIZE     0x104
#define REFLEX_ERR_NOT_FOUND        0x105
#define REFLEX_ERR_NOT_SUPPORTED    0x106
#define REFLEX_ERR_TIMEOUT          0x107
#define REFLEX_ERR_INVALID_RESPONSE 0x108
#define REFLEX_ERR_INVALID_CRC      0x109
#define REFLEX_ERR_INVALID_VERSION  0x10A

/**
 * @brief Return early if `expr` evaluates to an error.
 * Logs the tag and message on failure via reflex_hal_log if available,
 * otherwise a plain return.
 */
#define REFLEX_RETURN_ON_ERROR(expr, tag, msg) do {     \
    reflex_err_t _err = (expr);                         \
    if (_err != REFLEX_OK) {                            \
        REFLEX_LOG_ERROR_IMPL(tag, "%s: %s (0x%x)",    \
                              __func__, msg, (unsigned)_err); \
        return _err;                                    \
    }                                                   \
} while(0)

#define REFLEX_RETURN_ON_FALSE(cond, err, tag, msg) do { \
    if (!(cond)) {                                       \
        REFLEX_LOG_ERROR_IMPL(tag, "%s: %s",             \
                              __func__, msg);            \
        return (err);                                    \
    }                                                    \
} while(0)

/*
 * The log implementation macro is provided by the platform or by
 * reflex_hal.h. When compiling for host tests without a HAL, it
 * falls back to fprintf(stderr, ...).
 */
#ifndef REFLEX_LOG_ERROR_IMPL
#include <stdio.h>
#define REFLEX_LOG_ERROR_IMPL(tag, fmt, ...) \
    fprintf(stderr, "E (%s) " fmt "\n", tag, ##__VA_ARGS__)
#endif

#ifdef __cplusplus
}
#endif

#endif /* REFLEX_TYPES_H */
