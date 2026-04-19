/*
 * Reflex kernel port assembly — replaces FreeRTOS's rtos_int_enter/exit.
 *
 * Called from ESP-IDF's vectors.S. Must be in IRAM (interrupts can
 * fire when flash cache is disabled). Uses la (absolute addressing)
 * for all global accesses to avoid gp-relative addressing issues.
 */

    .section .iram1, "ax"
    .align 4

    .extern pxCurrentTCBs
    .extern xIsrStackTop
    .extern port_uxInterruptNesting
    .extern port_xSchedulerRunning
    .extern vTaskSwitchContext

    .global __wrap_rtos_int_enter
    .type __wrap_rtos_int_enter, @function
__wrap_rtos_int_enter:
    /* Check if scheduler is running */
    la      t0, port_xSchedulerRunning
    lw      t0, 0(t0)
    beqz    t0, .Lenter_end

    /* Increment nesting */
    la      t0, port_uxInterruptNesting
    lw      t1, 0(t0)
    addi    t2, t1, 1
    sw      t2, 0(t0)

    /* If nested (t1 > 0), skip stack save */
    li      a0, 0
    bnez    t1, .Lenter_end

    /* Save current SP to pxCurrentTCBs[0]->pxTopOfStack */
    la      t0, pxCurrentTCBs
    lw      t0, 0(t0)           /* t0 = pxCurrentTCBs[0] = TCB pointer */
    sw      sp, 0(t0)           /* TCB->pxTopOfStack = sp */

    /* Switch to ISR stack */
    la      t0, xIsrStackTop
    lw      sp, 0(t0)

    li      a0, 0               /* Return 0 (no coprocessor context) */
.Lenter_end:
    li      a0, 0               /* Ensure return value is always 0 */
    ret
    .size __wrap_rtos_int_enter, .-__wrap_rtos_int_enter


    .global __wrap_rtos_int_exit
    .type __wrap_rtos_int_exit, @function
__wrap_rtos_int_exit:
    /* Decrement nesting */
    la      t0, port_uxInterruptNesting
    lw      t1, 0(t0)
    addi    t1, t1, -1
    sw      t1, 0(t0)

    /* If still nested, skip context switch */
    bnez    t1, .Lexit_skip

    /* Only switch context if scheduler is running */
    la      t0, port_xSchedulerRunning
    lw      t0, 0(t0)
    beqz    t0, .Lexit_skip

    /* Call vTaskSwitchContext to pick the next task.
     * This updates pxCurrentTCBs[0]. */
    call    vTaskSwitchContext

    /* Restore SP from (possibly new) current TCB */
    la      t0, pxCurrentTCBs
    lw      t0, 0(t0)
    lw      sp, 0(t0)

.Lexit_skip:
    /* Return mstatus for vectors.S to restore via csrw */
    csrr    a0, mstatus
    ret
    .size __wrap_rtos_int_exit, .-__wrap_rtos_int_exit
