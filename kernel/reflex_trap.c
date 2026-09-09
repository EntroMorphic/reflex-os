/**
 * @file reflex_trap.c
 * @brief Reflex kernel trap handler.
 *
 * Called from reflex_vectors.S with the saved context frame in a0; the value
 * returned becomes the sp the assembly restores before `mret`. Compiled, never
 * called — nothing installs this vector. See reflex_startup.c for why.
 *
 * ## mcause on the ESP32-C6 is not the textbook encoding
 *
 * An earlier version of this file dispatched on `mcause == 7` and called it
 * "the RISC-V machine timer interrupt". That is the CLINT encoding from the
 * privileged spec, and it is not what this chip does. The C6 uses a PLIC with
 * `RV_EXTERNAL_INT_OFFSET == 0`, and ESP-IDF's own dispatcher says what that
 * means: "mcause contains the interrupt number that triggered the current
 * interrupt" (`components/riscv/interrupt.c`). For an interrupt, mcause is the
 * **CPU interrupt line the interrupt matrix was programmed to route to** —
 * a number chosen when the peripheral is wired up, not a fixed architectural
 * constant.
 *
 * Worse, 7 is not unused on this chip: for an *exception*, ESP-IDF reads
 * `mcause == 7` as a store access fault and `mcause == 5` as a load access
 * fault (`riscv/rv_utils.h`). So the old constant named one thing, meant
 * another, and collided with a third.
 *
 * The tick line therefore cannot be a compile-time constant. It is whatever
 * the routing step assigns, and that step does not exist yet — see below.
 *
 * ## What the standalone tick still needs
 *
 * There are two tick paths in the tree and only one works.
 *
 * `reflex_kernel_test.c` routes SYSTIMER TARGET1 through `esp_intr_alloc`,
 * which is ESP-IDF's allocator doing the interrupt-matrix mapping, the PLIC
 * priority and the `mie` bit; its ISR then calls `reflex_sched_tick` directly.
 * That works, and it is not independence.
 *
 * The standalone path is missing three things:
 *
 *  1. `setup_systimer_tick` in reflex_sched.c enables the interrupt at the
 *     SYSTIMER peripheral and stops. Nothing maps it through the interrupt
 *     matrix to a CPU line, gives it a PLIC priority, or sets the matching
 *     `mie` bit. The peripheral raises an interrupt that never reaches the core.
 *  2. Nothing tells this handler which line it was mapped to, which is what
 *     `reflex_trap_set_tick_line` below exists to receive. Until it is called,
 *     the handler claims no interrupt as the tick — deliberately, because
 *     guessing a line number is how you silently service the wrong device.
 *  3. With no tick, `reflex_sched_start` runs its first task and parks on
 *     `wfi` the moment everything blocks, with nothing left to wake it.
 *
 * That is register programming which has to be validated on hardware: a
 * misrouted interrupt does not fail to build.
 */

#include "reflex_sched.h"
#include "reflex_hal.h"
#include "reflex_rom_esp32c6.h"
#include <stdint.h>

#define MCAUSE_INTERRUPT_BIT (1U << 31)
/* Interrupt line numbers occupy the low bits of mcause on this chip. */
#define MCAUSE_CODE_MASK 0x7FFFFFFFu

/* The CPU interrupt line the scheduler tick arrives on, or -1 for "not routed".
 *
 * Not a constant, because it is not architectural: the interrupt matrix decides
 * it. -1 rather than a plausible default, because a wrong line number does not
 * fail — it services some other peripheral's interrupt as though it were the
 * tick, and leaves that peripheral's interrupt asserted forever. */
static volatile int s_tick_cpu_int = -1;

/* Point mtvec at Reflex's vector table.
 *
 * Separated from reflex_kernel_startup so the hand-off can be exercised without
 * also taking over task creation. */
