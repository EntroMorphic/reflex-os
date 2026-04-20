/** @file main.c
 * @brief Application entry point.
 */
#include "reflex_hal.h"
#include "reflex_task.h"
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "reflex_types.h"



#include "reflex_boot.h"
#include "reflex_log.h"
#include "reflex_storage.h"
#include "reflex_config.h"
#include "reflex_event.h"
#include "reflex_fabric.h"
#include "reflex_service.h"
#include "reflex_led_service.h"
#include "reflex_button_service.h"
#include "reflex_temp_service.h"
#if !CONFIG_REFLEX_RADIO_802154
#include "reflex_wifi.h"
#endif
#include "reflex_shell.h"
#include "reflex_cache.h"
#include "reflex_ternary.h"
#include "reflex_vm.h"
#include "reflex_vm_task.h"
#include "reflex_vm_loader.h"
#include "goose.h"

#define REFLEX_BOOT_LOOP_THRESHOLD 10
#define REFLEX_STABILITY_MS 10000

static void goose_supervisor_task(void *arg)
{
    while (1) {
        goose_supervisor_pulse();
        reflex_task_delay_ms(100); // 10Hz Pulse
    }
}

static void reflex_stability_task(void *arg)
{
    reflex_task_delay_ms(REFLEX_STABILITY_MS);
    reflex_config_set_boot_count(0);
    REFLEX_LOGI(REFLEX_BOOT_TAG, "system_stable=confirmed");
    reflex_task_delete(NULL);
}

static void manifest_demo_arc(void)
{
    /**
     * Phase 15: Atmospheric Arcing (Self-Arc Loopback Demo)
     */
    if (goose_atmosphere_init() == REFLEX_OK) {
        static goose_route_t arc_out_route;
        static goose_route_t arc_in_route;
        static goose_field_t atmosphere_field;
        
        goose_cell_t *local_intent = goose_fabric_get_cell("agency.led.intent");
        reflex_tryte9_t ghost_coord = goose_make_coord(-1, 0, 1);
        goose_cell_t *ghost_cell = goose_fabric_alloc_cell("ghost_arc", ghost_coord, true);

        if (local_intent && ghost_cell) {
            snprintf(arc_out_route.name, 16, "arc_out");
            arc_out_route.source_coord = local_intent->coord;
            arc_out_route.sink_coord = ghost_coord;
            arc_out_route.orientation = REFLEX_TRIT_POS;
            arc_out_route.coupling = GOOSE_COUPLING_RADIO;

            snprintf(arc_in_route.name, 16, "arc_in");
            arc_in_route.source_coord = ghost_coord;
            // In a real manifest, led_0 is woven by the Atlas
            goose_cell_t *led_0 = goose_fabric_get_cell_by_coord(goose_make_coord(1, 1, 0)); 
            if (led_0) {
                arc_in_route.sink_coord = led_0->coord;
                arc_in_route.orientation = REFLEX_TRIT_POS;
                arc_in_route.coupling = GOOSE_COUPLING_SOFTWARE;

                static goose_route_t demo_routes[2];
                demo_routes[0] = arc_out_route;
                demo_routes[1] = arc_in_route;

                memset(&atmosphere_field, 0, sizeof(goose_field_t));
                snprintf(atmosphere_field.name, 16, "atmosphere");
                atmosphere_field.routes = demo_routes;
                atmosphere_field.route_count = 2;
                atmosphere_field.rhythm = GOOSE_RHYTHM_DISTRIBUTED;

                goose_field_start_pulse(&atmosphere_field);
                REFLEX_LOGI(REFLEX_BOOT_TAG, "Geometric Arcing active (Self-Arc Loopback)");
            }
        }
    }
}

void app_main(void)
{
    int32_t boot_count = 0;
    bool safe_mode = false;
    static reflex_vm_task_runtime_t system_vm;
    static reflex_cache_t system_cache;

    reflex_boot_print_banner();
    
    // 1. Init Storage
    if (reflex_storage_init() != REFLEX_OK) {
        REFLEX_LOGE(REFLEX_BOOT_TAG, "storage init failed");
        reflex_shell_run();
        return;
    }

    // 2. Lifecycle Management
    reflex_config_get_safe_mode(&safe_mode);
    reflex_config_get_boot_count(&boot_count);
    
    if (boot_count >= REFLEX_BOOT_LOOP_THRESHOLD) safe_mode = true;

    if (safe_mode) {
        REFLEX_LOGW(REFLEX_BOOT_TAG, "safe mode detected, resetting counters");
        boot_count = 0;
        reflex_config_set_boot_count(0);
        reflex_config_set_safe_mode(false);
    }

    reflex_config_set_boot_count(boot_count + 1);
    
    // 3. Substrate Startup
    REFLEX_LOGI(REFLEX_BOOT_TAG, "starting substrate (boot_count=%ld)", (long)boot_count);
    if (reflex_event_bus_init() != REFLEX_OK || reflex_event_bus_start() != REFLEX_OK) {
        reflex_shell_run(); return;
    }

    if (reflex_fabric_init() != REFLEX_OK || goose_fabric_init() != REFLEX_OK) {
        reflex_shell_run(); return;
    }

    // 4. GOOSE Loom Manifestation
    goose_supervisor_init();
    goose_gateway_init();
    goose_atlas_manifest_weave();
    goose_lp_heartbeat_init();  // LP RISC-V Coherent Heartbeat

    // 6. Background Regulation
    reflex_task_create(goose_supervisor_task, "goose-super", 4096, NULL, 20, NULL);
    
    // 7. Binary Services
    reflex_service_manager_init();
    reflex_led_service_register();
    reflex_button_service_register();
    reflex_temp_service_register();
#if !CONFIG_REFLEX_RADIO_802154
    reflex_wifi_service_register();
#endif
    
    // 8. Ternary VM
    reflex_cache_init(&system_cache);
    reflex_vm_task_runtime_init(&system_vm);
    system_vm.vm.cache = (struct reflex_cache*)&system_cache;
    reflex_vm_task_register_service(&system_vm, "system-vm");
    
    if (reflex_service_start_all() != REFLEX_OK) {
        reflex_shell_run(); return;
    }

    // 8.5 Atmospheric arcing
#if CONFIG_REFLEX_RADIO_802154
    if (goose_atmosphere_init() == REFLEX_OK) {
        REFLEX_LOGI(REFLEX_BOOT_TAG, "atmospheric mesh: 802.15.4 (blob-free)");
    }
#else
    manifest_demo_arc();
#endif

    // 9. Stability & Shell
    reflex_task_create(reflex_stability_task, "reflex-stable", 2048, NULL, 5, NULL);
    reflex_event_publish(REFLEX_EVENT_BOOT_COMPLETE, NULL, 0);

    // Self-checks
    reflex_ternary_self_check();
    reflex_vm_self_check();
    reflex_vm_loader_self_check();
    reflex_vm_task_self_check();

    reflex_shell_run();
}
