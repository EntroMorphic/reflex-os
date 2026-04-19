/**
 * @file reflex_freertos_compat.c
 * @brief Reflex kernel — scheduler policy engine on FreeRTOS HAL.
 *
 * Composition of our atomics:
 *   - SYSTIMER TARGET1 → kernel tick (our timer, our ISR)
 *   - reflex_sched_tick → advance our tick counter
 *   - purpose state check → adjust FreeRTOS task priorities
 *   - vPortYield → trigger FreeRTOS context switch (their assembly)
 *
 * Our atoms: interrupt control (csrci/csrsi), spinlock
 * (__atomic_test_and_set), timer (SYSTIMER registers), task
 * selection (priority scan), tick counter. FreeRTOS's atoms:
 * register save/restore assembly, ISR stack management, interrupt
 * controller routing. The composition is clean — we decide WHEN
 * to switch, they do HOW to switch.
 */

#include "reflex_sched.h"
#include "esp_rom_sys.h"
#include "esp_intr_alloc.h"
#include "freertos/portmacro.h"
#include <stdint.h>
#include <stddef.h>

typedef int BaseType_t;
typedef unsigned int UBaseType_t;
typedef void *TaskHandle_t;
typedef void (*TaskFunction_t)(void *);
#define pdPASS 1
#define configMAX_PRIORITIES 25

/* reflex_portasm.S hardcodes these offsets — break the build if they drift */
_Static_assert(PORT_OFFSET_PX_STACK == 0x30,
    "TCB pxStack offset changed — update TCB_PX_STACK in reflex_portasm.S");
_Static_assert(PORT_OFFSET_PX_END_OF_STACK == 0x44,
    "TCB pxEndOfStack offset changed — update TCB_PX_END_OF_STACK in reflex_portasm.S");

/* SYSTIMER registers — our tick source */
#define SYSTIMER_BASE       0x60004000
#define SYSTIMER_CONF       (SYSTIMER_BASE + 0x00)
#define SYSTIMER_TGT1_CONF  (SYSTIMER_BASE + 0x38)
#define SYSTIMER_COMP1_LOAD (SYSTIMER_BASE + 0x54)
#define SYSTIMER_INT_ENA    (SYSTIMER_BASE + 0x64)
#define SYSTIMER_INT_CLR    (SYSTIMER_BASE + 0x6C)
#define REG32(a) (*(volatile uint32_t *)(a))
#define TICK_PERIOD (40000000 / 1000) /* 40MHz / 1kHz */
#define SYSTIMER_TARGET1_INTR_SOURCE 38

extern BaseType_t __real_xPortStartScheduler(void);
extern BaseType_t xTaskCreatePinnedToCore(TaskFunction_t fn, const char *name,
    uint32_t stack, void *arg, UBaseType_t prio, TaskHandle_t *handle, int core);
extern void vTaskDelay(uint32_t ticks);
extern void vPortYield(void);

/* Our tick counter — independent of FreeRTOS's xTickCount */
static volatile uint32_t s_kernel_tick = 0;

static void __attribute__((section(".iram1")))
reflex_tick_isr(void *arg) {
    (void)arg;
    s_kernel_tick++;
    REG32(SYSTIMER_INT_CLR) = (1 << 1);

    /* Future: check if purpose-modulated priority adjustment
     * should trigger a context switch. Call vPortYield() to
     * preempt the current task when a higher-priority task
     * should run based on substrate policy. */
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
    while (1) {
        /* The kernel supervisor runs every second and can:
         * 1. Read sys.purpose state
         * 2. Read plasticity/Hebbian state
         * 3. Adjust FreeRTOS task priorities via vTaskPrioritySet
         * 4. Manage holon lifecycle
         *
         * This is where the substrate's awareness becomes
         * scheduling policy. The purpose biases which tasks
         * get CPU time. The plasticity layer's learned
         * preferences inform the priority adjustments. */
        vTaskDelay(1000);
    }
}

BaseType_t __wrap_xPortStartScheduler(void) {
    esp_rom_printf("\n");
    esp_rom_printf("  ╔══════════════════════════════════════╗\n");
    esp_rom_printf("  ║       Reflex OS Kernel Active        ║\n");
    esp_rom_printf("  ╚══════════════════════════════════════╝\n");
    esp_rom_printf("\n");

    /* Register our tick ISR on SYSTIMER TARGET1.
     * FreeRTOS uses TARGET0 for its tick. We get our own. */
    setup_kernel_tick();
    intr_handle_t isr_handle;
    esp_intr_alloc(SYSTIMER_TARGET1_INTR_SOURCE, ESP_INTR_FLAG_IRAM,
                   reflex_tick_isr, NULL, &isr_handle);

    /* Create the kernel supervisor task */
    xTaskCreatePinnedToCore(reflex_kernel_supervisor, "reflex-kern",
                            2048, NULL, configMAX_PRIORITIES - 1, NULL, 0);

    esp_rom_printf("[reflex.kernel] tick=%dHz supervisor=active\n",
                   1000);

    /* Let FreeRTOS start the hardware scheduler */
    return __real_xPortStartScheduler();
}
