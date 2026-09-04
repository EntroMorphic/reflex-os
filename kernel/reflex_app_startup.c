/**
 * @file reflex_app_startup.c
 * @brief Reflex kernel architecture notes.
 *
 * The Reflex kernel runs as a policy layer on the scheduling HAL:
 *
 *   reflex_freertos_compat.c  — hooks xPortStartScheduler, creates the
 *                               kernel supervisor task, registers our
 *                               1kHz tick ISR on SYSTIMER TARGET1
 *   reflex_portasm.S          — owns interrupt context switching via
 *                               --wrap on rtos_int_enter/rtos_int_exit
 *   goose_supervisor.c        — registers the policy function that
 *                               modulates task priorities based on
 *                               purpose, Hebbian plasticity, and holons
 *   reflex_task_kernel.c      — all 13 functions of reflex_task.h,
 *                               selectable via CONFIG_REFLEX_KERNEL_SCHEDULER.
 *                               Not standalone: each one delegates to
 *                               FreeRTOS. An earlier version of this note
 *                               called it a "complete standalone task
 *                               backend", which read as though the scheduler
 *                               had already been replaced.
 *   reflex_sched.c            — the scheduler that actually is standalone,
 *                               with reflex_startup.c, reflex_trap.c and
 *                               reflex_vectors.S as its entry path. All four
 *                               compile; none are reached, so the linker
 *                               discards them. They are in the build so they
 *                               cannot rot before the work to reach them
 *                               lands.
 *
 * The standard ESP-IDF startup path runs unchanged. The kernel hooks
 * are injected at link time via --wrap flags in CMakeLists.txt.
 *
 * This file contains no code — it is architecture notes in a .c file, and
 * compiles to an empty object.
 */
