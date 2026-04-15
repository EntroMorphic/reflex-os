#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "esp_err.h"
#include "esp_log.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "reflex_boot.h"
#include "reflex_log.h"
#include "reflex_storage.h"
#include "reflex_config.h"
#include "reflex_event.h"
#include "reflex_fabric.h"
#include "reflex_service.h"
#include "reflex_led_service.h"
#include "reflex_button_service.h"
#include "reflex_wifi.h"
#include "reflex_shell.h"
#include "reflex_cache.h"
#include "reflex_ternary.h"
#include "reflex_vm.h"
#include "reflex_vm_task.h"
#include "reflex_vm_loader.h"
#include "goose.h"

#define REFLEX_BOOT_LOOP_THRESHOLD 3
#define REFLEX_STABILITY_MS 10000

static void goose_supervisor_task(void *arg)
{
    /**
     * Harmonic Supervisor Loop
     * Regulates general system posture at a Harmonic rhythm (10Hz).
     */
    while (1) {
        goose_supervisor_pulse();
        vTaskDelay(pdMS_TO_TICKS(100)); // 10Hz Pulse
    }
}

static void reflex_stability_task(void *arg)
{
    // Wait for the stability window to pass
    vTaskDelay(pdMS_TO_TICKS(REFLEX_STABILITY_MS));
    
    // If we haven't crashed yet, assume we are stable and clear the boot count
    reflex_config_set_boot_count(0);
    REFLEX_LOGI(REFLEX_BOOT_TAG, "system_stable=confirmed");
    
    vTaskDelete(NULL);
}