/* An anomaly worth stating before the code below, because the code below is
 * correct and the board disagrees.
 *
 * With mscratch left as found — which is to say uninitialised — the hand-off
 * got further: the scheduler started and ran both test tasks, printing
 * `task A: tick 0 (sys=1)` and the same for B, before dying on the first tick.
 * With mscratch set properly to a trap stack, it dies at the mtvec install
 * itself, consistently, three runs.
 *
 * Part of that was self-inflicted and is now fixed: the exit path re-established
 * mscratch by loading a pointer *variable*, and that variable landed in flash
 * at 0x420f2828. So every trap return performed a flash read from inside the
 * trap handler, which is not safe there and was a new load introduced by the
 * very change that regressed. The address is materialised directly now — no
 * memory access on the trap path at all.
 *
 * That inverts what should happen. A vector whose first instruction is
 * `csrrw sp, mscratch, sp` cannot be correct with mscratch unset, so the fix
 * below is right on its face; the machine says otherwise, and the disagreement
 * is not understood. It is kept as the fix rather than reverted to the luckier
 * version, and recorded here rather than smoothed over, because "correct by
 * reasoning, worse by measurement" is the exact shape of the mistakes this
 * file's history is made of. Resolving it is the next step of C3, not a
 * footnote to it.
 *
 * ---
 *
 * The trap stack, and the pointer the vector swaps into sp.
 *
 * mscratch must hold this before the vector is installed: entry does
 * `csrrw sp, mscratch, sp`, so whatever mscratch holds becomes the stack the
 * 132-byte frame is written to. Nothing in the tree ever wrote mscratch, so
 * the first trap after taking mtvec wrote that frame wherever the register
 * happened to point.
 *
 * Defined in C rather than as a .bss block in the vector file, so the
 * assembly's section layout is left exactly as it was — an earlier attempt put
 * it there and moved the entry point's section along with it. */
#define REFLEX_TRAP_STACK_WORDS 512
uint32_t reflex_trap_stack[REFLEX_TRAP_STACK_WORDS] __attribute__((aligned(16)));

void reflex_trap_install(void) {
    /* mscratch first, then mtvec. The vector's first instruction swaps sp with
     * mscratch, so installing the vector while mscratch is unset points the
     * very next trap's frame at whatever that register happened to hold. */
    uint32_t *top = reflex_trap_stack + REFLEX_TRAP_STACK_WORDS;
    __asm__ volatile("csrw mscratch, %0" : : "r"(top));
    /* Vectored mode, and the table rather than the handler.
     *
     * The mode field is not ours to choose: written with mode 0 it reads back
     * as 1 on this part, matching ESP-IDF's own mtvec. And the base is masked
     * to 256 bytes, so pointing it at a handler that merely happens to be
     * 4-byte aligned silently relocates the vector to somewhere before it.
     * reflex_vector_table is aligned for it and every entry jumps to the
     * handler, so all causes converge where the code always thought they did. */
    __asm__ volatile("la t0, reflex_vector_table\n"
                     "ori t0, t0, 1\n"
                     "csrw mtvec, t0\n"
                     :
                     :
                     : "t0");
}

/* Capture and restore the trap CSRs, so taking mtvec is a reversible step.
 *
 * Without these, any failure detected after reflex_trap_install has nowhere to
 * go: ESP-IDF's vector is gone and its address was never written down, so the
 * only honest response is to stall, and a stalled board needs a reflash. That
 * is the same shape as the sleep watchdog that stranded a board earlier in this
 * work — a safety mechanism whose failure mode is the thing it existed to
 * prevent. With the prior values in hand, a post-install check can hand the
 * machine back and keep a board that boots. */
void reflex_trap_snapshot(uint32_t *mtvec_out, uint32_t *mscratch_out) {
    uint32_t mtvec_val, mscratch_val;
    __asm__ volatile("csrr %0, mtvec" : "=r"(mtvec_val));
    __asm__ volatile("csrr %0, mscratch" : "=r"(mscratch_val));
    if (mtvec_out) *mtvec_out = mtvec_val;
    if (mscratch_out) *mscratch_out = mscratch_val;
}

void reflex_trap_restore(uint32_t mtvec_val, uint32_t mscratch_val) {
    /* mscratch first. mtvec is the switch: once it points back at ESP-IDF's
     * table, the next interrupt is handled by ESP-IDF's entry, which expects
     * its own mscratch. Restoring them the other way round leaves a window
     * where ESP-IDF's handler runs against Reflex's trap stack. */
    __asm__ volatile("csrw mscratch, %0" : : "r"(mscratch_val));
    __asm__ volatile("csrw mtvec, %0" : : "r"(mtvec_val));
}

void reflex_trap_set_tick_line(int cpu_int) {
    s_tick_cpu_int = cpu_int;
}

int reflex_trap_get_tick_line(void) {
    return s_tick_cpu_int;
}

/**
 * @brief Handle a trap; return the frame to resume from.
 *
 * The frame is returned unchanged for interrupts. reflex_vectors.S does honour
 * a different value — it restores sp from the return — so a preemptive switch
 * could be done here, but none is: context switching is cooperative, via
 * setjmp/longjmp in reflex_sched_yield.
 *
 * Does not return for exceptions.
 */
