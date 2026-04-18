/**
 * @file reflex_freertos_shim.c
 * @brief Reflex kernel hook at the FreeRTOS scheduler entry point.
 *
 * Wraps xPortStartScheduler to insert the Reflex kernel banner.
 * FreeRTOS runs unmodified as the scheduling HAL — it owns context
 * switching, tick interrupts, and hardware scheduling.
 *
 * CURRENT STATE: The hook is installed. FreeRTOS runs. The Reflex
 * kernel does not yet implement a scheduling policy engine. The
 * next step is a supervisor-level policy that calls vTaskPrioritySet
 * to adjust FreeRTOS task priorities based on the substrate's
 * purpose state and plasticity preferences.
 *
 * The standalone kernel code (reflex_sched.c, trap vectors, context
 * frames) remains in kernel/ as infrastructure for a future
 * bare-metal Reflex OS that doesn't use ESP-IDF or FreeRTOS.
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
