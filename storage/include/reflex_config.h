#ifndef REFLEX_CONFIG_H
#define REFLEX_CONFIG_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t reflex_config_init_defaults(void);

esp_err_t reflex_config_get_device_name(char *out, size_t out_len);
esp_err_t reflex_config_set_device_name(const char *value);

esp_err_t reflex_config_get_log_level(int32_t *out);
esp_err_t reflex_config_set_log_level(int32_t value);

esp_err_t reflex_config_get_wifi_ssid(char *out, size_t out_len);
esp_err_t reflex_config_set_wifi_ssid(const char *value);

esp_err_t reflex_config_get_wifi_password(char *out, size_t out_len);
esp_err_t reflex_config_set_wifi_password(const char *value);

esp_err_t reflex_config_get_safe_mode(bool *out);
esp_err_t reflex_config_set_safe_mode(bool value);

esp_err_t reflex_config_get_boot_count(int32_t *out);
esp_err_t reflex_config_set_boot_count(int32_t value);

#ifdef __cplusplus
}
#endif

#endif
