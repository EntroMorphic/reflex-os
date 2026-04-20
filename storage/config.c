/** @file config.c
 * @brief Persistent configuration key-value accessors.
 */
#include "reflex_config.h"

#include <string.h>

#include "reflex_types.h"
#include "reflex_kv.h"

static const char *REFLEX_CONFIG_NAMESPACE = "config";
static const char *REFLEX_CONFIG_DEFAULT_DEVICE_NAME = "reflex-os";
static const int32_t REFLEX_CONFIG_DEFAULT_LOG_LEVEL = 2;
static const char *REFLEX_CONFIG_DEFAULT_WIFI_SSID = "";
static const char *REFLEX_CONFIG_DEFAULT_WIFI_PASSWORD = "";
static const uint8_t REFLEX_CONFIG_DEFAULT_SAFE_MODE = 0;
static const int32_t REFLEX_CONFIG_DEFAULT_BOOT_COUNT = 0;

static reflex_err_t reflex_config_open(bool readonly, reflex_kv_handle_t *out_handle)
{
    REFLEX_RETURN_ON_FALSE(out_handle != NULL, REFLEX_ERR_INVALID_ARG, "reflex.config", "handle is required");
    return reflex_kv_open(REFLEX_CONFIG_NAMESPACE, readonly, out_handle);
}

static reflex_err_t reflex_config_set_default_str(reflex_kv_handle_t handle, const char *key, const char *value)
{
    size_t size = 0;
    reflex_err_t err = reflex_kv_get_str(handle, key, NULL, &size);

    if (err != REFLEX_OK) {
        return reflex_kv_set_str(handle, key, value);
    }
    return REFLEX_OK;
}

static reflex_err_t reflex_config_set_default_i32(reflex_kv_handle_t handle, const char *key, int32_t value)
{
    int32_t existing = 0;
    reflex_err_t err = reflex_kv_get_i32(handle, key, &existing);

    if (err != REFLEX_OK) {
        return reflex_kv_set_i32(handle, key, value);
    }
    return REFLEX_OK;
}

static reflex_err_t reflex_config_set_default_u8(reflex_kv_handle_t handle, const char *key, uint8_t value)
{
    uint8_t existing = 0;
    reflex_err_t err = reflex_kv_get_u8(handle, key, &existing);

    if (err != REFLEX_OK) {
        return reflex_kv_set_u8(handle, key, value);
    }
    return REFLEX_OK;
}

static reflex_err_t reflex_config_get_string(const char *key, char *out, size_t out_len)
{
    reflex_kv_handle_t handle;
    size_t size = out_len;
    reflex_err_t err;

    REFLEX_RETURN_ON_FALSE(out != NULL, REFLEX_ERR_INVALID_ARG, "reflex.config", "output is required");
    REFLEX_RETURN_ON_FALSE(out_len > 0, REFLEX_ERR_INVALID_ARG, "reflex.config", "output length is required");

    REFLEX_RETURN_ON_ERROR(reflex_config_open(true, &handle), "reflex.config", "open failed");
    err = reflex_kv_get_str(handle, key, out, &size);
    reflex_kv_close(handle);
    return err;
}

static reflex_err_t reflex_config_set_string(const char *key, const char *value)
{
    reflex_kv_handle_t handle;
    reflex_err_t err;

    REFLEX_RETURN_ON_FALSE(value != NULL, REFLEX_ERR_INVALID_ARG, "reflex.config", "value is required");
    REFLEX_RETURN_ON_ERROR(reflex_config_open(false, &handle), "reflex.config", "open failed");
    err = reflex_kv_set_str(handle, key, value);
    if (err == REFLEX_OK) {
        err = reflex_kv_commit(handle);
    }
    reflex_kv_close(handle);
    return err;
}

reflex_err_t reflex_config_init_defaults(void)
{
    reflex_kv_handle_t handle;
    reflex_err_t err;

    REFLEX_RETURN_ON_ERROR(reflex_config_open(false, &handle), "reflex.config", "open failed");

    err = reflex_config_set_default_str(handle, "device_name", REFLEX_CONFIG_DEFAULT_DEVICE_NAME);
    if (err != REFLEX_OK) {
        goto fail;
    }

    err = reflex_config_set_default_i32(handle, "log_level", REFLEX_CONFIG_DEFAULT_LOG_LEVEL);
    if (err != REFLEX_OK) {
        goto fail;
    }

    err = reflex_config_set_default_str(handle, "wifi_ssid", REFLEX_CONFIG_DEFAULT_WIFI_SSID);
    if (err != REFLEX_OK) {
        goto fail;
    }

    err = reflex_config_set_default_str(handle, "wifi_password", REFLEX_CONFIG_DEFAULT_WIFI_PASSWORD);
    if (err != REFLEX_OK) {
        goto fail;
    }

    err = reflex_config_set_default_u8(handle, "safe_mode", REFLEX_CONFIG_DEFAULT_SAFE_MODE);
    if (err != REFLEX_OK) {
        goto fail;
    }

    err = reflex_config_set_default_i32(handle, "boot_count", REFLEX_CONFIG_DEFAULT_BOOT_COUNT);
    if (err != REFLEX_OK) {
        goto fail;
    }

    err = reflex_kv_commit(handle);
    if (err != REFLEX_OK) {
        goto fail;
    }
    reflex_kv_close(handle);
    return REFLEX_OK;

fail:
    reflex_kv_close(handle);
    return err;
}

