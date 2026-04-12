#include "reflex_config.h"

#include <string.h>

#include "esp_check.h"
#include "nvs.h"

static const char *REFLEX_CONFIG_NAMESPACE = "config";
static const char *REFLEX_CONFIG_DEFAULT_DEVICE_NAME = "reflex-os";
static const int32_t REFLEX_CONFIG_DEFAULT_LOG_LEVEL = 2;
static const char *REFLEX_CONFIG_DEFAULT_WIFI_SSID = "";
static const char *REFLEX_CONFIG_DEFAULT_WIFI_PASSWORD = "";
static const uint8_t REFLEX_CONFIG_DEFAULT_SAFE_MODE = 0;
static const int32_t REFLEX_CONFIG_DEFAULT_BOOT_COUNT = 0;

static esp_err_t reflex_config_open(nvs_open_mode_t mode, nvs_handle_t *out_handle)
{
    ESP_RETURN_ON_FALSE(out_handle != NULL, ESP_ERR_INVALID_ARG, "reflex.config", "handle is required");
    return nvs_open(REFLEX_CONFIG_NAMESPACE, mode, out_handle);
}

static esp_err_t reflex_config_set_default_str(nvs_handle_t handle, const char *key, const char *value)
{
    size_t size = 0;
    esp_err_t err = nvs_get_str(handle, key, NULL, &size);

    if (err == ESP_ERR_NVS_NOT_FOUND) {
        return nvs_set_str(handle, key, value);
    }

    return err == ESP_OK ? ESP_OK : err;
}

static esp_err_t reflex_config_set_default_i32(nvs_handle_t handle, const char *key, int32_t value)
{
    int32_t existing = 0;
    esp_err_t err = nvs_get_i32(handle, key, &existing);

    if (err == ESP_ERR_NVS_NOT_FOUND) {
        return nvs_set_i32(handle, key, value);
    }

    return err == ESP_OK ? ESP_OK : err;
}

static esp_err_t reflex_config_set_default_u8(nvs_handle_t handle, const char *key, uint8_t value)
{
    uint8_t existing = 0;
    esp_err_t err = nvs_get_u8(handle, key, &existing);

    if (err == ESP_ERR_NVS_NOT_FOUND) {
        return nvs_set_u8(handle, key, value);
    }

    return err == ESP_OK ? ESP_OK : err;
}

static esp_err_t reflex_config_get_string(const char *key, char *out, size_t out_len)
{
    nvs_handle_t handle;
    size_t size = out_len;
    esp_err_t err;

    ESP_RETURN_ON_FALSE(out != NULL, ESP_ERR_INVALID_ARG, "reflex.config", "output is required");
    ESP_RETURN_ON_FALSE(out_len > 0, ESP_ERR_INVALID_ARG, "reflex.config", "output length is required");

    ESP_RETURN_ON_ERROR(reflex_config_open(NVS_READONLY, &handle), "reflex.config", "open failed");
    err = nvs_get_str(handle, key, out, &size);
    nvs_close(handle);
    return err;
}

static esp_err_t reflex_config_set_string(const char *key, const char *value)
{
    nvs_handle_t handle;
    esp_err_t err;

    ESP_RETURN_ON_FALSE(value != NULL, ESP_ERR_INVALID_ARG, "reflex.config", "value is required");
    ESP_RETURN_ON_ERROR(reflex_config_open(NVS_READWRITE, &handle), "reflex.config", "open failed");
    err = nvs_set_str(handle, key, value);
    if (err == ESP_OK) {
        err = nvs_commit(handle);
    }
    nvs_close(handle);
    return err;
}

esp_err_t reflex_config_init_defaults(void)
{
    nvs_handle_t handle;
    esp_err_t err;

    ESP_RETURN_ON_ERROR(reflex_config_open(NVS_READWRITE, &handle), "reflex.config", "open failed");

    err = reflex_config_set_default_str(handle, "device_name", REFLEX_CONFIG_DEFAULT_DEVICE_NAME);
    if (err != ESP_OK) {
        goto fail;
    }

    err = reflex_config_set_default_i32(handle, "log_level", REFLEX_CONFIG_DEFAULT_LOG_LEVEL);
    if (err != ESP_OK) {
        goto fail;
    }

    err = reflex_config_set_default_str(handle, "wifi_ssid", REFLEX_CONFIG_DEFAULT_WIFI_SSID);
    if (err != ESP_OK) {
        goto fail;
    }

    err = reflex_config_set_default_str(handle, "wifi_password", REFLEX_CONFIG_DEFAULT_WIFI_PASSWORD);
    if (err != ESP_OK) {
        goto fail;
    }

    err = reflex_config_set_default_u8(handle, "safe_mode", REFLEX_CONFIG_DEFAULT_SAFE_MODE);
    if (err != ESP_OK) {
        goto fail;
    }

    err = reflex_config_set_default_i32(handle, "boot_count", REFLEX_CONFIG_DEFAULT_BOOT_COUNT);
    if (err != ESP_OK) {
        goto fail;
    }

    err = nvs_commit(handle);
    if (err != ESP_OK) {
        goto fail;
    }
    nvs_close(handle);
    return ESP_OK;

fail:
    nvs_close(handle);
    return err;
}

