#include "reflex_led_service.h"

#include "esp_check.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "reflex_led.h"
#include "reflex_service.h"
#include "reflex_event.h"
#include "reflex_fabric.h"
#include "goose.h"

static goose_cell_t *led_intent = NULL;
static goose_cell_t led_physical = { 
    .name = "physical", 
    .state = REFLEX_TRIT_ZERO, 
    .type = GOOSE_CELL_HARDWARE_OUT, 
    .hardware_addr = REFLEX_LED_PIN 
};

static goose_route_t led_route = {
    .name = "agency_path",
    .source = NULL, // Will be linked to led_intent
    .sink = &led_physical,
    .orientation = REFLEX_TRIT_POS,
    .coupling = GOOSE_COUPLING_SOFTWARE
};

static goose_field_t led_agency_field = {
    .name = "led_agency",
    .routes = &led_route,
    .route_count = 1
};

static void reflex_led_task(void *arg)
{
    // Retrieve the intent cell from the global fabric
    led_intent = goose_fabric_get_cell("led_intent");
    led_route.source = led_intent;

    if (!led_intent) {
        ESP_LOGE("LED_SERVICE", "Failed to find led_intent cell in fabric!");
        vTaskDelete(NULL);
        return;
    }

    // Register field for global regulation
    goose_supervisor_register_field(&led_agency_field);

    reflex_trit_t last_state = led_intent->state;

    while (1) {
        // Tapestry Processing: Observe the fabric cell
        if (led_intent->state != last_state) {
            ESP_LOGD("LED_SERVICE", "Tapestry transition detected: %d -> %d", last_state, led_intent->state);
            
            // Process the agency field to propagate intent to hardware
            goose_process_transitions(&led_agency_field);
            last_state = led_intent->state;
        }
        
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}

static esp_err_t reflex_led_service_init(void *ctx)
{
    (void)ctx;
    // We still call reflex_led_init to ensure the GPIO is configured
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
