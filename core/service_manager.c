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

static uint32_t s_restart_backoff_ms[REFLEX_SERVICE_MAX];
static uint32_t s_restart_count[REFLEX_SERVICE_MAX];
static uint64_t s_next_restart_us[REFLEX_SERVICE_MAX];

#define WATCHDOG_BACKOFF_INIT_MS  1000
#define WATCHDOG_BACKOFF_MAX_MS  30000

void reflex_service_watchdog_tick(void) {
    if (!s_reflex_service_manager_ready) return;
    uint64_t now = reflex_hal_time_us();

    for (size_t i = 0; i < s_reflex_service_count; i++) {
        const reflex_service_desc_t *svc = s_reflex_services[i];
        if (!svc->status || !svc->start) continue;

        reflex_service_status_t st = svc->status(svc->context);
        if (st != REFLEX_SERVICE_STATUS_FAULTED) {
            if (s_restart_count[i] > 0 && st == REFLEX_SERVICE_STATUS_STARTED) {
                s_restart_count[i] = 0;
                s_restart_backoff_ms[i] = 0;
            }
            continue;
        }

        if (s_next_restart_us[i] > now) continue;

        if (s_restart_backoff_ms[i] == 0)
            s_restart_backoff_ms[i] = WATCHDOG_BACKOFF_INIT_MS;
        else if (s_restart_backoff_ms[i] < WATCHDOG_BACKOFF_MAX_MS)
            s_restart_backoff_ms[i] *= 2;

        REFLEX_LOGW("reflex.svc", "restarting %s (attempt %lu, backoff %lums)",
                     svc->name, (unsigned long)(s_restart_count[i] + 1),
                     (unsigned long)s_restart_backoff_ms[i]);

        if (svc->stop) svc->stop(svc->context);
        reflex_err_t rc = svc->start(svc->context);
        s_restart_count[i]++;
        s_next_restart_us[i] = now + (uint64_t)s_restart_backoff_ms[i] * 1000;

        if (rc != REFLEX_OK) {
            REFLEX_LOGW("reflex.svc", "%s restart failed rc=0x%x", svc->name, rc);
        }
    }
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
