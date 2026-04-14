#include "reflex_led_service.h"

#include "esp_check.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "reflex_led.h"
#include "reflex_service.h"
#include "reflex_event.h"
#include "reflex_fabric.h"

static void reflex_led_task(void *arg)
{
    while (1) {
        reflex_message_t msg;
        if (reflex_fabric_recv(REFLEX_NODE_LED, &msg) == ESP_OK) {
            // Op 1: Toggle, Op 2: On, Op 3: Off
            if (msg.op == 1) {
                reflex_led_toggle();
            } else if (msg.op == 2) {
                reflex_led_set(true);
            } else if (msg.op == 3) {
                reflex_led_set(false);
            }
        }
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}

static esp_err_t reflex_led_service_init(void *ctx)
{
    (void)ctx;
    return reflex_led_init();
}

static esp_err_t reflex_led_service_start(void *ctx)
{
    (void)ctx;
    if (xTaskCreate(reflex_led_task, "reflex-led", 2048, NULL, 5, NULL) != pdPASS) {
        return ESP_FAIL;
    }
    return ESP_OK;
}

static reflex_service_status_t reflex_led_service_status(void *ctx)
{
    (void)ctx;
    return REFLEX_SERVICE_STATUS_STARTED;
}

esp_err_t reflex_led_service_register(void)
{
    static reflex_service_desc_t desc = {
        .name = "led",
        .init = reflex_led_service_init,
        .start = reflex_led_service_start,
        .status = reflex_led_service_status,
    };
    return reflex_service_register(&desc);
}