reflex_err_t reflex_config_get_device_name(char *out, size_t out_len)
{
    return reflex_config_get_string("device_name", out, out_len);
}

reflex_err_t reflex_config_set_device_name(const char *value)
{
    return reflex_config_set_string("device_name", value);
}

reflex_err_t reflex_config_get_log_level(int32_t *out)
{
    reflex_kv_handle_t handle;
    reflex_err_t err;

    REFLEX_RETURN_ON_FALSE(out != NULL, REFLEX_ERR_INVALID_ARG, "reflex.config", "output is required");
    REFLEX_RETURN_ON_ERROR(reflex_config_open(true, &handle), "reflex.config", "open failed");
    err = reflex_kv_get_i32(handle, "log_level", out);
    reflex_kv_close(handle);
    return err;
}

reflex_err_t reflex_config_set_log_level(int32_t value)
{
    reflex_kv_handle_t handle;
    reflex_err_t err;

    REFLEX_RETURN_ON_ERROR(reflex_config_open(false, &handle), "reflex.config", "open failed");
    err = reflex_kv_set_i32(handle, "log_level", value);
    if (err == REFLEX_OK) {
        err = reflex_kv_commit(handle);
    }
    reflex_kv_close(handle);
    return err;
}

reflex_err_t reflex_config_get_wifi_ssid(char *out, size_t out_len)
{
    return reflex_config_get_string("wifi_ssid", out, out_len);
}

reflex_err_t reflex_config_set_wifi_ssid(const char *value)
{
    return reflex_config_set_string("wifi_ssid", value);
}

reflex_err_t reflex_config_get_wifi_password(char *out, size_t out_len)
{
    return reflex_config_get_string("wifi_password", out, out_len);
}

reflex_err_t reflex_config_set_wifi_password(const char *value)
{
    return reflex_config_set_string("wifi_password", value);
}

reflex_err_t reflex_config_get_safe_mode(bool *out)
{
    reflex_kv_handle_t handle;
    uint8_t value = 0;
    reflex_err_t err;

    REFLEX_RETURN_ON_FALSE(out != NULL, REFLEX_ERR_INVALID_ARG, "reflex.config", "output is required");
    REFLEX_RETURN_ON_ERROR(reflex_config_open(true, &handle), "reflex.config", "open failed");
    err = reflex_kv_get_u8(handle, "safe_mode", &value);
    reflex_kv_close(handle);
    if (err == REFLEX_OK) {
        *out = value != 0;
    }
    return err;
}

reflex_err_t reflex_config_set_safe_mode(bool value)
{
    reflex_kv_handle_t handle;
    reflex_err_t err;

    REFLEX_RETURN_ON_ERROR(reflex_config_open(false, &handle), "reflex.config", "open failed");
    err = reflex_kv_set_u8(handle, "safe_mode", value ? 1 : 0);
    if (err == REFLEX_OK) {
        err = reflex_kv_commit(handle);
    }
    reflex_kv_close(handle);
    return err;
}

reflex_err_t reflex_config_get_boot_count(int32_t *out)
{
    reflex_kv_handle_t handle;
    reflex_err_t err;

    REFLEX_RETURN_ON_FALSE(out != NULL, REFLEX_ERR_INVALID_ARG, "reflex.config", "output is required");
    REFLEX_RETURN_ON_ERROR(reflex_config_open(true, &handle), "reflex.config", "open failed");
    err = reflex_kv_get_i32(handle, "boot_count", out);
    reflex_kv_close(handle);
    return err;
}

reflex_err_t reflex_config_set_boot_count(int32_t value)
{
    reflex_kv_handle_t handle;
    reflex_err_t err;

    REFLEX_RETURN_ON_ERROR(reflex_config_open(false, &handle), "reflex.config", "open failed");
    err = reflex_kv_set_i32(handle, "boot_count", value);
    if (err == REFLEX_OK) {
        err = reflex_kv_commit(handle);
    }
    reflex_kv_close(handle);
    return err;
}
