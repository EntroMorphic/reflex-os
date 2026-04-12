#ifndef REFLEX_SERVICE_H
#define REFLEX_SERVICE_H

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    REFLEX_SERVICE_STATUS_STOPPED = 0,
    REFLEX_SERVICE_STATUS_STARTED,
    REFLEX_SERVICE_STATUS_FAULTED,
} reflex_service_status_t;

typedef struct reflex_service_desc {
    const char *name;
    esp_err_t (*init)(void *ctx);
    esp_err_t (*start)(void *ctx);
    esp_err_t (*stop)(void *ctx);
    reflex_service_status_t (*status)(void *ctx);
    void *context;
} reflex_service_desc_t;

esp_err_t reflex_service_manager_init(void);
esp_err_t reflex_service_register(const reflex_service_desc_t *service);
esp_err_t reflex_service_start_all(void);
esp_err_t reflex_service_stop_all(void);

// For shell iteration
size_t reflex_service_get_count(void);
const reflex_service_desc_t *reflex_service_get_by_index(size_t index);

#ifdef __cplusplus
}
#endif

#endif
