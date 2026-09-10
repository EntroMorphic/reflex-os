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
#include "goose_metabolic.h"
#include "reflex_tuning.h"

/* Boot constants now live in reflex_tuning.h:
 * REFLEX_BOOT_LOOP_THRESHOLD, REFLEX_STABILITY_MS */

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
    /* Two counters, and only one of them was being cleared here. The NVS
     * boot_count above is the OS's; Boot0 keeps its own in an always-on
     * register and used to clear it itself just before jumping, which made it
     * blind to an application that started and then crashed. */
    reflex_hal_boot_mark_stable();
    REFLEX_LOGI(REFLEX_BOOT_TAG, "system_stable=confirmed");
    reflex_task_delete(NULL);
}

/* Self-arc loopback demo, ESP-NOW builds only.
 *
 * Its one call site is already inside the !CONFIG_REFLEX_RADIO_802154 arm; the
 * definition was not, so the blob-free build compiled a function nothing could
 * call and warned about it on every run. The guard belongs on both. */
#if !CONFIG_REFLEX_RADIO_802154
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
        goose_cell_t *ghost_cell = goose_fabric_ensure_cell("ghost_arc", ghost_coord, true);

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
#endif /* !CONFIG_REFLEX_RADIO_802154 */

void app_main(void)
{
    int32_t boot_count = 0;
    bool safe_mode = false;
    static reflex_vm_task_runtime_t system_vm;
    static reflex_cache_t system_cache;

    /* Disarm the low-power watchdog before anything else runs.
     *
     * It survives the reset it causes, so a board that was reset by it comes up
     * with it still armed and counting. The disarm therefore has to beat the
     * timeout, and the first attempt put it at the start of the shell — which
     * is far too late. Boot reached "[reflex.kernel] supervisor: policy=
     * registered" and the port dropped: the watchdog fired again during init,
     * every time, and the board sat in a loop no command could interrupt
     * because no command could be typed.
     *
     * Here it runs before storage, before the supervisor, before the banner has
     * finished — as close to the first instruction of the application as this
     * file gets. ESP-IDF's own startup disarms this watchdog for exactly the
     * same reason, and at the same point. */
    reflex_hal_wdt_disarm();

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
    /* These two branches abandon the entire substrate — no fabric, no loom, no
     * services, no radio, no self-checks — and used to do it in total silence,
     * dropping to a shell that looks like a normal boot unless you notice the
     * eight missing lines above it. That is how the first boot under the Reflex
     * task backend read: event bus ready, then a prompt, and nothing to say
     * which of four calls had failed or that anything had. Say which. */
    reflex_err_t rc_bus = reflex_event_bus_init();
    if (rc_bus != REFLEX_OK) {
        REFLEX_LOGE(REFLEX_BOOT_TAG, "event_bus_init failed rc=0x%x; dropping to shell", rc_bus);
        reflex_shell_run();
        return;
    }
    rc_bus = reflex_event_bus_start();
    if (rc_bus != REFLEX_OK) {
        REFLEX_LOGE(REFLEX_BOOT_TAG, "event_bus_start failed rc=0x%x; dropping to shell", rc_bus);
        reflex_shell_run();
        return;
    }

    reflex_err_t rc_fab = reflex_fabric_init();
    if (rc_fab != REFLEX_OK) {
        REFLEX_LOGE(REFLEX_BOOT_TAG, "fabric_init failed rc=0x%x; dropping to shell", rc_fab);
        reflex_shell_run();
        return;
    }
    rc_fab = goose_fabric_init();
    if (rc_fab != REFLEX_OK) {
        REFLEX_LOGE(REFLEX_BOOT_TAG, "goose_fabric_init failed rc=0x%x; dropping to shell", rc_fab);
        reflex_shell_run();
        return;
    }

    // 4. GOOSE Loom Manifestation
    goose_supervisor_init();
    goose_atlas_manifest_weave();
    goose_lp_heartbeat_init();  // LP RISC-V Coherent Heartbeat
    goose_metabolic_init();     // Vital perception + circuit breaker

    // 6. Background Regulation
    reflex_task_create(goose_supervisor_task, "goose-super", 6144, NULL, 20, NULL);
    
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
    /* register_service runs the service init hook synchronously; it preserves
     * vm.cache precisely so this order works. See reflex_vm_task_service_init. */
    reflex_vm_task_register_service(&system_vm, "system-vm");
    
    /* A degraded service set is not a reason to abandon the substrate.
     *
     * This used to drop straight to a shell and skip everything below —
     * atmospheric arcing, the stability marker, the self-checks — if any single
     * service failed to start. On the classic ESP32, which has no temperature
     * sensor, that cost the board its entire radio. reflex_service_start_all
     * now starts every service it can and names the ones it cannot, so the
     * sensible response here is to note the degradation and keep going. */
    if (reflex_service_start_all() != REFLEX_OK) {
        REFLEX_LOGW(REFLEX_BOOT_TAG, "continuing with a degraded service set");
    }

    // 8.5 Atmospheric arcing
#if CONFIG_REFLEX_RADIO_802154
    if (goose_atmosphere_init() == REFLEX_OK) {
        /* "no Wi-Fi blob", not "blob-free": measured with make blob-check,
         * this image still links 48,566 bytes of libphy.a and libbtbb.a across
         * 15 symbols. What it avoids is the 804,754-byte Wi-Fi blob. */
        REFLEX_LOGI(REFLEX_BOOT_TAG, "atmospheric mesh: 802.15.4 (no Wi-Fi blob)");
    }
#else
    manifest_demo_arc();
#endif

    // 9. Stability & Shell
    /* Progress markers across the last four steps of boot.
     *
     * Everything above this point announces itself and everything below it used
     * to be silent, so "boot stops after the radio" covered a task creation, an
     * event publish, four self-checks and the shell — five places, no way to
     * tell which. Under Reflex's own scheduler that stretch is exactly where
     * the FreeRTOS assumptions run out, so it is the stretch that most needs to
     * say where it got to. */
    reflex_task_create(reflex_stability_task, "reflex-stable", 2048, NULL, 5, NULL);
    REFLEX_LOGI(REFLEX_BOOT_TAG, "stability task created");

    reflex_event_publish(REFLEX_EVENT_BOOT_COMPLETE, NULL, 0);
    REFLEX_LOGI(REFLEX_BOOT_TAG, "boot_complete published");

    // Self-checks
    reflex_ternary_self_check();
    reflex_vm_self_check();
    reflex_vm_loader_self_check();
    reflex_vm_task_self_check();
    REFLEX_LOGI(REFLEX_BOOT_TAG, "self_checks=done");

    reflex_shell_run();
}
