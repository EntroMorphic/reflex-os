/**
 * @file reflex_kernel_test.c
 * @brief Standalone kernel scheduler test.
 *
 * Registers a SYSTIMER TARGET1 ISR to drive the kernel tick, then
 * creates two tasks that alternate printing via cooperative yield.
 * Proves the Reflex kernel can schedule tasks on real hardware.
 */

#include "reflex_sched.h"
#include "reflex_rom_esp32c6.h"
#include "reflex_hal.h"
#include "reflex_soc_esp32c6.h"

/* SYSTIMER TARGET1's interrupt-matrix source number.
 *
 * This said 38, which is wrong: ETS_SYSTIMER_TARGET1_INTR_SOURCE is 58 on the
 * C6, and 38 is a different peripheral. Routing 38 would have mapped some
 * other device's interrupt as the scheduler tick — the tick would never fire
 * and that device's interrupt would be hijacked. Never caught because this
 * file was in no build and had never run.
 *
 * The value now comes from the SVD-backed header, where `make soc-bridge`
 * proves it against ESP-IDF's own enum rather than trusting a number typed
 * into a comment. */
#include "reflex_soc_esp32c6.h"

static void task_a(void *arg) {
    (void)arg;
    for (int i = 0; i < 20; i++) {
        esp_rom_printf("[kernel] task A: tick %d (sys=%lu)\n",
                       i, (unsigned long)reflex_sched_get_tick());
        reflex_sched_delay_ms(100);
    }
    esp_rom_printf("[kernel] task A: done\n");
}

static void task_b(void *arg) {
    (void)arg;
    for (int i = 0; i < 20; i++) {
        esp_rom_printf("[kernel] task B: tick %d (sys=%lu)\n",
                       i, (unsigned long)reflex_sched_get_tick());
        reflex_sched_delay_ms(130);
    }
    esp_rom_printf("[kernel] task B: done\n");
}

void reflex_kernel_test(void) {
    esp_rom_printf("\n[kernel] Reflex OS kernel scheduler test\n");

    /* The tick is not allocated here any more.
     *
     * This routed SYSTIMER_TARGET1 itself and then called reflex_sched_start,
     * which routes and arms it too — two allocations of the same source, the
     * second landing on a different CPU line than the first. That was harmless
     * only because nothing ever called this function. reflex_sched_start owns
     * the tick now, and `kernel tick` measures the same path: 5/5 verified cold
     * starts at 1000 Hz on the independence build. */

    /* Each of these reported a status that was discarded. Starting a scheduler
     * whose tasks failed to be created looks identical to a scheduler that
     * hangs, and this is a test — the one place a silent failure is least
     * affordable. */
    reflex_err_t rc = reflex_sched_init();
    if (rc != REFLEX_OK) {
        esp_rom_printf("[kernel] ERROR: sched init failed (0x%x)\n", (int)rc);
        return;
    }
    rc = reflex_sched_create_task(task_a, "task-A", 4096, NULL, 5, NULL);
    if (rc != REFLEX_OK) {
        esp_rom_printf("[kernel] ERROR: task-A create failed (0x%x)\n", (int)rc);
        return;
    }
    rc = reflex_sched_create_task(task_b, "task-B", 4096, NULL, 5, NULL);
    if (rc != REFLEX_OK) {
        esp_rom_printf("[kernel] ERROR: task-B create failed (0x%x)\n", (int)rc);
        return;
    }

    /* --- the hand-off ---------------------------------------------------
     *
     * Everything below is one transaction: after mtvec is taken, ESP-IDF's
     * dispatcher is gone and only Reflex's handler runs. The order is not
     * arbitrary and each step exists because skipping it was tried.
     *
     * Route and arm the tick first, so its CPU line is known before the
     * handler that must recognise it is installed. reflex_sched_start would do
     * this itself, but by then it is too late to ask which line it chose. */
    rc = reflex_sched_tick_start();
    if (rc != REFLEX_OK) {
        esp_rom_printf("[kernel] ERROR: tick start failed (0x%x)\n", (int)rc);
        return;
    }

    reflex_intr_route_t tick;
    reflex_hal_intr_describe(REFLEX_INTR_SRC_SYSTIMER_TARGET1, &tick);
    esp_rom_printf("[kernel] tick on cpu_int=%d\n", (int)tick.cpu_int);

    /* Tell the handler which line is the tick. Without this it recognises
     * nothing, leaves every interrupt unacknowledged, and the core stops. */
    reflex_trap_set_tick_line((int)tick.cpu_int);

    /* Take the stack watchpoint back. ESP-IDF arms it with the bounds of the
     * FreeRTOS task this was called from, so the first switch to a Reflex
     * stack panics — observed exactly that way, SP inside the Reflex task's
     * own stack and the guard still holding the caller's bounds. Doing this
     * alone was not enough: FreeRTOS re-arms it on every context switch, which
     * is why the quiesce below has to come with it. */
    reflex_hal_stack_guard_disable();

    /* Silence every line but the tick.
     *
     * Reflex's handler does not acknowledge a line it does not recognise —
     * deliberately, since acknowledging one blind is worse — so any ESP-IDF
     * interrupt left enabled asserts, is never cleared, and the machine stops.
     * FreeRTOS's own tick is among them, and quiescing it is also what stops
     * FreeRTOS re-arming the stack watchpoint. */
    uint32_t saved_mask = reflex_hal_intr_quiesce_except(1u << tick.cpu_int);
    esp_rom_printf("[kernel] quiesced PLIC 0x%08x -> 0x%08x\n", (unsigned)saved_mask,
                   (unsigned)(1u << tick.cpu_int));

    /* And stand down the timer-group watchdogs, which FreeRTOS was feeding.
     * Without this the chip resets a few seconds in with TG1_WDT_HPSYS, after
     * the scheduler has started perfectly well. */
    reflex_hal_wdt_disable_timg();
    esp_rom_printf("[kernel] timer-group watchdogs disabled\n");

    esp_rom_printf("[kernel] taking mtvec\n");
    reflex_trap_install();

    esp_rom_printf("[kernel] starting scheduler (cooperative)...\n");
    rc = reflex_sched_start();

    /* If it ever returns, put the machine back the way it was found so the
     * failure is reportable rather than silent. */
    reflex_hal_intr_restore(saved_mask);

    esp_rom_printf("[kernel] ERROR: scheduler returned (0x%x)\n", (int)rc);
}
