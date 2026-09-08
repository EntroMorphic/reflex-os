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
#define SYSTIMER_TARGET1_INTR_SOURCE REFLEX_INTR_SRC_SYSTIMER_TARGET1

static void __attribute__((section(".iram1"))) systimer_tick_isr(void *arg) {
    (void)arg;
    reflex_sched_tick();
    reflex_sched_ack_tick();
}

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

    /* The tick arrives through Reflex's own interrupt allocator, which does the
     * interrupt-matrix mapping, the PLIC priority and the mie bit itself.
     *
     * It used to come through ESP-IDF's, with a comment noting that this
     * therefore exercised the Reflex scheduler and not Reflex's independence.
     * That is no longer the trade: reflex_hal_intr_alloc is the path the
     * console's receive interrupt runs on and the path the scheduler tick was
     * measured on at 1000 Hz across ten cold starts.
     *
     * Worth being plain that nothing calls this function — the linker discards
     * the whole object, which is why its ESP-IDF include cost the image nothing
     * and the independence count showed it anyway. It is fixed here so that
     * wiring it up during the C3 scheduler cutover does not silently reintroduce
     * the dependency. */
    reflex_intr_handle_t isr_handle;
    reflex_err_t ie = reflex_hal_intr_alloc(REFLEX_INTR_SRC_SYSTIMER_TARGET1, 0, systimer_tick_isr,
                                            NULL, &isr_handle);
    if (ie != REFLEX_OK) {
        esp_rom_printf("[kernel] ERROR: tick ISR alloc failed (0x%x)\n", (int)ie);
        return;
    }

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

    esp_rom_printf("[kernel] starting scheduler (cooperative)...\n");
    rc = reflex_sched_start();

    esp_rom_printf("[kernel] ERROR: scheduler returned (0x%x)\n", (int)rc);
}
