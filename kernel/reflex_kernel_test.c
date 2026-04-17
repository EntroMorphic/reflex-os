/**
 * @file reflex_kernel_test.c
 * @brief Standalone kernel scheduler test.
 *
 * Registers a SYSTIMER TARGET1 ISR to drive the kernel tick, then
 * creates two tasks that alternate printing via cooperative yield.
 * Proves the Reflex kernel can schedule tasks on real hardware.
 */

#include "reflex_sched.h"
#include "esp_rom_sys.h"
#include "esp_intr_alloc.h"

/* SYSTIMER TARGET1 interrupt source number for C6 */
#define SYSTIMER_TARGET1_INTR_SOURCE 38

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

    /* Register the tick ISR via ESP-IDF's interrupt allocator */
    intr_handle_t isr_handle;
    esp_intr_alloc(SYSTIMER_TARGET1_INTR_SOURCE, ESP_INTR_FLAG_IRAM,
                   systimer_tick_isr, NULL, &isr_handle);

    reflex_sched_init();
    reflex_sched_create_task(task_a, "task-A", 4096, NULL, 5, NULL);
    reflex_sched_create_task(task_b, "task-B", 4096, NULL, 5, NULL);

    esp_rom_printf("[kernel] starting scheduler (cooperative)...\n");
    reflex_sched_start();

    esp_rom_printf("[kernel] ERROR: scheduler returned\n");
}
