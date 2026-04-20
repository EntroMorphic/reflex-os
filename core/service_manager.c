/** @file service_manager.c
 * @brief Service lifecycle registration and startup.
 */
#include "reflex_service.h"

#include <string.h>

#include "reflex_types.h"
#include "reflex_hal.h"
#include "reflex_task.h"

#include "reflex_log.h"

#define REFLEX_SERVICE_MAX 16

static const reflex_service_desc_t *s_reflex_services[REFLEX_SERVICE_MAX];
static size_t s_reflex_service_count = 0;
static bool s_reflex_service_manager_ready = false;

reflex_err_t reflex_service_manager_init(void)
{
    memset(s_reflex_services, 0, sizeof(s_reflex_services));
    s_reflex_service_count = 0;
    s_reflex_service_manager_ready = true;
    REFLEX_LOGI(REFLEX_BOOT_TAG, "service_manager=ready");
    return REFLEX_OK;
}

reflex_err_t reflex_service_register(const reflex_service_desc_t *service)
{
    REFLEX_RETURN_ON_FALSE(s_reflex_service_manager_ready, REFLEX_ERR_INVALID_STATE, "reflex.svc", "manager not ready");
    REFLEX_RETURN_ON_FALSE(service != NULL, REFLEX_ERR_INVALID_ARG, "reflex.svc", "service desc required");
    REFLEX_RETURN_ON_FALSE(s_reflex_service_count < REFLEX_SERVICE_MAX, REFLEX_ERR_NO_MEM, "reflex.svc", "max services reached");

    s_reflex_services[s_reflex_service_count++] = service;
    
    if (service->init != NULL) {
        REFLEX_RETURN_ON_ERROR(service->init(service->context), "reflex.svc", "service init failed");
    }

    REFLEX_LOGI(REFLEX_BOOT_TAG, "service_registered=%s", service->name);
    return REFLEX_OK;
}

reflex_err_t reflex_service_start_all(void)
{
    REFLEX_RETURN_ON_FALSE(s_reflex_service_manager_ready, REFLEX_ERR_INVALID_STATE, "reflex.svc", "manager not ready");

    for (size_t i = 0; i < s_reflex_service_count; ++i) {
        const reflex_service_desc_t *svc = s_reflex_services[i];
        if (svc->start != NULL) {
            REFLEX_RETURN_ON_ERROR(svc->start(svc->context), "reflex.svc", "service start failed");
            REFLEX_LOGI(REFLEX_BOOT_TAG, "service_started=%s", svc->name);
        }
    }

    return REFLEX_OK;
}

reflex_err_t reflex_service_stop_all(void)
{
    REFLEX_RETURN_ON_FALSE(s_reflex_service_manager_ready, REFLEX_ERR_INVALID_STATE, "reflex.svc", "manager not ready");

    for (int i = (int)s_reflex_service_count - 1; i >= 0; --i) {
        const reflex_service_desc_t *svc = s_reflex_services[i];
        if (svc->stop != NULL) {
            svc->stop(svc->context);
        }
    }

    return REFLEX_OK;
}

size_t reflex_service_get_count(void)
{
    return s_reflex_service_count;
}

const reflex_service_desc_t *reflex_service_get_by_index(size_t index)
{
    if (index >= s_reflex_service_count) {
        return NULL;
    }
    return s_reflex_services[index];
}
