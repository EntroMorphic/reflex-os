#include "reflex_hal.h"
#include "reflex_task.h"
#include <string.h>
#include "reflex_led_service.h"





#include "reflex_led.h"
#include "reflex_service.h"
#include "reflex_event.h"
#include "reflex_fabric.h"
#include "goose.h"

static goose_route_t led_route;
static goose_field_t led_agency_field;

static void reflex_led_task(void *arg)
{
    // Retrieve the intent cell from the global fabric
    goose_cell_t *led_intent = goose_fabric_get_cell("led_intent");
    
    // Allocate a coordinate for the physical LED (Field 1, Region 1, Cell 15)
    reflex_tryte9_t phys_coord = goose_make_coord(1, 1, 15);
    goose_cell_t *led_phys = goose_fabric_alloc_cell("led_phys", phys_coord, true);

    if (!led_intent || !led_phys) {
        REFLEX_LOGE("LED_SERVICE", "Failed to manifest LED in Fabric!");
        reflex_task_delete(NULL);
        return;
    }

    goose_fabric_set_agency(led_phys, REFLEX_LED_PIN, GOOSE_CELL_HARDWARE_OUT);

    // Manifest the Route
    memset(&led_route, 0, sizeof(goose_route_t));
    snprintf(led_route.name, 16, "led_path");
    led_route.source_coord = led_intent->coord;
    led_route.sink_coord = phys_coord;
    led_route.orientation = REFLEX_TRIT_POS;
    led_route.coupling = GOOSE_COUPLING_SOFTWARE;

    // Manifest the Field
    memset(&led_agency_field, 0, sizeof(goose_field_t));
    snprintf(led_agency_field.name, 16, "led_agency");
    led_agency_field.routes = &led_route;
    led_agency_field.route_count = 1;
    led_agency_field.rhythm = GOOSE_RHYTHM_REACTIVE;

    // Register field for global regulation (10Hz)
    goose_supervisor_register_field(&led_agency_field);

    // Start regional high-speed pulse (100Hz)
    goose_field_start_pulse(&led_agency_field);

    reflex_task_delete(NULL);
}

static reflex_err_t reflex_led_service_init(void *ctx)
{
    (void)ctx;
    return reflex_led_init();
}

static reflex_err_t reflex_led_service_start(void *ctx)
{
    (void)ctx;
    if (reflex_task_create(reflex_led_task, "reflex-led", 4096, NULL, 5, NULL) != REFLEX_OK) {
        return REFLEX_FAIL;
    }
    return REFLEX_OK;
}

static reflex_service_status_t reflex_led_service_status(void *ctx)
{
    (void)ctx;
    return REFLEX_SERVICE_STATUS_STARTED;
}

reflex_err_t reflex_led_service_register(void)
{
    static reflex_service_desc_t desc = {
        .name = "led",
        .init = reflex_led_service_init,
        .start = reflex_led_service_start,
        .status = reflex_led_service_status,
    };
    return reflex_service_register(&desc);
}
