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
 *
 * reflex_task_kernel.c implements all thirteen functions of that interface,
 * but it is not a standalone backend: every one of them delegates to FreeRTOS,
 * as its own header says. The genuinely standalone scheduler is
 * reflex_sched.c, which is compiled and then discarded by the linker because
 * nothing reaches it yet — its callers (reflex_startup.c, reflex_trap.c) are
 * compiled for the same reason and are equally unreferenced.
 */

#include "reflex_sched.h"
#include "reflex_kernel.h"
#include "reflex_hal.h"
#include "reflex_rom_esp32c6.h"
#include "reflex_task.h"
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

/* The TCB-offset asserts and the freertos header they needed live in
 * reflex_freertos_tcb_assert.c now, compiled only where FreeRTOS is the task
 * backend. A guard around the include would not have removed the dependency:
 * check_independence.py reads include lines from the source of every compiled
 * file rather than running the preprocessor, and it is right not to — a guarded
 * include is still a file the build must be able to find. Splitting removes it.
 */

#define KERNEL_POLICY_PERIOD_MS 1000
/* Was configMAX_PRIORITIES - 1 (24). Stated directly so the value does not
 * depend on a FreeRTOS header the independence path does not include. */
#define REFLEX_KERNEL_SUPERVISOR_PRIO 24

/* Declared and defined unconditionally, even where FreeRTOS is never started.
 *
 * --wrap=xPortStartScheduler is on the link line for every build, so it
 * rewrites FreeRTOS's own call whether or not this configuration wants it, and
 * a wrapper that exists only behind the flag fails the link with an undefined
 * reference. reflex_app_entry.c carries the same note about
 * esp_startup_start_app for the same reason — and this was still walked into a
 * second time. Under REFLEX_OWN_ENTRY nothing ever calls it. */
extern BaseType_t __real_xPortStartScheduler(void);

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

/* Delays through reflex_task.h, not vTaskDelay.
 *
 * That one substitution is what lets this supervisor run on either backend, and
 * it is why taking Tier C to zero did not mean deleting the policy engine.
 * reflex_kernel_set_policy has a weak no-op fallback in goose_supervisor.c, so
 * dropping this file would have linked cleanly and quietly stopped modulating
 * task priorities on the one configuration the work is aimed at. */
static void reflex_kernel_supervisor(void *arg) {
    (void)arg;
    reflex_task_delay_ms(3000);
    printf("[reflex.kernel] supervisor: policy=%s\n",
           s_policy_fn ? "registered" : "none");
    while (1) {
        s_kernel_tick++;
        if (s_policy_fn) s_policy_fn(s_kernel_tick);
        reflex_task_delay_ms(KERNEL_POLICY_PERIOD_MS);
    }
}

void reflex_kernel_start_supervisor(void) {
    esp_rom_printf("\n");
    esp_rom_printf("  ╔══════════════════════════════════════╗\n");
    esp_rom_printf("  ║       Reflex OS Kernel Active        ║\n");
    esp_rom_printf("  ╚══════════════════════════════════════╝\n");
    esp_rom_printf("\n");

    /* One below the ceiling, expressed in the interface's own terms rather than
     * configMAX_PRIORITIES, which came from a FreeRTOS header this file no
     * longer includes on every path. */
    reflex_task_create(reflex_kernel_supervisor, "reflex-kern", 4096, NULL,
                       REFLEX_KERNEL_SUPERVISOR_PRIO, NULL);

    /* Was "tick=1000Hz", which was never true: the alarm wrote to I2C0 and
     * fired at 0Hz. The policy cadence is what this line should report. */
    esp_rom_printf("[reflex.kernel] policy=%dms supervisor=active\n",
                   KERNEL_POLICY_PERIOD_MS);
}

BaseType_t __wrap_xPortStartScheduler(void) {
    reflex_kernel_start_supervisor();
    return __real_xPortStartScheduler();
}
