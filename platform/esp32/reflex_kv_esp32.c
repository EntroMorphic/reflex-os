/**
 * @file reflex_kv_esp32c6.c
 * @brief Reflex KV — NVS backend.
 */

#include "reflex_kv.h"
#include "esp_log.h"
#include "nvs.h"
#include "nvs_flash.h"

reflex_err_t reflex_kv_init(void) {
    esp_err_t rc = nvs_flash_init();
    if (rc == ESP_ERR_NVS_NO_FREE_PAGES || rc == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        /* This erases every key on the device, and it used to do so in silence.
         *
         * It is the standard ESP-IDF recovery, and as recovery it is right —
         * an NVS partition that is full or written by a newer library cannot be
         * opened any other way. What is not right is returning REFLEX_OK
         * afterwards with no record: a board that wipes its configuration on
         * every boot then looks exactly like a board that persists it. The
         * aura key auto-provisions instead of loading, so the mesh can never
         * pair; `purpose set` reports success and the setting is gone by the
         * next boot. Both were observed before this line existed, and neither
         * pointed here.
         *
         * Logged at error level because it is data loss, and it names which of
         * the two conditions caused it so the fix is a partition-table question
         * or a library-version question rather than a guess. */
        ESP_LOGE("reflex.kv",
                 "NVS unusable (%s) — erasing every stored key; "
                 "aura pairing, purpose and config will not survive this boot",
                 rc == ESP_ERR_NVS_NO_FREE_PAGES ? "no free pages" : "new version found");
        nvs_flash_erase();
        rc = nvs_flash_init();
        if (rc == ESP_OK) {
            ESP_LOGW("reflex.kv", "NVS re-initialised empty after erase");
        }
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

/* NVS. ESP-IDF permits far larger blobs than this, but 4000 is the size a
 * single NVS entry holds without spilling into the multi-page path, and it is
 * the number a caller can rely on without knowing the partition layout.
 * Reported conservatively on purpose: the cost of understating it is a
 * truncated record that says so, and of overstating it a rejected write. */
size_t reflex_kv_value_max(void) { return 4000; }

reflex_err_t reflex_kv_get_blob(reflex_kv_handle_t h, const char *key,
                                void *buf, size_t *len) {
    return (reflex_err_t)nvs_get_blob((nvs_handle_t)(uintptr_t)h, key, buf, len);
}

reflex_err_t reflex_kv_set_blob(reflex_kv_handle_t h, const char *key,
                                const void *buf, size_t len) {
    return (reflex_err_t)nvs_set_blob((nvs_handle_t)(uintptr_t)h, key, buf, len);
}

reflex_err_t reflex_kv_get_i32(reflex_kv_handle_t h, const char *key, int32_t *out) {
    return (reflex_err_t)nvs_get_i32((nvs_handle_t)(uintptr_t)h, key, out);
}

reflex_err_t reflex_kv_set_i32(reflex_kv_handle_t h, const char *key, int32_t val) {
    return (reflex_err_t)nvs_set_i32((nvs_handle_t)(uintptr_t)h, key, val);
}

reflex_err_t reflex_kv_get_u8(reflex_kv_handle_t h, const char *key, uint8_t *out) {
    return (reflex_err_t)nvs_get_u8((nvs_handle_t)(uintptr_t)h, key, out);
}

reflex_err_t reflex_kv_set_u8(reflex_kv_handle_t h, const char *key, uint8_t val) {
    return (reflex_err_t)nvs_set_u8((nvs_handle_t)(uintptr_t)h, key, val);
}

reflex_err_t reflex_kv_erase(reflex_kv_handle_t h, const char *key) {
    return (reflex_err_t)nvs_erase_key((nvs_handle_t)(uintptr_t)h, key);
}

reflex_err_t reflex_kv_commit(reflex_kv_handle_t h) {
    return (reflex_err_t)nvs_commit((nvs_handle_t)(uintptr_t)h);
}
