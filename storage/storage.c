#include "reflex_storage.h"

#include <stdbool.h>

#include "esp_check.h"
#include "nvs_flash.h"

#include "reflex_config.h"
#include "reflex_log.h"

static bool s_reflex_storage_ready = false;

esp_err_t reflex_storage_init(void)
{
    esp_err_t err = nvs_flash_init();

    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        REFLEX_LOGW(REFLEX_BOOT_TAG, "nvs init requires erase err=%s", esp_err_to_name(err));
        ESP_RETURN_ON_ERROR(nvs_flash_erase(), REFLEX_BOOT_TAG, "nvs erase failed");
        err = nvs_flash_init();
    }

    if (err == ESP_OK) {
        ESP_RETURN_ON_ERROR(reflex_config_init_defaults(), REFLEX_BOOT_TAG, "config defaults init failed");
        s_reflex_storage_ready = true;
        REFLEX_LOGI(REFLEX_BOOT_TAG, "storage=nvs_ready");
        return ESP_OK;
    }

    s_reflex_storage_ready = false;
    REFLEX_LOGE(REFLEX_BOOT_TAG, "storage init failed err=%s", esp_err_to_name(err));
    return err;
}

bool reflex_storage_is_ready(void)
{
    return s_reflex_storage_ready;
}
