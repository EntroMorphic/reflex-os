/**
 * @file reflex_trap.c
 * @brief Reflex OS trap handler — dispatches interrupts and exceptions.
 *
 * Called from reflex_vectors.S with a pointer to the saved context
 * frame. Returns the sp of the task to resume (which may be different
 * from the interrupted task if a context switch occurred).
 */

#include "reflex_sched.h"
#include <stdint.h>

/* RISC-V mcause values */
#define MCAUSE_INTERRUPT_BIT  (1U << 31)
#define MCAUSE_MACHINE_TIMER  7
#define MCAUSE_MACHINE_EXT    11

/* The trap handler is called from assembly with the saved context
 * frame pointer in a0. It returns the sp to restore (may be the
 * same or a different task's sp after a context switch). */
uint32_t *reflex_trap_handler(uint32_t *frame) {
    uint32_t mcause;
    __asm__ volatile ("csrr %0, mcause" : "=r"(mcause));

    if (mcause & MCAUSE_INTERRUPT_BIT) {
        /* Interrupt */
        uint32_t cause = mcause & 0x7FF;
        if (cause == MCAUSE_MACHINE_TIMER) {
            reflex_sched_tick();
            extern void reflex_sched_ack_tick(void);
            reflex_sched_ack_tick();
        }
        /* Other interrupts can be dispatched here */
    } else {
        /* Exception — fault handling is stubbed. Interrupt dispatch
         * (tick, external) is handled by reflex_portasm.S and the
         * ESP-IDF vector table. */
    }

    return frame;
}
