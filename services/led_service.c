#include "reflex_led_service.h"

#include "esp_check.h"

#include "reflex_led.h"
#include "reflex_service.h"
#include "reflex_event.h"

static esp_err_t reflex_led_service_init(void *ctx)
{
    (void)ctx;
    return reflex_led_init();
}

static void reflex_led_event_handler(const reflex_event_t *event, void *ctx)
{
    (void)event;
    (void)ctx;
    reflex_led_toggle();
}

static esp_err_t reflex_led_service_start(void *ctx)
{
    (void)ctx;
    return reflex_event_subscribe(REFLEX_EVENT_CUSTOM, reflex_led_event_handler, NULL);
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
