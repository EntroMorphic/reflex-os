#include "reflex_event.h"

#include <string.h>

#include "esp_check.h"
#include "esp_timer.h"

#include "reflex_log.h"

#define REFLEX_EVENT_MAX_SUBSCRIBERS 16

typedef struct {
    reflex_event_type_t type;
    reflex_event_handler_t handler;
    void *ctx;
} reflex_subscriber_t;

static reflex_subscriber_t s_reflex_subscribers[REFLEX_EVENT_MAX_SUBSCRIBERS];
static size_t s_reflex_subscriber_count = 0;
static bool s_reflex_event_bus_ready = false;

esp_err_t reflex_event_bus_init(void)
{
    memset(s_reflex_subscribers, 0, sizeof(s_reflex_subscribers));
    s_reflex_subscriber_count = 0;
    s_reflex_event_bus_ready = true;
    REFLEX_LOGI(REFLEX_BOOT_TAG, "event_bus=ready");
    return ESP_OK;
}

esp_err_t reflex_event_subscribe(reflex_event_type_t type, reflex_event_handler_t handler, void *ctx)
{
    ESP_RETURN_ON_FALSE(s_reflex_event_bus_ready, ESP_ERR_INVALID_STATE, "reflex.ev", "bus not ready");
    ESP_RETURN_ON_FALSE(handler != NULL, ESP_ERR_INVALID_ARG, "reflex.ev", "handler required");
    ESP_RETURN_ON_FALSE(s_reflex_subscriber_count < REFLEX_EVENT_MAX_SUBSCRIBERS, ESP_ERR_NO_MEM, "reflex.ev", "max subscribers reached");

    s_reflex_subscribers[s_reflex_subscriber_count].type = type;
    s_reflex_subscribers[s_reflex_subscriber_count].handler = handler;
    s_reflex_subscribers[s_reflex_subscriber_count].ctx = ctx;
    s_reflex_subscriber_count++;

    return ESP_OK;
}

esp_err_t reflex_event_publish(reflex_event_type_t type, void *data, size_t data_len)
{
    ESP_RETURN_ON_FALSE(s_reflex_event_bus_ready, ESP_ERR_INVALID_STATE, "reflex.ev", "bus not ready");

    reflex_event_t event = {
        .type = type,
        .timestamp_ms = (uint32_t)(esp_timer_get_time() / 1000),
        .data = data,
        .data_len = data_len,
    };

    for (size_t i = 0; i < s_reflex_subscriber_count; ++i) {
        if (s_reflex_subscribers[i].type == type) {
            s_reflex_subscribers[i].handler(&event, s_reflex_subscribers[i].ctx);
        }
    }

    return ESP_OK;
}
