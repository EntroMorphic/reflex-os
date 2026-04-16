#ifndef REFLEX_CONFIG_H
#define REFLEX_CONFIG_H

/**
 * @file reflex_config.h
 * @brief Typed persistent configuration store backed by NVS.
 *
 * Every getter/setter maps to a single NVS key in the "reflex"
 * namespace. Values are strict typed (no generic blob API) so
 * callers can't accidentally store the wrong type under a key.
 * String getters take an explicit buffer + length; setters take a
 * null-terminated string.
 *
 * The safe-mode and boot-count entries back the host's crash-loop
 * detector: core/boot increments the boot counter on each start,
 * then sets it back to zero after the stability window elapses
 * without a panic (see reflex_stability_task in main.c). If the
 * counter crosses REFLEX_BOOT_LOOP_THRESHOLD, main.c enables safe
 * mode for the next boot and re-enters the shell without running
 * the full service bring-up.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "reflex_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Seed the config namespace with first-boot defaults if empty. */
reflex_err_t reflex_config_init_defaults(void);

/** @brief Human-friendly device name used by the banner and telemetry. */
reflex_err_t reflex_config_get_device_name(char *out, size_t out_len);
reflex_err_t reflex_config_set_device_name(const char *value);

/** @brief Persisted log-level token read by #reflex_log_init. */
reflex_err_t reflex_config_get_log_level(int32_t *out);
reflex_err_t reflex_config_set_log_level(int32_t value);

/** @brief Wi-Fi STA credentials. Empty SSID = "no creds, LP+ESP-NOW only". */
reflex_err_t reflex_config_get_wifi_ssid(char *out, size_t out_len);
reflex_err_t reflex_config_set_wifi_ssid(const char *value);
reflex_err_t reflex_config_get_wifi_password(char *out, size_t out_len);
reflex_err_t reflex_config_set_wifi_password(const char *value);

/** @brief Crash-loop detector: if true at boot, main.c enters safe mode. */
reflex_err_t reflex_config_get_safe_mode(bool *out);
reflex_err_t reflex_config_set_safe_mode(bool value);

/** @brief Crash-loop detector: incremented on each boot, reset after stability. */
reflex_err_t reflex_config_get_boot_count(int32_t *out);
reflex_err_t reflex_config_set_boot_count(int32_t value);

#ifdef __cplusplus
}
#endif

#endif
