#ifndef REFLEX_STORAGE_H
#define REFLEX_STORAGE_H

/**
 * @file reflex_storage.h
 * @brief NVS bring-up for the "reflex" and "goose" namespaces.
 *
 * Initializes the underlying flash partition (calling
 * nvs_flash_erase + nvs_flash_init on the first-use path) so that
 * reflex_config_* and goose_atmosphere_set_key can open their
 * blobs immediately after boot.
 */

#include <stdbool.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Open the NVS flash partition. Idempotent. */
esp_err_t reflex_storage_init(void);

/** @brief True if NVS has been initialized this boot. */
bool reflex_storage_is_ready(void);

#ifdef __cplusplus
}
#endif

#endif