uint32_t *reflex_trap_handler(uint32_t *frame) {
    uint32_t mcause;
    __asm__ volatile("csrr %0, mcause" : "=r"(mcause));

    if (mcause & MCAUSE_INTERRUPT_BIT) {
        int line = (int)(mcause & MCAUSE_CODE_MASK);
        if (s_tick_cpu_int >= 0 && line == s_tick_cpu_int) {
            reflex_sched_tick();
            reflex_sched_ack_tick();
            return frame;
        }
        /* Every other line goes to whatever Reflex registered for it.
         *
         * This handler used to service the tick and nothing else, which was
         * defensible while it only ran for the length of `kernel selftest`.
         * Once Reflex owns the entry point it is the machine's only interrupt
         * dispatcher, and a dispatcher that answers one line is why the board
         * printed a shell prompt and then ignored every key pressed at it: the
         * console receive interrupt is Reflex's own, allocated through Reflex's
         * own allocator, and it was arriving here and being dropped.
         *
         * The handler table is the HAL's, so this asks the HAL rather than
         * keeping a second copy of it. */
        if (reflex_hal_intr_dispatch_line(line)) {
            return frame;
        }
        /* Genuinely nobody's — an ESP-IDF interrupt, most likely, allocated
         * through its allocator during boot and invisible to Reflex's table.
         *
         * This used to leave it alone, on the grounds that acknowledging an
         * interrupt this layer does not understand would be worse. That is true
         * of acknowledging it. It is not true of masking it: left asserted, a
         * level-triggered line re-enters immediately and forever, so the choice
         * was never "leave it alone", it was "stop the machine". Masking keeps
         * the machine and loses one device, and reflex_hal_intr_unclaimed_lines
         * says which. */
        /* Before masking, ask whether anyone else owns it.
         *
         * ESP-IDF keeps its own per-line handler table and exposes a getter,
         * so a line that is nobody's as far as Reflex's table is concerned may
         * still have a driver waiting on it. Masking without asking is how
         * independence came to mean "the peripherals are switched off": under
         * this vector only the tick and the console were live, and the
         * blob-free radio could transmit but never receive. */
        if (reflex_hal_intr_dispatch_foreign(line)) {
            return frame;
        }

        reflex_hal_intr_mask_unclaimed(line);
        return frame;
    }

    /* An exception, and this must not return.
     *
     * mepc still points at the faulting instruction, so restoring the frame and
     * executing mret re-runs it and traps again, forever. The version before
     * this one did exactly that — an empty exception arm falling through to
     * `return frame` — turning any fault into a silent lockup. A stubbed
     * exception handler that returns is worse than no handler at all.
     *
     * esp_rom_printf, not REFLEX_LOGE. The logging path formats into a
     * 192-byte stack buffer, and the stack here is the interrupted context's —
     * which may be precisely what overflowed and caused this trap. Adding
     * another 192 bytes to an overflowed stack faults again, inside the
     * handler, and recurses until the machine dies with nothing printed. The
     * ROM printf writes through a fixed ROM routine with a small frame. */
    uint32_t mepc = 0, mtval = 0;
    __asm__ volatile("csrr %0, mepc" : "=r"(mepc));
    __asm__ volatile("csrr %0, mtval" : "=r"(mtval));

    /* esp_rom_printf, not REFLEX_LOGE, and not Reflex's console writer either.
     *
     * The logging path formats into a 192-byte stack buffer, and the stack here
     * is the interrupted context's — which may be precisely what overflowed and
     * caused this trap. The ROM printf writes through a fixed ROM routine with a
     * small frame.
     *
     * This was briefly changed to write through reflex_hal_console_emit on the
     * belief that esp_rom_printf goes to UART0 and is invisible on a
     * USB-serial-JTAG board. That belief was wrong, and the evidence for it was
     * worthless: two probes placed with esp_rom_printf printed nothing, but the
     * code holding them was never reached — an interrupt storm was stopping the
     * machine earlier. Tested directly afterwards, esp_rom_printf prints here
     * perfectly well. Reverted, because the replacement was strictly worse in a
     * fatal path: it spins on a USB FIFO and calls reflex_hal_time_us, on a
     * machine already known to be broken. */
    esp_rom_printf("[reflex.trap] unhandled exception mcause=0x%x mepc=0x%x mtval=0x%x - halting\n",
                   (unsigned)mcause, (unsigned)mepc, (unsigned)mtval);

    /* Interrupts stay disabled: trap entry cleared MIE, and re-enabling would
     * let a tick preempt a machine already known to be broken. */
    for (;;) {
        __asm__ volatile("wfi");
    }
}
