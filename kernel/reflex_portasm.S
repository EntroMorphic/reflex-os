/*
 * Reflex kernel port — rtos_int_enter/exit replacement.
 * Matches FreeRTOS's portasm.S calling convention for single-core C6.
 * Callee-saved registers (s-regs) are safe — ISR vector saves them.
 *
 * Assumptions: CONFIG_FREERTOS_UNICORE=y, SOC_CPU_COPROC_NUM=0.
 * Porting to multi-core or coprocessor chips (P4) requires handling
 * per-core arrays and coprocessor save/restore in a1.
 */

#include "sdkconfig.h"

    .section .iram1, "ax"
    .align 4

    .extern pxCurrentTCBs
    .extern xIsrStackTop
    .extern xIsrStackBottom
    .extern port_uxInterruptNesting
    .extern port_xSchedulerRunning
    .extern xPortSwitchFlag
    .extern vTaskSwitchContext

#if CONFIG_ESP_SYSTEM_HW_STACK_GUARD
/* ASSIST_DEBUG base for hardware stack guard */
#define ASSIST_DEBUG_BASE   0x600c2000
#define ASSIST_DEBUG_CONF   0x600c2000
#define SP_MIN_OFF          0x38
#define SP_MAX_OFF          0x3C
#endif

/* TCB offsets (single-core, no integrity check bytes).
 * Validated by _Static_assert in reflex_freertos_compat.c */
#define TCB_PX_STACK        0x30
#define TCB_PX_END_OF_STACK 0x44

    .global __wrap_rtos_int_enter
    .type __wrap_rtos_int_enter, @function
__wrap_rtos_int_enter:
    lw      a0, port_xSchedulerRunning
    beqz    a0, .Lenter_end

    la      a0, port_uxInterruptNesting
    lw      a1, 0(a0)
    addi    a2, a1, 1
    sw      a2, 0(a0)

    li      a0, 0
    bnez    a1, .Lenter_end

#if CONFIG_ESP_SYSTEM_HW_STACK_GUARD
    /* --- Stack guard STOP --- */
    lui     a0, %hi(ASSIST_DEBUG_CONF)
    lw      a1, %lo(ASSIST_DEBUG_CONF)(a0)
    andi    a1, a1, -769
    sw      a1, %lo(ASSIST_DEBUG_CONF)(a0)
#endif

    /* Save SP to current TCB */
    lw      a0, pxCurrentTCBs
    sw      sp, 0(a0)

    /* Switch to ISR stack */
    lw      sp, xIsrStackTop

#if CONFIG_ESP_SYSTEM_HW_STACK_GUARD
    /* --- Stack guard START with ISR bounds --- */
    lw      a0, xIsrStackBottom
    mv      a1, sp
    lui     a2, %hi(ASSIST_DEBUG_BASE)
    sw      a0, SP_MIN_OFF(a2)
    sw      a1, SP_MAX_OFF(a2)
    lui     a0, %hi(ASSIST_DEBUG_CONF)
    lw      a1, %lo(ASSIST_DEBUG_CONF)(a0)
    ori     a1, a1, 768
    sw      a1, %lo(ASSIST_DEBUG_CONF)(a0)
#endif

    li      a0, 0
.Lenter_end:
    ret
    .size __wrap_rtos_int_enter, .-__wrap_rtos_int_enter


    .global __wrap_rtos_int_exit
    .type __wrap_rtos_int_exit, @function
__wrap_rtos_int_exit:
    /* Save ra and a0 (mstatus) in callee-saved regs.
     * ISR vector already saved s-regs, so this is safe. */
    mv      s10, ra
    mv      s11, a0

    lw      a0, port_xSchedulerRunning
    beqz    a0, .Lexit_end

    /* Decrement nesting (with underflow guard) */
    la      a2, port_uxInterruptNesting
    lw      a0, 0(a2)
    beqz    a0, .Lexit_skip_dec
    addi    a0, a0, -1
    sw      a0, 0(a2)
    bnez    a0, .Lexit_end
.Lexit_skip_dec:

    /* Only switch if xPortSwitchFlag is set */
    la      s6, xPortSwitchFlag
    lw      a1, 0(s6)
    beqz    a1, .Lexit_restore_sp

    call    vTaskSwitchContext
    sw      zero, 0(s6)

.Lexit_restore_sp:
#if CONFIG_ESP_SYSTEM_HW_STACK_GUARD
    /* --- Stack guard STOP --- */
    lui     a0, %hi(ASSIST_DEBUG_CONF)
    lw      a1, %lo(ASSIST_DEBUG_CONF)(a0)
    andi    a1, a1, -769
    sw      a1, %lo(ASSIST_DEBUG_CONF)(a0)
#endif

    /* Restore SP from (possibly new) current TCB */
    lw      a0, pxCurrentTCBs
    lw      sp, 0(a0)

#if CONFIG_ESP_SYSTEM_HW_STACK_GUARD
    /* --- Stack guard START with task's actual bounds --- */
    lw      a1, TCB_PX_END_OF_STACK(a0)
    lw      a0, TCB_PX_STACK(a0)
    lui     a2, %hi(ASSIST_DEBUG_BASE)
    sw      a0, SP_MIN_OFF(a2)
    sw      a1, SP_MAX_OFF(a2)
    lui     a0, %hi(ASSIST_DEBUG_CONF)
    lw      a1, %lo(ASSIST_DEBUG_CONF)(a0)
    ori     a1, a1, 768
    sw      a1, %lo(ASSIST_DEBUG_CONF)(a0)
#endif

.Lexit_end:
    mv      a0, s11
    mv      ra, s10
    ret
    .size __wrap_rtos_int_exit, .-__wrap_rtos_int_exit
