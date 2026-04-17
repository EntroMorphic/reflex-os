/**
 * @file reflex_app_startup.c
 * @brief Reflex kernel running ON TOP of FreeRTOS.
 *
 * Architecture: FreeRTOS handles interrupts, preemption, and
 * hardware context switching (the microkernel). The Reflex scheduler
 * manages application-level concerns: purpose-modulated task
 * priority, Hebbian scheduling preferences, holon lifecycle.
 *
 * This is the same architecture as Linux (runs on a microkernel
 * interrupt handler) and macOS (Mach microkernel + BSD personality).
 * FreeRTOS is our scheduling HAL — it's behind reflex_task.h.
 *
 * When CONFIG_REFLEX_KERNEL_SCHEDULER is set, this file replaces
 * the default app startup to run our supervisor loop.
 */

/* This file is currently a stub — the existing reflex_task_esp32c6.c
 * backend already routes through FreeRTOS correctly. The kernel
 * supervisor (purpose-modulated scheduling, holon lifecycle) will
 * be added here as an OS-level task that adjusts FreeRTOS task
 * priorities based on the substrate's purpose and plasticity state.
 *
 * For now, the standard ESP-IDF startup path runs unchanged. */
