#include "reflex_event.h"

#include <string.h>

#include "esp_check.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"

#include "reflex_log.h"

#define REFLEX_EVENT_MAX_SUBSCRIBERS 16
#define REFLEX_EVENT_QUEUE_LEN 10
#define REFLEX_EVENT_TASK_STACK 4096
#define REFLEX_EVENT_TASK_PRIO 10

typedef struct {
    reflex_event_type_t type;
    reflex_event_handler_t handler;
    void *ctx;
} reflex_subscriber_t;

// Internal event storage for the queue
typedef struct {
    reflex_event_type_t type;
    uint32_t timestamp_ms;
    uint8_t data[32]; // Small static buffer for event data to avoid complex allocs in MVP
    size_t data_len;
} reflex_queued_event_t;

static reflex_subscriber_t s_reflex_subscribers[REFLEX_EVENT_MAX_SUBSCRIBERS];
static size_t s_reflex_subscriber_count = 0;
static QueueHandle_t s_event_queue = NULL;
static bool s_reflex_event_bus_ready = false;

static void reflex_event_task(void *arg)
{
    reflex_queued_event_t q_ev;
    while (1) {
        if (xQueueReceive(s_event_queue, &q_ev, portMAX_DELAY) == pdPASS) {
            reflex_event_t event = {
                .type = q_ev.type,
                .timestamp_ms = q_ev.timestamp_ms,
                .data = q_ev.data,
                .data_len = q_ev.data_len
            };

            for (size_t i = 0; i < s_reflex_subscriber_count; ++i) {
                if (s_reflex_subscribers[i].type == q_ev.type) {
                    s_reflex_subscribers[i].handler(&event, s_reflex_subscribers[i].ctx);
                }
            }
        }
    }
}

esp_err_t reflex_event_bus_init(void)
{
    memset(s_reflex_subscribers, 0, sizeof(s_reflex_subscribers));
    s_reflex_subscriber_count = 0;
    
    s_event_queue = xQueueCreate(REFLEX_EVENT_QUEUE_LEN, sizeof(reflex_queued_event_t));
    ESP_RETURN_ON_FALSE(s_event_queue != NULL, ESP_ERR_NO_MEM, "reflex.ev", "queue create failed");

    s_reflex_event_bus_ready = true;
    REFLEX_LOGI(REFLEX_BOOT_TAG, "event_bus=ready (async)");
    return ESP_OK;
}

esp_err_t reflex_event_bus_start(void)
{
    if (xTaskCreate(reflex_event_task, "reflex-ev", REFLEX_EVENT_TASK_STACK, NULL, REFLEX_EVENT_TASK_PRIO, NULL) != pdPASS) {
        return ESP_FAIL;
    }
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

    reflex_queued_event_t q_ev = {
        .type = type,
        .timestamp_ms = (uint32_t)(esp_timer_get_time() / 1000),
        .data_len = data_len > sizeof(q_ev.data) ? sizeof(q_ev.data) : data_len
    };

    if (data != NULL && data_len > 0) {
        memcpy(q_ev.data, data, q_ev.data_len);
    }

    if (xQueueSend(s_event_queue, &q_ev, 0) != pdPASS) {
        REFLEX_LOGW("reflex.ev", "queue full, event type=%d dropped", (int)type);
        return ESP_ERR_TIMEOUT;
    }

    return ESP_OK;
}
