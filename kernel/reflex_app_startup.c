/**
 * @file reflex_app_startup.c
 * @brief Replaces FreeRTOS's esp_startup_start_app.
 *
 * This function is called by ESP-IDF's startup path AFTER all
 * hardware initialization (BSS, heap, console, flash cache) but
 * BEFORE FreeRTOS starts. By providing our own implementation,
 * we intercept the startup and run the Reflex kernel scheduler
 * instead of FreeRTOS.
 *
 * The startup path is:
 *   ROM → Boot0 → ESP-IDF cpu_start → do_core_init → do_secondary_init
 *   → esp_startup_start_app [THIS FILE] → Reflex kernel scheduler
 *
 * FreeRTOS never starts. Our scheduler owns the CPU.
 */

#include "reflex_sched.h"
#include "esp_rom_sys.h"
#include "esp_intr_alloc.h"

#define SYSTIMER_TARGET1_INTR_SOURCE 38

extern void app_main(void);

static void __attribute__((section(".iram1"))) systimer_tick_isr(void *arg) {
    (void)arg;
    reflex_sched_tick();
    reflex_sched_ack_tick();
}

static void main_task_wrapper(void *arg) {
    (void)arg;
    app_main();
    /* If app_main returns, idle forever */
    while (1) { __asm__ volatile ("wfi"); }
}

void __wrap_esp_startup_start_app(void) {
    esp_rom_printf("\n[reflex.kernel] taking over from ESP-IDF startup\n");

    /* Directly call app_main without any scheduler for now.
     * This proves the --wrap intercept works. The scheduler
     * integration comes next. */
    esp_rom_printf("[reflex.kernel] calling app_main directly\n");
    app_main();

    /* Should never reach here */
    esp_rom_printf("[reflex.kernel] ERROR: scheduler returned\n");
    while (1) { __asm__ volatile ("wfi"); }
}
