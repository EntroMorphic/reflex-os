/**
 * @file reflex_freertos_compat.c
 * @brief Reflex kernel — scheduler policy engine on FreeRTOS HAL.
 *
 * The kernel supervisor runs as the highest-priority FreeRTOS task.
 * Every second it calls the registered policy function, which the
 * substrate (GOOSE) layer provides. The policy modulates task
 * priorities based on purpose, plasticity, and holon state.
 */

#include "reflex_sched.h"
#include "reflex_kernel.h"
#include "esp_rom_sys.h"
#include "esp_intr_alloc.h"
#include "freertos/portmacro.h"
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>

typedef int BaseType_t;
typedef unsigned int UBaseType_t;
typedef void *TaskHandle_t;
typedef void (*TaskFunction_t)(void *);
#define pdPASS 1
#define configMAX_PRIORITIES 25

_Static_assert(PORT_OFFSET_PX_STACK == 0x30,
    "TCB pxStack offset changed — update TCB_PX_STACK in reflex_portasm.S");
_Static_assert(PORT_OFFSET_PX_END_OF_STACK == 0x44,
    "TCB pxEndOfStack offset changed — update TCB_PX_END_OF_STACK in reflex_portasm.S");

#define SYSTIMER_BASE       0x60004000
#define SYSTIMER_CONF       (SYSTIMER_BASE + 0x00)
#define SYSTIMER_TGT1_CONF  (SYSTIMER_BASE + 0x38)
#define SYSTIMER_COMP1_LOAD (SYSTIMER_BASE + 0x54)
#define SYSTIMER_INT_ENA    (SYSTIMER_BASE + 0x64)
#define SYSTIMER_INT_CLR    (SYSTIMER_BASE + 0x6C)
#define REG32(a) (*(volatile uint32_t *)(a))
#define TICK_PERIOD (40000000 / 1000)
#define SYSTIMER_TARGET1_INTR_SOURCE 38
#define MS_TO_TICKS(ms) ((ms) / (1000 / 100))

extern BaseType_t __real_xPortStartScheduler(void);
extern BaseType_t xTaskCreatePinnedToCore(TaskFunction_t fn, const char *name,
    uint32_t stack, void *arg, UBaseType_t prio, TaskHandle_t *handle, int core);
extern void vTaskDelay(uint32_t ticks);

static volatile uint32_t s_kernel_tick = 0;
static reflex_kernel_policy_fn s_policy_fn = NULL;

void reflex_kernel_set_policy(reflex_kernel_policy_fn fn) {
    s_policy_fn = fn;
}

static void __attribute__((section(".iram1")))
reflex_tick_isr(void *arg) {
    (void)arg;
    s_kernel_tick++;
    REG32(SYSTIMER_INT_CLR) = (1 << 1);
}

static void setup_kernel_tick(void) {
    REG32(SYSTIMER_CONF) |= (1 << 0);
    REG32(SYSTIMER_TGT1_CONF) = (1 << 30) | TICK_PERIOD;
    REG32(SYSTIMER_COMP1_LOAD) = 1;
    REG32(SYSTIMER_CONF) |= (1 << 25);
    REG32(SYSTIMER_INT_CLR) = (1 << 1);
    REG32(SYSTIMER_INT_ENA) |= (1 << 1);
}

static void reflex_kernel_supervisor(void *arg) {
    (void)arg;
    vTaskDelay(MS_TO_TICKS(3000));
    printf("[reflex.kernel] supervisor: policy=%s\n",
           s_policy_fn ? "registered" : "none");
    while (1) {
        if (s_policy_fn) s_policy_fn(s_kernel_tick);
        vTaskDelay(MS_TO_TICKS(1000));
    }
}

BaseType_t __wrap_xPortStartScheduler(void) {
    esp_rom_printf("\n");
    esp_rom_printf("  ╔══════════════════════════════════════╗\n");
    esp_rom_printf("  ║       Reflex OS Kernel Active        ║\n");
    esp_rom_printf("  ╚══════════════════════════════════════╝\n");
    esp_rom_printf("\n");

    setup_kernel_tick();
    intr_handle_t isr_handle;
    esp_intr_alloc(SYSTIMER_TARGET1_INTR_SOURCE, ESP_INTR_FLAG_IRAM,
                   reflex_tick_isr, NULL, &isr_handle);

    xTaskCreatePinnedToCore(reflex_kernel_supervisor, "reflex-kern",
                            4096, NULL, configMAX_PRIORITIES - 1, NULL, 0);

    esp_rom_printf("[reflex.kernel] tick=1000Hz supervisor=active\n");

    return __real_xPortStartScheduler();
}
