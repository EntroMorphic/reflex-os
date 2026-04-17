/*
 * Reflex OS — RISC-V Exception Vectors and Context Switch
 *
 * This is the assembly-level interface between the silicon and the
 * kernel scheduler. It handles:
 * 1. Trap entry — save all GP registers + CSRs
 * 2. Trap dispatch — call the C handler
 * 3. Context switch — restore a (possibly different) task's registers
 * 4. Task startup — load a new task's initial context
 *
 * The context frame is 33 words (132 bytes):
 *   [0]  x1 (ra)
 *   [1]  x2 (sp) — saved but not restored (sp is the frame pointer)
 *   [2]  x3 (gp)
 *   [3]  x4 (tp)
 *   [4]  x5 (t0) ... [30] x31 (t6)
 *   [31] mepc
 *   [32] mstatus
 */

    .section .text
    .global reflex_trap_entry
    .global reflex_context_switch
    .align 4

/*
 * Trap entry point. Set as mtvec. Saves all registers, calls
 * reflex_trap_handler (C), then restores registers and mret.
 */
reflex_trap_entry:
    /* Save sp to mscratch, load kernel sp if needed */
    csrrw sp, mscratch, sp

    /* Allocate context frame (33 words = 132 bytes) */
    addi sp, sp, -132

    /* Save x1-x31 (x0 is always zero) */
    sw   x1,  0(sp)
    /* x2 (sp) saved below from mscratch */
    sw   x3,  8(sp)
    sw   x4,  12(sp)
    sw   x5,  16(sp)
    sw   x6,  20(sp)
    sw   x7,  24(sp)
    sw   x8,  28(sp)
    sw   x9,  32(sp)
    sw   x10, 36(sp)
    sw   x11, 40(sp)
    sw   x12, 44(sp)
    sw   x13, 48(sp)
    sw   x14, 52(sp)
    sw   x15, 56(sp)
    sw   x16, 60(sp)
    sw   x17, 64(sp)
    sw   x18, 68(sp)
    sw   x19, 72(sp)
    sw   x20, 76(sp)
    sw   x21, 80(sp)
    sw   x22, 84(sp)
    sw   x23, 88(sp)
    sw   x24, 92(sp)
    sw   x25, 96(sp)
    sw   x26, 100(sp)
    sw   x27, 104(sp)
    sw   x28, 108(sp)
    sw   x29, 112(sp)
    sw   x30, 116(sp)
    sw   x31, 120(sp)

    /* Save the original sp (from mscratch) */
    csrr t0, mscratch
    sw   t0, 4(sp)

    /* Save mepc and mstatus */
    csrr t0, mepc
    sw   t0, 124(sp)
    csrr t0, mstatus
    sw   t0, 128(sp)

    /* Call C trap handler with sp as argument (pointer to context frame) */
    mv   a0, sp
    call reflex_trap_handler

    /* The trap handler may have changed s_current->sp, meaning we
     * should restore a different task's context. Load sp from the
     * return value (or from the current task's TCB). For simplicity,
     * the handler returns the sp to restore in a0. */
    mv   sp, a0

    /* Restore mepc and mstatus */
    lw   t0, 124(sp)
    csrw mepc, t0
    lw   t0, 128(sp)
    csrw mstatus, t0

    /* Restore x1-x31 */
    lw   x1,  0(sp)
    /* x2 (sp) restored last */
    lw   x3,  8(sp)
    lw   x4,  12(sp)
    lw   x5,  16(sp)
    lw   x6,  20(sp)
    lw   x7,  24(sp)
    lw   x8,  28(sp)
    lw   x9,  32(sp)
    lw   x10, 36(sp)
    lw   x11, 40(sp)
    lw   x12, 44(sp)
    lw   x13, 48(sp)
    lw   x14, 52(sp)
    lw   x15, 56(sp)
    lw   x16, 60(sp)
    lw   x17, 64(sp)
    lw   x18, 68(sp)
    lw   x19, 72(sp)
    lw   x20, 76(sp)
    lw   x21, 80(sp)
    lw   x22, 84(sp)
    lw   x23, 88(sp)
    lw   x24, 92(sp)
    lw   x25, 96(sp)
    lw   x26, 100(sp)
    lw   x27, 104(sp)
    lw   x28, 108(sp)
    lw   x29, 112(sp)
    lw   x30, 116(sp)
    lw   x31, 120(sp)

    /* Restore sp and return from trap */
    lw   sp, 4(sp)
    mret
