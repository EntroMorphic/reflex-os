/**
 * @file reflex_app_startup.c
 * @brief Replaces FreeRTOS's esp_startup_start_app via --wrap.
 *
 * Starts the Reflex kernel scheduler instead of FreeRTOS.
 * ESP-IDF's internal components still call FreeRTOS APIs, which
 * are routed to our scheduler via the shim (reflex_freertos_shim.c).
 */

#include "reflex_sched.h"
#include "esp_rom_sys.h"

extern void app_main(void);

static void main_task_wrapper(void *arg) {
    (void)arg;
    app_main();
    while (1) { reflex_sched_delay_ms(1000); }
}

void __wrap_esp_startup_start_app(void) {
    esp_rom_printf("\n[reflex.kernel] Reflex OS kernel taking over\n");

    /* Initialize the scheduler (creates idle task) */
    reflex_sched_init();

    /* Create the main task at high priority */
    reflex_sched_create_task(main_task_wrapper, "main", 8192, NULL, 10, NULL);

    esp_rom_printf("[reflex.kernel] starting scheduler — FreeRTOS is not running\n");

    /* Start the Reflex scheduler — never returns */
    reflex_sched_start();

    esp_rom_printf("[reflex.kernel] ERROR: scheduler returned\n");
    while (1) { __asm__ volatile ("wfi"); }
}
