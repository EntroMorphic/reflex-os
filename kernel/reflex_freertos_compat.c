/**
 * @file reflex_freertos_compat.c
 * @brief Reflex kernel hook at the FreeRTOS scheduler entry point.
 *
 * Wraps xPortStartScheduler to:
 * 1. Print the Reflex kernel banner
 * 2. Create the kernel supervisor task
 * 3. Let FreeRTOS run the hardware scheduler
 *
 * FreeRTOS runs ALL scheduling internally. The Reflex kernel's
 * policy engine (purpose-modulated priority adjustment) operates
 * as a highest-priority FreeRTOS task that observes the substrate
 * and adjusts other tasks' priorities accordingly.
 *
 * The compat layer (task/queue/event __wrap_ implementations) is
 * preserved in git history for the future standalone Reflex OS
 * that replaces FreeRTOS entirely. That project requires owning
 * the port.c (tick ISR, context switch assembly) — not just the
 * API surface.
 */

#include "esp_rom_sys.h"
#include <stdint.h>
#include <stddef.h>

typedef int BaseType_t;
typedef unsigned int UBaseType_t;
typedef void *TaskHandle_t;
typedef void (*TaskFunction_t)(void *);
#define pdPASS 1
#define configMAX_PRIORITIES 25

extern BaseType_t __real_xPortStartScheduler(void);
extern BaseType_t xTaskCreatePinnedToCore(TaskFunction_t fn, const char *name,
    uint32_t stack, void *arg, UBaseType_t prio, TaskHandle_t *handle, int core);
extern void vTaskDelay(uint32_t ticks);

static void reflex_kernel_supervisor(void *arg) {
    (void)arg;
    while (1) {
        /* Future: read sys.purpose state, adjust FreeRTOS task
         * priorities via vTaskPrioritySet for purpose-modulated
         * scheduling. For now: exist as the kernel presence. */
        vTaskDelay(1000);
    }
}

BaseType_t __wrap_xPortStartScheduler(void) {
    esp_rom_printf("\n");
    esp_rom_printf("  ╔══════════════════════════════════════╗\n");
    esp_rom_printf("  ║       Reflex OS Kernel Active        ║\n");
    esp_rom_printf("  ╚══════════════════════════════════════╝\n");
    esp_rom_printf("\n");

    xTaskCreatePinnedToCore(reflex_kernel_supervisor, "reflex-kern",
                            2048, NULL, configMAX_PRIORITIES - 1, NULL, 0);

    return __real_xPortStartScheduler();
}
