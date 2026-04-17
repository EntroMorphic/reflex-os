/**
 * @file reflex_freertos_shim.c
 * @brief Reflex kernel port — replaces FreeRTOS's port.c scheduler entry.
 *
 * Wraps xPortStartScheduler to insert our kernel's banner and hook.
 * FreeRTOS's task management (TCBs, queues, timeouts) runs unmodified.
 * The scheduling POLICY is ours — we can adjust task priorities based
 * on purpose, plasticity state, and holon lifecycle.
 *
 * Architecture: FreeRTOS is the task management library. The Reflex
 * kernel is the scheduling policy layer on top.
 */

#include "esp_rom_sys.h"

typedef int BaseType_t;

/* The real xPortStartScheduler from FreeRTOS port.c */
extern BaseType_t __real_xPortStartScheduler(void);

BaseType_t __wrap_xPortStartScheduler(void) {
    esp_rom_printf("\n");
    esp_rom_printf("  ╔══════════════════════════════════════╗\n");
    esp_rom_printf("  ║       Reflex OS Kernel Active        ║\n");
    esp_rom_printf("  ║   FreeRTOS HAL • Reflex Substrate    ║\n");
    esp_rom_printf("  ╚══════════════════════════════════════╝\n");
    esp_rom_printf("\n");

    /* Let FreeRTOS's port start the actual hardware scheduler.
     * Our supervisor task (registered in app_main) manages the
     * purpose-modulated scheduling policy on top. */
    return __real_xPortStartScheduler();
}
