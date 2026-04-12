#include "reflex_service.h"

#include <string.h>

#include "esp_check.h"

#include "reflex_log.h"

#define REFLEX_SERVICE_MAX 16

static const reflex_service_desc_t *s_reflex_services[REFLEX_SERVICE_MAX];
static size_t s_reflex_service_count = 0;
static bool s_reflex_service_manager_ready = false;

esp_err_t reflex_service_manager_init(void)
{
    memset(s_reflex_services, 0, sizeof(s_reflex_services));
    s_reflex_service_count = 0;
    s_reflex_service_manager_ready = true;
    REFLEX_LOGI(REFLEX_BOOT_TAG, "service_manager=ready");
    return ESP_OK;
}

esp_err_t reflex_service_register(const reflex_service_desc_t *service)
{
    ESP_RETURN_ON_FALSE(s_reflex_service_manager_ready, ESP_ERR_INVALID_STATE, "reflex.svc", "manager not ready");
    ESP_RETURN_ON_FALSE(service != NULL, ESP_ERR_INVALID_ARG, "reflex.svc", "service desc required");
    ESP_RETURN_ON_FALSE(s_reflex_service_count < REFLEX_SERVICE_MAX, ESP_ERR_NO_MEM, "reflex.svc", "max services reached");

    s_reflex_services[s_reflex_service_count++] = service;
    
    if (service->init != NULL) {
        ESP_RETURN_ON_ERROR(service->init(service->context), "reflex.svc", "service %s init failed", service->name);
    }

    REFLEX_LOGI(REFLEX_BOOT_TAG, "service_registered=%s", service->name);
    return ESP_OK;
}

esp_err_t reflex_service_start_all(void)
{
    ESP_RETURN_ON_FALSE(s_reflex_service_manager_ready, ESP_ERR_INVALID_STATE, "reflex.svc", "manager not ready");

    for (size_t i = 0; i < s_reflex_service_count; ++i) {
        const reflex_service_desc_t *svc = s_reflex_services[i];
        if (svc->start != NULL) {
            ESP_RETURN_ON_ERROR(svc->start(svc->context), "reflex.svc", "service %s start failed", svc->name);
            REFLEX_LOGI(REFLEX_BOOT_TAG, "service_started=%s", svc->name);
        }
    }

    return ESP_OK;
}

esp_err_t reflex_service_stop_all(void)
{
    ESP_RETURN_ON_FALSE(s_reflex_service_manager_ready, ESP_ERR_INVALID_STATE, "reflex.svc", "manager not ready");

    for (int i = (int)s_reflex_service_count - 1; i >= 0; --i) {
        const reflex_service_desc_t *svc = s_reflex_services[i];
        if (svc->stop != NULL) {
            svc->stop(svc->context);
        }
    }

    return ESP_OK;
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
