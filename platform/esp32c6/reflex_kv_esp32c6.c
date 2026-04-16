/**
 * @file reflex_kv_esp32c6.c
 * @brief Reflex KV — NVS backend.
 */

#include "reflex_kv.h"
#include "nvs.h"
#include "nvs_flash.h"

reflex_err_t reflex_kv_init(void) {
    esp_err_t rc = nvs_flash_init();
    if (rc == ESP_ERR_NVS_NO_FREE_PAGES || rc == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        nvs_flash_erase();
        rc = nvs_flash_init();
    }
    return (reflex_err_t)rc;
}

reflex_err_t reflex_kv_open(const char *ns, bool readonly,
                            reflex_kv_handle_t *out) {
    nvs_handle_t h;
    esp_err_t rc = nvs_open(ns, readonly ? NVS_READONLY : NVS_READWRITE, &h);
    if (rc != ESP_OK) return (reflex_err_t)rc;
    *out = (reflex_kv_handle_t)(uintptr_t)h;
    return REFLEX_OK;
}

void reflex_kv_close(reflex_kv_handle_t h) {
    nvs_close((nvs_handle_t)(uintptr_t)h);
}

reflex_err_t reflex_kv_get_str(reflex_kv_handle_t h, const char *key,
                               char *buf, size_t *len) {
    return (reflex_err_t)nvs_get_str((nvs_handle_t)(uintptr_t)h, key, buf, len);
}

reflex_err_t reflex_kv_set_str(reflex_kv_handle_t h, const char *key,
                               const char *val) {
    return (reflex_err_t)nvs_set_str((nvs_handle_t)(uintptr_t)h, key, val);
}

reflex_err_t reflex_kv_get_blob(reflex_kv_handle_t h, const char *key,
                                void *buf, size_t *len) {
    return (reflex_err_t)nvs_get_blob((nvs_handle_t)(uintptr_t)h, key, buf, len);
}

reflex_err_t reflex_kv_set_blob(reflex_kv_handle_t h, const char *key,
                                const void *buf, size_t len) {
    return (reflex_err_t)nvs_set_blob((nvs_handle_t)(uintptr_t)h, key, buf, len);
}

reflex_err_t reflex_kv_erase(reflex_kv_handle_t h, const char *key) {
    return (reflex_err_t)nvs_erase_key((nvs_handle_t)(uintptr_t)h, key);
}

reflex_err_t reflex_kv_commit(reflex_kv_handle_t h) {
    return (reflex_err_t)nvs_commit((nvs_handle_t)(uintptr_t)h);
}
