/** @file storage.c
 * @brief Storage subsystem init and migration.
 */
#include "reflex_storage.h"

#include <stdbool.h>

#include "reflex_types.h"
#include "reflex_kv.h"

#include "reflex_config.h"
#include "reflex_log.h"

static bool s_reflex_storage_ready = false;

reflex_err_t reflex_storage_init(void)
{
    reflex_err_t err = reflex_kv_init();
    if (err != REFLEX_OK) {
        REFLEX_LOGE(REFLEX_BOOT_TAG, "storage init failed err=0x%x", (unsigned)err);
        return err;
    }

    REFLEX_RETURN_ON_ERROR(reflex_config_init_defaults(), REFLEX_BOOT_TAG, "config defaults init failed");
    s_reflex_storage_ready = true;
    REFLEX_LOGI(REFLEX_BOOT_TAG, "storage=ready");
    return REFLEX_OK;
}

bool reflex_storage_is_ready(void)
{
    return s_reflex_storage_ready;
}
