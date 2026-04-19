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
 *   reflex_task_kernel.c      — complete standalone task backend
 *                               (13 functions) selectable via
 *                               CONFIG_REFLEX_KERNEL_SCHEDULER
 *
 * The standard ESP-IDF startup path runs unchanged. The kernel hooks
 * are injected at link time via --wrap flags in CMakeLists.txt.
 */
