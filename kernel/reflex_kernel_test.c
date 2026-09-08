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
    for (int i = 0; i < 10; i++) {
        esp_rom_printf("[kernel] task A: tick %d (sys=%lu)\n",
                       i, (unsigned long)reflex_sched_get_tick());
        reflex_sched_delay_ms(500);
    }
    esp_rom_printf("[kernel] task A: done\n");
}

static void task_b(void *arg) {
    (void)arg;
    for (int i = 0; i < 10; i++) {
        esp_rom_printf("[kernel] task B: tick %d (sys=%lu)\n",
                       i, (unsigned long)reflex_sched_get_tick());
        reflex_sched_delay_ms(700);
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

    /* Take the stack watchpoint back before switching stacks. Without this the
     * first switch panics with a stack protection fault — the SP is inside the
     * Reflex task's own stack, and ESP-IDF's guard is still armed with the
     * bounds of the FreeRTOS task this was called from. */
    reflex_hal_stack_guard_disable();

    esp_rom_printf("[kernel] starting scheduler (cooperative)...\n");
    rc = reflex_sched_start();

    esp_rom_printf("[kernel] ERROR: scheduler returned (0x%x)\n", (int)rc);
}