void app_main(void)
{
    int32_t boot_count = 0;
    bool safe_mode = false;
    static reflex_vm_task_runtime_t system_vm;
    static reflex_cache_t system_cache;

    reflex_boot_print_banner();
    
    // 1. Init Storage
    if (reflex_storage_init() != ESP_OK) {
        REFLEX_LOGE(REFLEX_BOOT_TAG, "storage init failed, entering minimal shell");
        reflex_shell_run();
        return;
    }

    // 2. Check Boot Loop / Safe Mode
    reflex_config_get_safe_mode(&safe_mode);
    reflex_config_get_boot_count(&boot_count);
    
    if (boot_count >= REFLEX_BOOT_LOOP_THRESHOLD) {
        REFLEX_LOGW(REFLEX_BOOT_TAG, "boot loop detected (%ld attempts)", (long)boot_count);
        safe_mode = true;
    }

    if (safe_mode) {
        REFLEX_LOGW(REFLEX_BOOT_TAG, "safe mode active");
        // Reset boot count and safe mode so we can try normal boot again if fixed
        reflex_config_set_boot_count(0);
        reflex_config_set_safe_mode(false);
        reflex_shell_run();
        return;
    }

    // 3. Normal Startup (Reflex OS 2.0: Manifest Weaving)
    // Increment boot count immediately
    reflex_config_set_boot_count(boot_count + 1);
    
    // Start Event Bus Task
    if (reflex_event_bus_init() != ESP_OK || reflex_event_bus_start() != ESP_OK) {
        REFLEX_LOGE(REFLEX_BOOT_TAG, "event bus init failed, entering minimal shell");
        reflex_shell_run();
        return;
    }

    if (reflex_fabric_init() != ESP_OK) {
        REFLEX_LOGE(REFLEX_BOOT_TAG, "fabric init failed, entering minimal shell");
        reflex_shell_run();
        return;
    }

    // Phase 14: GOOSE Loom Manifestation
    if (goose_fabric_init() != ESP_OK) {
        REFLEX_LOGE(REFLEX_BOOT_TAG, "goose fabric init failed");
    }

    if (goose_supervisor_init() != ESP_OK) {
        REFLEX_LOGE(REFLEX_BOOT_TAG, "goose supervisor init failed");
    }

    if (goose_gateway_init() != ESP_OK) {
        REFLEX_LOGE(REFLEX_BOOT_TAG, "goose gateway init failed");
    }

    if (goose_atlas_manifest_weave() != ESP_OK) {
        REFLEX_LOGE(REFLEX_BOOT_TAG, "goose atlas weave failed");
    }

    // Phase 15: Atmospheric Arcing (Inter-System Tapestry)
    if (goose_atmosphere_init() == ESP_OK) {
        /**
         * Demo: The Blinky Arc
         * Node A (Source): Arcs state of 'led_intent' to Remote Coordinate (-1, 0, 1)
         * Node B (Sink):   Weaves a Lawful Route from (-1, 0, 1) to its own LED.
         * For this proof-of-concept, we weave BOTH routes on this node (Self-Arc).
         */
        
        // 1. Create a Remote Field for arcing
        static goose_region_t remote_region;
        static goose_route_t arc_out_route;
        static goose_route_t arc_in_route;
        static goose_field_t atmosphere_field;
        
        goose_cell_t *local_intent = goose_fabric_get_cell("led_intent");
        
        // The Atmospheric Ghost Coordinate
        reflex_tryte9_t ghost_coord = goose_make_coord(-1, 0, 1);
        goose_cell_t *ghost_cell = goose_fabric_alloc_cell("ghost_arc", ghost_coord);

        if (local_intent && ghost_cell) {
            // ARC OUT: Local Intent -> RADIO -> Ghost
            snprintf(arc_out_route.name, 16, "arc_out");
            arc_out_route.source = local_intent;
            arc_out_route.sink = ghost_cell;
            arc_out_route.orientation = 1;
            arc_out_route.coupling = GOOSE_COUPLING_RADIO;

            // ARC IN: Ghost -> SOFTWARE -> Local LED (Physical Pin 15)
            // Note: In a real multi-node setup, this route only exists on the receiver.
            snprintf(arc_in_route.name, 16, "arc_in");
            arc_in_route.source = ghost_cell;
            arc_in_route.sink = goose_fabric_get_cell("led_0"); // Atlas-woven cell for GPIO 15
            arc_in_route.orientation = 1;
            arc_in_route.coupling = GOOSE_COUPLING_SOFTWARE;

            atmosphere_field.routes = (goose_route_t[]){arc_out_route, arc_in_route};
            atmosphere_field.route_count = 2;
            atmosphere_field.rhythm = GOOSE_RHYTHM_DISTRIBUTED;
            snprintf(atmosphere_field.name, 16, "atmosphere");

            goose_field_start_pulse(&atmosphere_field);
            REFLEX_LOGI(REFLEX_BOOT_TAG, "Geometric Arcing active (Self-Arc Loopback)");
        }
    }
    
    xTaskCreate(goose_supervisor_task, "goose-super", 4096, NULL, 20, NULL);
    
    // Signal Loom startup: sys_status = POS
    goose_cell_t *sys_status = goose_fabric_get_cell("sys_status");
    if (sys_status) sys_status->state = 1;

    if (reflex_service_manager_init() != ESP_OK) {
        REFLEX_LOGE(REFLEX_BOOT_TAG, "service manager init failed, entering minimal shell");
        reflex_shell_run();
        return;
    }
    
    if (reflex_led_service_register() != ESP_OK ||
        reflex_button_service_register() != ESP_OK ||
        reflex_wifi_service_register() != ESP_OK) {
        REFLEX_LOGE(REFLEX_BOOT_TAG, "service registration failed, entering minimal shell");
        reflex_shell_run();
        return;
    }
    
    reflex_cache_init(&system_cache);
    reflex_vm_task_runtime_init(&system_vm);
    system_vm.vm.cache = (struct reflex_cache*)&system_cache;
    reflex_vm_task_register_service(&system_vm, "system-vm");
    
    if (reflex_service_start_all() != ESP_OK) {
        REFLEX_LOGE(REFLEX_BOOT_TAG, "service startup failed, entering minimal shell");
        reflex_shell_run();
        return;
    }

    // Start stability monitor to clear boot_count after 10s
    xTaskCreate(reflex_stability_task, "reflex-stable", 2048, NULL, 5, NULL);

    reflex_event_publish(REFLEX_EVENT_BOOT_COMPLETE, NULL, 0);

    // Run self-checks (keep these for now to verify logic)
    reflex_ternary_self_check();
    reflex_vm_self_check();
    reflex_vm_loader_self_check();
    reflex_vm_task_self_check();

    reflex_shell_run();
}
