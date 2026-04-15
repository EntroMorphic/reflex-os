#ifndef REFLEX_LOG_H
#define REFLEX_LOG_H

/**
 * @file reflex_log.h
 * @brief Thin facade over ESP-IDF's ESP_LOG macros.
 *
 * Exists so the rest of the codebase can route through a single
 * logging surface that could be swapped (e.g. for a file sink or
 * remote telemetry) without touching every call site. The current
 * implementation is a direct passthrough to ESP_LOG.
 *
 * Use REFLEX_LOGI / REFLEX_LOGW / REFLEX_LOGE with a stable tag
 * string so grep-by-tag works in the serial log.
 */

#include "esp_log.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Initialize log-level policy from persisted config. */
void reflex_log_init(void);

/** @brief Override the global log threshold at runtime. */
void reflex_log_set_level(esp_log_level_t level);

/** @brief Read back the currently-effective log threshold. */
esp_log_level_t reflex_log_get_level(void);

/** @brief Tag used by main.c and core/boot.c for the startup banner. */
#define REFLEX_BOOT_TAG "reflex.boot"
/** @brief Tag used by the serial shell dispatcher. */
#define REFLEX_SHELL_TAG "reflex.shell"

#define REFLEX_LOGI(tag, format, ...) ESP_LOGI(tag, format, ##__VA_ARGS__)
#define REFLEX_LOGW(tag, format, ...) ESP_LOGW(tag, format, ##__VA_ARGS__)
#define REFLEX_LOGE(tag, format, ...) ESP_LOGE(tag, format, ##__VA_ARGS__)

#ifdef __cplusplus
}
#endif

#endif
