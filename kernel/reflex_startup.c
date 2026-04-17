/**
 * @file reflex_startup.c
 * @brief Reflex OS startup — BSS clear, mtvec setup, scheduler init.
 *
 * This is the C-level startup code that runs after the bootloader
 * hands off to the kernel. Currently called from the ESP-IDF startup
 * path (app_main). When we fully own startup (Dep 10 complete), this
 * will be called from our own assembly entry point after stack setup.
 *
 * The startup sequence:
 * 1. Clear BSS (if not done by the loader/startup assembly)
 * 2. Set mtvec to our trap handler
 * 3. Initialize the scheduler
 * 4. Create the main task
 * 5. Start the scheduler (never returns)
 */

#include "reflex_sched.h"
#include "reflex_hal.h"
#include <stdint.h>

/* Assembly entry point from reflex_vectors.S */
extern void reflex_trap_entry(void);

void reflex_kernel_startup(void (*main_task)(void *), void *arg) {
    /* Set the trap vector to our handler */
    __asm__ volatile (
        "la t0, reflex_trap_entry\n"
        "csrw mtvec, t0\n"
        : : : "t0"
    );

    REFLEX_LOGI("reflex.kernel", "trap vector installed");

    /* Initialize the scheduler (creates idle task) */
    reflex_sched_init();

    /* Create the main task */
    reflex_sched_create_task(main_task, "main", 8192, arg, 10, NULL);

    REFLEX_LOGI("reflex.kernel", "scheduler starting");

    /* Start the scheduler — does not return */
    reflex_sched_start();
}
