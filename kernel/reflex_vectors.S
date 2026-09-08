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

/* IRAM, not flash. A trap handler in flash cannot run while the cache is
 * disabled — during a flash write, say — which is precisely when a trap is
 * least affordable. ESP-IDF puts its own vector in IRAM at 0x40800000 for the
 * same reason. */
    .section .iram1, "ax"
    .global reflex_trap_entry
    .align 4

/*
 * `.global reflex_context_switch` used to sit here for a routine this file
 * never defined and nothing anywhere referenced — a leftover from a planned
 * context switch. Declaring a global for a symbol that does not exist puts an
 * undefined entry in the object's symbol table and would only surface as a
 * link error the first time something tried to call it. The context switch
 * belongs with C2, when reflex_sched_start is actually reached; it is not
 * announced here until it exists.
 */

/*
 * Trap entry point. Set as mtvec. Saves all registers, calls
 * reflex_trap_handler (C), then restores registers and mret.
 */
/* The vector table, and why there has to be one.
 *
 * This file assumed mtvec direct mode with 4-byte alignment: write the address
 * of reflex_trap_entry and every trap lands on it. The C6 does neither.
 *
 * Read back after writing it, mtvec holds 0x4201d901 where reflex_trap_entry
 * is at 0x4201d9b0. Two things happened to that value. The low bits of the
 * base were masked away — the hardware wants the table 256-byte aligned — and
 * the mode field came back as 1, vectored, which is also what ESP-IDF's own
 * mtvec reads (0x40800001). So the vector was installed 0xb0 bytes *before*
 * the handler, and interrupts dispatched to base + 4*cause: into whatever
 * instructions happened to precede the entry point.
 *
 * That is why the scheduler could run two cooperative tasks and then die on the
 * first tick. Nothing about the stack switch was wrong. The trap never arrived
 * where the code believed it would.
 *
 * So: a real table, 256-byte aligned, one jump per cause. Exceptions land at
 * entry 0 in vectored mode, interrupts at 4*cause, and every one of them jumps
 * to the common handler which already reads mcause to decide what happened. */
    .section .iram1, "ax"
    .align 8
    .global reflex_vector_table
reflex_vector_table:
    /* norvc, because vectored dispatch is BASE + 4*cause and the assembler
     * will happily compress `j` to a two-byte c.j. It did: the table came out
     * 64 bytes instead of 128, which would have sent cause 11 to entry 22 —
     * another silent wrong-target, of exactly the kind this table exists to
     * fix. */
    .option push
    .option norvc
    .rept 32
    j    reflex_trap_entry
    .endr
    .option pop

    .align 4
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

    /* Re-establish the trap stack for the next trap.
     *
     * Entry swaps sp with mscratch, so on the way in mscratch ends up holding
     * the interrupted task's sp — and nothing put the trap stack back. The
     * second trap would therefore swap that task sp into sp and build its
     * frame on the task's own stack, on top of whatever the task was doing.
     * The first trap after the vector was installed was the only one that
     * could work, and even it only if mscratch happened to hold something
     * usable. t0 is restored from the frame below, so it is free here. */
    /* Address materialised, not loaded. An earlier version read a pointer
     * variable here, and that variable lived in flash at 0x420f2828 — a flash
     * read on every trap return, from inside the trap handler. `la` with a
     * symbol+offset is an auipc/addi pair: no memory access at all. (2048 does
     * not fit a bare addi immediate, which is signed 12-bit; the pseudo-op
     * splits it across the pair correctly.) */
    la   t0, reflex_trap_stack + 2048
    csrw mscratch, t0

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
