/**
 * @file reflex_freertos_compat.c
 * @brief Reflex kernel — scheduler policy engine.
 *
 * The kernel owns interrupt context switching via --wrap on
 * rtos_int_enter/rtos_int_exit (see reflex_portasm.S). The
 * supervisor task runs at highest priority and calls a policy
 * function registered by the substrate (GOOSE) layer at 1Hz.
 * The policy modulates task priorities based on purpose,
 * Hebbian plasticity, and holon lifecycle state.
 *
 * FreeRTOS remains as the scheduling HAL behind reflex_task.h.
 * A complete standalone backend exists in reflex_task_kernel.c.
 */

#include "reflex_sched.h"
#include "reflex_kernel.h"
#include "reflex_hal.h"
#include "esp_rom_sys.h"
#include "freertos/portmacro.h"
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>

typedef int BaseType_t;
typedef unsigned int UBaseType_t;
typedef void *TaskHandle_t;
typedef void (*TaskFunction_t)(void *);
#define pdPASS 1
/* FreeRTOSConfig.h (reached via portmacro.h above) already defines this as 25.
 * Defer to it rather than shadow it — redefining produced a warning, and if the
 * two ever diverged this file would compute a priority ceiling the scheduler
 * does not actually enforce. The fallback keeps the file self-contained if it
 * is ever compiled without the FreeRTOS headers. */
#ifndef configMAX_PRIORITIES
#define configMAX_PRIORITIES 25
#endif

/* Validate FreeRTOS TCB layout for reflex_portasm.S stack guard offsets.
 * These become obsolete when the Reflex scheduler is the production default. */
_Static_assert(PORT_OFFSET_PX_STACK == 0x30,
    "TCB pxStack offset changed — update TCB_PX_STACK in reflex_portasm.S");
_Static_assert(PORT_OFFSET_PX_END_OF_STACK == 0x44,
    "TCB pxEndOfStack offset changed — update TCB_PX_END_OF_STACK in reflex_portasm.S");

#define MS_TO_TICKS(ms) ((ms) / (1000 / 100))
#define KERNEL_POLICY_PERIOD_MS 1000

extern BaseType_t __real_xPortStartScheduler(void);
extern BaseType_t xTaskCreatePinnedToCore(TaskFunction_t fn, const char *name,
    uint32_t stack, void *arg, UBaseType_t prio, TaskHandle_t *handle, int core);
extern void vTaskDelay(uint32_t ticks);

static volatile uint32_t s_kernel_tick = 0;
static reflex_kernel_policy_fn s_policy_fn = NULL;

void reflex_kernel_set_policy(reflex_kernel_policy_fn fn) {
    s_policy_fn = fn;
}

/* There was a hardware tick here — a SYSTIMER TARGET1 alarm and an ISR that
 * incremented s_kernel_tick — and it is gone deliberately.
 *
 * It never worked. SYSTIMER_BASE was hardcoded to 0x60004000, which is I2C0 on
 * this chip, so setup_kernel_tick() spent its life writing alarm configuration
 * into I2C registers and the ISR never fired. Correcting that base (7789b79)
 * pointed the same writes at the real systimer for the first time — and they
 * are wrong there in a way that matters: the code sets bits 0 and 25 of
 * SYSTIMER_CONF, but TARGET1_WORK_EN is bit 23. SYSTIMER_CONF also carries the
 * enable bits for alarm 0, which ESP-IDF uses as the FreeRTOS OS tick
 * (SYSTIMER_ALARM_OS_TICK_CORE0 == 0). Read-modify-writing unknown bits into
 * the register that gates the OS tick, from code that had never executed, is
 * not a risk worth carrying.
 *
 * And it bought nothing. s_kernel_tick is passed to the policy function, which
 * discards it — goose_kernel_policy_tick opens with `(void)tick`. The policy
 * cadence comes from the vTaskDelay below, never from the alarm.
 *
 * So the counter is now advanced by the supervisor loop that actually uses it.
 * Same observable behaviour, no shared-peripheral risk. If a real hardware tick
 * is wanted later, take the alarm index and the bit positions from
 * soc/systimer_reg.h rather than literals, and pick an alarm IDF is not using. */

static void reflex_kernel_supervisor(void *arg) {
    (void)arg;
    vTaskDelay(MS_TO_TICKS(3000));
    printf("[reflex.kernel] supervisor: policy=%s\n",
           s_policy_fn ? "registered" : "none");
    while (1) {
        s_kernel_tick++;
        if (s_policy_fn) s_policy_fn(s_kernel_tick);
        vTaskDelay(MS_TO_TICKS(KERNEL_POLICY_PERIOD_MS));
    }
}

BaseType_t __wrap_xPortStartScheduler(void) {
    esp_rom_printf("\n");
    esp_rom_printf("  ╔══════════════════════════════════════╗\n");
    esp_rom_printf("  ║       Reflex OS Kernel Active        ║\n");
    esp_rom_printf("  ╚══════════════════════════════════════╝\n");
    esp_rom_printf("\n");

    xTaskCreatePinnedToCore(reflex_kernel_supervisor, "reflex-kern",
                            4096, NULL, configMAX_PRIORITIES - 1, NULL, 0);

    /* Was "tick=1000Hz", which was never true: the alarm wrote to I2C0 and
     * fired at 0Hz. The policy cadence is what this line should report. */
    esp_rom_printf("[reflex.kernel] policy=%dms supervisor=active\n",
                   KERNEL_POLICY_PERIOD_MS);

    return __real_xPortStartScheduler();
}
