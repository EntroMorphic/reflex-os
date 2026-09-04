/**
 * @file reflex_trap.c
 * @brief Reflex kernel trap handler.
 *
 * Called from reflex_vectors.S with the saved context frame in a0, and returns
 * the frame to resume from. Compiled, never called — see reflex_startup.c for
 * why nothing installs this vector yet.
 *
 * ## The standalone tick does not yet reach this handler
 *
 * There are two tick paths in the tree and only one of them works.
 *
 * `reflex_kernel_test.c` routes SYSTIMER TARGET1 through `esp_intr_alloc`,
 * which is ESP-IDF's interrupt allocator doing the interrupt-matrix mapping,
 * the PLIC priority and the `mie` bit. Its ISR then calls `reflex_sched_tick`
 * directly. That works, and it is not independence: the tick arrives courtesy
 * of the framework this is meant to replace.
 *
 * The standalone path — `setup_systimer_tick` in reflex_sched.c, plus this
 * handler — is incomplete in three specific ways, recorded here because they
 * are the substance of what C2 still owes:
 *
 *  1. `setup_systimer_tick` enables the interrupt at the SYSTIMER peripheral
 *     and stops there. Nothing maps it through the C6 interrupt matrix to a
 *     CPU line, sets a PLIC priority, or enables the corresponding `mie` bit.
 *     The peripheral raises an interrupt that never reaches the core.
 *  2. This handler recognises `mcause == 7`, the RISC-V machine *timer*
 *     interrupt from the CLINT. A C6 peripheral interrupt does not arrive that
 *     way; it comes through the interrupt matrix. So even a correctly routed
 *     systimer tick would fall through the dispatch below unhandled.
 *  3. With no tick, `reflex_sched_start` runs the first task and then parks on
 *     `wfi` the moment every task blocks, with nothing left to wake them.
 *
 * None of that is guesswork about hardware behaviour — it is what the code
 * does and does not do. Closing it is register programming that has to be
 * validated on a board, because a misrouted interrupt does not fail to build.
 */

#include "reflex_sched.h"
#include "reflex_hal.h"
#include <stdint.h>

/* RISC-V mcause values */
#define MCAUSE_INTERRUPT_BIT (1U << 31)
#define MCAUSE_MACHINE_TIMER 7
#define MCAUSE_MACHINE_EXT 11

/**
 * @brief Handle a trap and return the frame to resume.
 *
 * The frame is returned unchanged. An earlier comment described this as
 * returning "the sp of the task to resume (which may be different ... if a
 * context switch occurred)", which overstated it: context switching is
 * cooperative, done by setjmp/longjmp in reflex_sched_yield, and no path here
 * has ever switched tasks.
 */
uint32_t *reflex_trap_handler(uint32_t *frame) {
    uint32_t mcause;
    __asm__ volatile("csrr %0, mcause" : "=r"(mcause));

    if (mcause & MCAUSE_INTERRUPT_BIT) {
        uint32_t cause = mcause & 0x7FF;
        if (cause == MCAUSE_MACHINE_TIMER) {
            reflex_sched_tick();
            reflex_sched_ack_tick();
        }
        /* Any other interrupt is ignored, which is survivable: the source
         * stays asserted and the machine makes no progress, but it does not
         * corrupt anything. Dispatch for external interrupts belongs here once
         * item 1 above is done. */
        return frame;
    }

    /* An exception, and the handler must not return.
     *
     * mepc still points at the instruction that faulted, so restoring the
     * frame and executing mret re-runs it and traps again, forever. The
     * previous version did exactly that — the exception arm was empty and fell
     * through to `return frame` — which turns any fault into a silent lockup
     * with no output and nothing to inspect. A stubbed exception handler that
     * returns is worse than none at all.
     *
     * Halting loudly instead. There is no fault recovery to attempt here and
     * no scheduler state worth preserving once the machine has taken an
     * exception this layer does not understand. */
    uint32_t mepc = 0, mtval = 0;
    __asm__ volatile("csrr %0, mepc" : "=r"(mepc));
    __asm__ volatile("csrr %0, mtval" : "=r"(mtval));

    REFLEX_LOGE("reflex.trap", "unhandled exception mcause=0x%lx mepc=0x%lx mtval=0x%lx — halting",
                (unsigned long)mcause, (unsigned long)mepc, (unsigned long)mtval);

    /* Interrupts stay disabled: the trap entry cleared MIE, and re-enabling
     * would let a tick preempt a machine already known to be broken. */
    for (;;) {
        __asm__ volatile("wfi");
    }
}
