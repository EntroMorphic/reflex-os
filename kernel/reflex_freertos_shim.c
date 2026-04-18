/**
 * @file reflex_freertos_shim.c
 * @brief Reflex kernel — wraps xPortStartScheduler.
 *
 * Hooks the scheduler entry point to:
 * 1. Print the Reflex kernel banner
 * 2. Register a high-priority kernel supervisor task that will
 *    implement purpose-modulated scheduling policy
 * 3. Let FreeRTOS run the hardware scheduler
 *
 * FreeRTOS is the scheduling HAL. The kernel supervisor task
 * runs at the highest priority and adjusts other tasks' priorities
 * based on the substrate's purpose and plasticity state.
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
                                          uint32_t stack, void *arg,
                                          UBaseType_t prio, TaskHandle_t *handle,
                                          int core);

/* The kernel supervisor task — runs at highest priority, yields
 * after each check so it doesn't starve other tasks. This is where
 * purpose-modulated scheduling policy will be implemented. */
static void reflex_kernel_supervisor(void *arg) {
    (void)arg;
    /* The supervisor runs inside the Reflex OS substrate — it's
     * goose_supervisor_pulse. This task is a placeholder for the
     * kernel-level policy engine that will adjust FreeRTOS task
     * priorities based on purpose, plasticity, and holon state.
     *
     * For now: just exist. The hook is in place. */
    while (1) {
        /* Future: read purpose state, adjust task priorities
         * via vTaskPrioritySet based on substrate policy. */
        extern void vTaskDelay(uint32_t ticks);
        vTaskDelay(1000);
    }
}

BaseType_t __wrap_xPortStartScheduler(void) {
    esp_rom_printf("\n");
    esp_rom_printf("  ╔══════════════════════════════════════╗\n");
    esp_rom_printf("  ║       Reflex OS Kernel Active        ║\n");
    esp_rom_printf("  ╚══════════════════════════════════════╝\n");
    esp_rom_printf("\n");

    /* Create the kernel supervisor task at highest priority */
    xTaskCreatePinnedToCore(reflex_kernel_supervisor, "reflex-kern",
                            2048, NULL, configMAX_PRIORITIES - 1, NULL, 0);

    /* Let FreeRTOS start the hardware scheduler */
    return __real_xPortStartScheduler();
}
