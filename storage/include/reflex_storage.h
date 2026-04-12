#ifndef REFLEX_STORAGE_H
#define REFLEX_STORAGE_H

#include <stdbool.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t reflex_storage_init(void);
bool reflex_storage_is_ready(void);

#ifdef __cplusplus
}
#endif

#endif
