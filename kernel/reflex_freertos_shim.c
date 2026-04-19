/**
 * @file reflex_freertos_shim.c
 * @brief DEPRECATED — superseded by reflex_freertos_compat.c.
 *
 * This was the original scheduler hook before the kernel owned
 * interrupt context switching. The active implementation is in
 * reflex_freertos_compat.c, which:
 *   - Wraps xPortStartScheduler to create the supervisor task
 *   - Owns rtos_int_enter/exit via reflex_portasm.S
 *   - Runs a 1Hz policy tick that modulates task priorities
 *   - Registers the kernel tick ISR on SYSTIMER TARGET1
 *
 * This file is retained for reference only. It is not compiled.
 */