esp_err_t reflex_config_get_device_name(char *out, size_t out_len)
{
    return reflex_config_get_string("device_name", out, out_len);
}

esp_err_t reflex_config_set_device_name(const char *value)
{
    return reflex_config_set_string("device_name", value);
}

esp_err_t reflex_config_get_log_level(int32_t *out)
{
    nvs_handle_t handle;
    esp_err_t err;

    ESP_RETURN_ON_FALSE(out != NULL, ESP_ERR_INVALID_ARG, "reflex.config", "output is required");
    ESP_RETURN_ON_ERROR(reflex_config_open(NVS_READONLY, &handle), "reflex.config", "open failed");
    err = nvs_get_i32(handle, "log_level", out);
    nvs_close(handle);
    return err;
}

esp_err_t reflex_config_set_log_level(int32_t value)
{
    nvs_handle_t handle;
    esp_err_t err;

    ESP_RETURN_ON_ERROR(reflex_config_open(NVS_READWRITE, &handle), "reflex.config", "open failed");
    err = nvs_set_i32(handle, "log_level", value);
    if (err == ESP_OK) {
        err = nvs_commit(handle);
    }
    nvs_close(handle);
    return err;
}

esp_err_t reflex_config_get_wifi_ssid(char *out, size_t out_len)
{
    return reflex_config_get_string("wifi_ssid", out, out_len);
}

esp_err_t reflex_config_set_wifi_ssid(const char *value)
{
    return reflex_config_set_string("wifi_ssid", value);
}

esp_err_t reflex_config_get_wifi_password(char *out, size_t out_len)
{
    return reflex_config_get_string("wifi_password", out, out_len);
}

esp_err_t reflex_config_set_wifi_password(const char *value)
{
    return reflex_config_set_string("wifi_password", value);
}

esp_err_t reflex_config_get_safe_mode(bool *out)
{
    nvs_handle_t handle;
    uint8_t value = 0;
    esp_err_t err;

    ESP_RETURN_ON_FALSE(out != NULL, ESP_ERR_INVALID_ARG, "reflex.config", "output is required");
    ESP_RETURN_ON_ERROR(reflex_config_open(NVS_READONLY, &handle), "reflex.config", "open failed");
    err = nvs_get_u8(handle, "safe_mode", &value);
    nvs_close(handle);
    if (err == ESP_OK) {
        *out = value != 0;
    }
    return err;
}

esp_err_t reflex_config_set_safe_mode(bool value)
{
    nvs_handle_t handle;
    esp_err_t err;

    ESP_RETURN_ON_ERROR(reflex_config_open(NVS_READWRITE, &handle), "reflex.config", "open failed");
    err = nvs_set_u8(handle, "safe_mode", value ? 1 : 0);
    if (err == ESP_OK) {
        err = nvs_commit(handle);
    }
    nvs_close(handle);
    return err;
}

esp_err_t reflex_config_get_boot_count(int32_t *out)
{
    nvs_handle_t handle;
    esp_err_t err;

    ESP_RETURN_ON_FALSE(out != NULL, ESP_ERR_INVALID_ARG, "reflex.config", "output is required");
    ESP_RETURN_ON_ERROR(reflex_config_open(NVS_READONLY, &handle), "reflex.config", "open failed");
    err = nvs_get_i32(handle, "boot_count", out);
    nvs_close(handle);
    return err;
}

esp_err_t reflex_config_set_boot_count(int32_t value)
{
    nvs_handle_t handle;
    esp_err_t err;

    ESP_RETURN_ON_ERROR(reflex_config_open(NVS_READWRITE, &handle), "reflex.config", "open failed");
    err = nvs_set_i32(handle, "boot_count", value);
    if (err == ESP_OK) {
        err = nvs_commit(handle);
    }
    nvs_close(handle);
    return err;
}
