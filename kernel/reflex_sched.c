/**
 * @file reflex_sched.c
 * @brief Reflex OS cooperative scheduler — setjmp/longjmp based.
 *
 * Each task has a jmp_buf for saving/restoring callee-saved registers.
 * New tasks are started by switching sp to their stack and calling
 * the entry function. Yielding saves the current context via setjmp
 * and restores the scheduler context via longjmp.
 *
 * @warning NOT CURRENTLY LINKED INTO THE FIRMWARE. This file compiles, but
 *          nothing calls reflex_sched_start on device and the linker discards
 *          every symbol here — no reflex_sched_* symbol appears in
 *          reflex_os.elf. reflex_task_kernel.c delegates task management to
 *          FreeRTOS instead (see docs/implementation-status.md). Note that
 *          CONFIG_REFLEX_KERNEL_SCHEDULER=y does NOT activate this scheduler;
 *          it selects the kernel task backend, which is the FreeRTOS-delegating
 *          one. The host test suite exercises the parts that work without a
 *          real context switch.
 *
 * @warning Before reviving this, fix the stack switch in reflex_sched_start.
 *          Assigning sp with inline asm part-way through a C function is
 *          undefined behaviour: the compiler still believes it owns the frame,
 *          so `next` and `new_sp` may live in the old one, and `next->state`
 *          is written after entry() returns through a pointer that may since
 *          have been clobbered. It survives today only by optimisation-level
 *          luck. A correct implementation needs an asm trampoline that
 *          switches sp and jumps in one step, never returning into C on the
 *          old stack.
 */

#include "reflex_sched.h"
#include "reflex_kqueue.h"
#include "reflex_hal.h"
#include <string.h>
#include <stdlib.h>

/* SYSTIMER registers for tick generation.
 *
 * The base was hardcoded as 0x60004000, which is I2C0 on the ESP32-C6 — the
 * shadow atlas names that exact address comm.i2c0.scl_low_period. Taken from
 * the SoC definitions now. Guarded because the host suite compiles this file
 * and has no soc/ headers. */
#include "reflex_regops.h"
#include "reflex_soc_esp32c6.h"

/* The host guard is gone with the ESP-IDF include that required it. The base
 * now comes from Reflex's own SVD-generated header, which the host suite can
 * compile as readily as the target — so the host build exercises these offsets
 * against the real base instead of against a stand-in 0. */
#define SYSTIMER_BASE REFLEX_DR_REG_SYSTIMER_BASE
#define SYSTIMER_CONF           (SYSTIMER_BASE + 0x00)
#define SYSTIMER_TARGET1_CONF   (SYSTIMER_BASE + 0x38)
#define SYSTIMER_COMP1_LOAD     (SYSTIMER_BASE + 0x54)
#define SYSTIMER_TARGET1_HI (SYSTIMER_BASE + 0x24)
#define SYSTIMER_TARGET1_LO (SYSTIMER_BASE + 0x28)
#define SYSTIMER_UNIT0_VALUE_HI (SYSTIMER_BASE + 0x40)
#define SYSTIMER_UNIT0_VALUE_LO (SYSTIMER_BASE + 0x44)
#define SYSTIMER_REAL_TARGET1_LO (SYSTIMER_BASE + 0x7C)
#define SYSTIMER_REAL_TARGET1_HI (SYSTIMER_BASE + 0x80)
#define SYSTIMER_INT_ENA        (SYSTIMER_BASE + 0x64)
#define SYSTIMER_INT_CLR (SYSTIMER_BASE + 0x6C)
#define SYSTIMER_INT_RAW (SYSTIMER_BASE + 0x68)
#define SYSTIMER_INT_ST (SYSTIMER_BASE + 0x70)
/* The SYSTIMER counter runs at 16 MHz, not at the 40 MHz crystal.
 *
 * The divider is fixed at 2.5 on the C6 — Reflex's own generated SoC header
 * records it as REFLEX_SOC_SYSTIMER_FIXED_DIVIDER, and ESP-IDF's soc_caps.h
 * spells it out: "Clock source divider is fixed: 2.5". The period was computed
 * against 40 MHz, and one measurement matched that exactly — 200 ticks in
 * 499167 us, 400 Hz against a target of 1000, which is 1/2.5 and puts the
 * counter at 16 MHz by arithmetic.
 *
 * That arithmetic is correct and is confirmed on hardware: on the default
 * ESP-NOW build the tick measures 491 ticks in 490865 us — 1000 Hz, exactly on
 * target. An earlier note here claimed the derived period did not produce the
 * derived rate; that was wrong, and it was wrong because every measurement
 * behind it came from the 802.15.4 build, where the tick does not fire for a
 * reason that has nothing to do with the period. See
 * docs/independence-dependency-map.md. */
#define SYSTIMER_CLK_HZ 16000000 /* 40 MHz XTAL / 2.5 */
#define SYSTIMER_TICK_PERIOD (SYSTIMER_CLK_HZ / REFLEX_SCHED_TICK_HZ)

/* SYSTIMER_CONF fields, per the C6 TRM. Named rather than written as bare bit
 * numbers because this function had `1 << 25` where it meant `1 << 23`:
 * bit 25 is TIMER_UNIT1_CORE1_STALL_EN, which on a single-core part does
 * nothing observable, so the comparator's work-enable was simply never set.
 * The interrupt was routed correctly all the way to the CPU and the peripheral
 * never raised it — read off a board as `raw=0x00000000` with TARGET1 enabled
 * in SYSTIMER_INT_ENA. A wrong bit number in a peripheral register is invisible
 * to every gate this repository has. */
#define SYSTIMER_TARGET1_WORK_EN_BIT 23
#define SYSTIMER_TARGET0_WORK_EN_BIT 24 /* ESP-IDF's; do not touch */
#define SYSTIMER_TIMER_UNIT0_WORK_EN 30 /* defaults to 1, ESP-IDF relies on it */

/* SYSTIMER_TARGET1_CONF fields. */
#define SYSTIMER_TARGET1_PERIOD_MODE 30
#define SYSTIMER_TARGET1_UNIT_SEL 31 /* 0 selects timer unit 0 */

reflex_tcb_t s_tasks[REFLEX_SCHED_MAX_TASKS];
static reflex_tcb_t *s_current = NULL;
static volatile uint32_t s_tick_count = 0;
static volatile uint32_t s_critical_nesting = 0;
static bool s_started = false;
static jmp_buf s_scheduler_context;

/* ---- Task management ---- */

static reflex_tcb_t *find_free_slot(void) {
    for (int i = 0; i < REFLEX_SCHED_MAX_TASKS; i++) {
        if (s_tasks[i].state == REFLEX_TASK_STATE_FREE) return &s_tasks[i];
    }
    return NULL;
}

reflex_err_t reflex_sched_create_task(void (*entry)(void *), const char *name,
                                      uint32_t stack_bytes, void *arg,
                                      int priority, reflex_tcb_t **out_tcb) {
    if (!entry) return REFLEX_ERR_INVALID_ARG;
    if (stack_bytes < REFLEX_SCHED_MIN_STACK) stack_bytes = REFLEX_SCHED_MIN_STACK;

    reflex_tcb_t *tcb = find_free_slot();
    if (!tcb) return REFLEX_ERR_NO_MEM;

    uint32_t *stack = malloc(stack_bytes);
    if (!stack) return REFLEX_ERR_NO_MEM;

    tcb->stack_base = stack;
    tcb->stack_size = stack_bytes;
    tcb->priority = priority;
    /* Bounded copy, always terminated. */
    if (name) {
        size_t n = strlen(name);
        if (n >= sizeof(tcb->name)) n = sizeof(tcb->name) - 1;
        memcpy(tcb->name, name, n);
        tcb->name[n] = '\0';
    } else {
        tcb->name[0] = '\0';
    }
    tcb->state = REFLEX_TASK_STATE_READY;
    tcb->wake_tick = 0;
    tcb->blocked_on = NULL;
    tcb->wake_deadline_valid = false;
    tcb->entry = entry;
    tcb->arg = arg;
    tcb->started = false;

    if (out_tcb) *out_tcb = tcb;
    return REFLEX_OK;
}

/* Free the stacks of tasks that have retired, and return their slots.
 *
 * Separate from reflex_sched_delete_task because a task cannot release its own
 * stack: it is standing on it. The self-delete path therefore marked the task
 * DEAD, yielded, and left both the stack and the slot behind for the rest of
 * the boot — and self-delete is the normal way to retire, which is how
 * reflex_vm_task_entry ends. A board that started and stopped VM tasks ran out
 * of slots and heap without ever calling anything that looked like a leak.
 *
 * Safe to free here precisely because it runs from the scheduler, after the
 * longjmp has already moved execution off the retiring task's stack and onto
 * the scheduler's own.
 *
 * Takes the array rather than reading the file-scope one so the host suite can
 * exercise it; the scheduler loop is the only caller that is not a test. */
bool reflex_sched_slot_info(int index, reflex_sched_slot_t *out) {
    if (!out || index < 0 || index >= REFLEX_SCHED_MAX_TASKS) return false;
    const reflex_tcb_t *t = &s_tasks[index];
    out->state = t->state;
    out->name = t->name;
    out->priority = t->priority;
    out->stack_size = t->stack_size;
    out->wake_tick = t->wake_tick;
    out->has_stack = (t->stack_base != NULL);
    out->started = t->started;
    out->wake_deadline_valid = t->wake_deadline_valid;
    out->is_current = (t == s_current);
    return true;
}

void reflex_sched_reap(reflex_tcb_t *tasks, int count) {
    if (!tasks) return;
    for (int i = 0; i < count; i++) {
        if (tasks[i].state != REFLEX_TASK_STATE_DEAD) continue;
        free(tasks[i].stack_base);
        tasks[i].stack_base = NULL;
        tasks[i].stack_size = 0;
        /* Cleared, not merely marked FREE. A slot is reused by
         * reflex_sched_create_task, and a stale `started` would send the
         * scheduler down the resume path into a jmp_buf belonging to a task
         * that no longer exists. */
        tasks[i].started = false;
        tasks[i].blocked_on = NULL;
        tasks[i].wake_deadline_valid = false;
        tasks[i].wake_tick = 0;
        tasks[i].name[0] = '\0';
        tasks[i].state = REFLEX_TASK_STATE_FREE;
    }
}

void reflex_sched_delete_task(reflex_tcb_t *tcb) {
    /* Naming the current task explicitly is the same as passing NULL.
     *
     * Handled here rather than falling through to the free below, which would
     * release the stack the caller is executing on. */
    if (!tcb || tcb == s_current) {
        if (s_current) {
            s_current->state = REFLEX_TASK_STATE_DEAD;
            /* Does not return: the scheduler will not pick a DEAD task, and
             * reflex_sched_reap collects the stack once we are off it. */
            reflex_sched_yield();
        }
        return;
    }

    tcb->state = REFLEX_TASK_STATE_DEAD;
    if (!s_started) {
        /* No scheduler to do the reaping, so do it here. With nothing running,
         * nothing can be standing on this stack. */
        reflex_sched_reap(tcb, 1);
    }
}

/* ---- Scheduler core ---- */

int reflex_sched_find_index(const reflex_tcb_t *tasks, int count, const char *name) {
    if (!tasks || count <= 0 || !name) return -1;
    for (int i = 0; i < count; i++) {
        /* A freed or dead slot keeps whatever name pointer it last held —
         * create_task never clears it and delete_task only changes state — so
         * skipping by state is what stops a lookup resolving to a corpse and
         * handing the caller a TCB that is about to be reused. */
        if (tasks[i].state == REFLEX_TASK_STATE_FREE || tasks[i].state == REFLEX_TASK_STATE_DEAD) {
            continue;
        }
        if (tasks[i].name[0] && strcmp(tasks[i].name, name) == 0) return i;
    }
    return -1;
}

reflex_tcb_t *reflex_sched_find_by_name(const char *name) {
    int idx = reflex_sched_find_index(s_tasks, REFLEX_SCHED_MAX_TASKS, name);
    return (idx < 0) ? NULL : &s_tasks[idx];
}

void reflex_sched_set_priority(reflex_tcb_t *t, int priority) {
    if (!t) return;
    t->priority = priority;
    /* No reschedule here. The change takes effect at the next scheduling
     * decision, which is the next tick or yield — raising your own priority
     * does not preempt anyone mid-call. */
}

int reflex_sched_get_priority(const reflex_tcb_t *t) {
    return t ? t->priority : 0;
}

int reflex_sched_select(const reflex_tcb_t *tasks, int count, int start) {
    if (!tasks || count <= 0) return -1;
    /* Normalise rather than trust: a caller computing `current + 1` off the end
     * of the table would otherwise index out of range on the first probe. */
    int begin = start % count;
    if (begin < 0) begin += count;

    int best = -1;
    for (int i = 0; i < count; i++) {
        int idx = (begin + i) % count;
        if (tasks[idx].state != REFLEX_TASK_STATE_READY) continue;
        /* Strictly greater, so the first task reached at a given priority wins.
         * With the scan starting after the current task, that is what makes
         * equal priorities round-robin instead of always picking the lowest
         * slot. */
        if (best < 0 || tasks[idx].priority > tasks[best].priority) best = idx;
    }
    return best;
}

/* Only reflex_sched_start consumes this, and that is compiled out of the host
 * build, so scope it the same way rather than leave an unused-function warning. */
#ifndef REFLEX_HOST_BUILD
static reflex_tcb_t *pick_next(void) {
    /* Collect anything that retired since the last decision. Here because this
     * is the first point after a task yields at which execution is back on the
     * scheduler's stack and a retired task's stack is provably idle. */
    reflex_sched_reap(s_tasks, REFLEX_SCHED_MAX_TASKS);
    if (s_current && s_current->state == REFLEX_TASK_STATE_FREE) {
        s_current = NULL; /* it was just reaped; do not round-robin from it */
    }

    for (int i = 0; i < REFLEX_SCHED_MAX_TASKS; i++) {
        /* Time only wakes tasks that asked for a deadline. That includes a
         * queue wait with a timeout — excluding queue waiters here would mean
         * such a wait could only ever end by a peer acting, never by expiring.
         * An untimed waiter has wake_deadline_valid false and is left for its
         * peer. */
        if (reflex_sched_should_time_wake(&s_tasks[i], s_tick_count)) {
            s_tasks[i].blocked_on = NULL;
            s_tasks[i].wake_deadline_valid = false;
            s_tasks[i].state = REFLEX_TASK_STATE_READY;
        }
    }

    int start = s_current ? (int)(s_current - s_tasks) + 1 : 0;
    int idx = reflex_sched_select(s_tasks, REFLEX_SCHED_MAX_TASKS, start);
    return (idx < 0) ? NULL : &s_tasks[idx];
}
#endif /* !REFLEX_HOST_BUILD */

#ifndef REFLEX_HOST_BUILD
/* Defined below, next to the scheduler loop that also uses it. */
static void setup_systimer_tick(void);

/* Handle for the routed systimer interrupt, so it can be given back. */
static reflex_intr_handle_t s_tick_intr = NULL;

static void sched_tick_isr(void *arg) {
    (void)arg;
    reflex_sched_tick();
    reflex_sched_ack_tick();
}

reflex_err_t reflex_sched_tick_start(void) {
    if (s_tick_intr) return REFLEX_OK; /* already running */

    /* Route first, then arm the comparator.
     *
     * The reverse order was tried and is wrong in a way that stops the core.
     * A routed line with an unarmed source is inert — nothing asserts it. An
     * armed source with no routing is not: until reflex_hal_intr_alloc writes
     * the interrupt-matrix entry, SYSTIMER_TARGET1's entry holds its reset
     * value of 0, so the comparator asserts onto CPU interrupt line 0 rather
     * than the line Reflex is about to claim. Level-triggered, so it stays
     * asserted, and the core stops making progress. Observed as a task
     * watchdog timeout with the idle task starved. */
    reflex_err_t rc = reflex_hal_intr_alloc(REFLEX_INTR_SRC_SYSTIMER_TARGET1, 0, sched_tick_isr,
                                            NULL, &s_tick_intr);
    if (rc != REFLEX_OK) {
        /* Nothing to unwind: with routing first, the comparator has not been
         * armed and the peripheral interrupt has not been enabled. The old
         * code cleared SYSTIMER_INT_ENA here, which made sense when arming
         * came first and is now only a misleading write to a bit this function
         * never set. */
        s_tick_intr = NULL;
        return rc;
    }

    setup_systimer_tick();
    return REFLEX_OK;
}

void reflex_sched_tick_stop(void) {
    /* Peripheral first, for the same reason: never leave a routed line with an
     * armed source and no handler. */
    REFLEX_REG(SYSTIMER_INT_ENA) &= ~(1u << 1);
    /* The comparator is deliberately left running. Only its interrupt stops.
     *
     * This used to clear TARGET1_WORK_EN as well, on the reasonable-sounding
     * grounds that leaving it set keeps the comparator matching and re-raising
     * into SYSTIMER_INT_RAW forever. It does — and stopping it cost far more
     * than that, because it is what made the tick work exactly once per boot:
     * the first `kernel tick` of a boot measured 998 Hz and every later one
     * measured 2 Hz, a single tick and then nothing.
     *
     * The reason is in REAL_TARGET1, the alarm the hardware actually matches
     * on, which no readback exposed until one was added for this. In period
     * mode COMP1_LOAD does not set the alarm to "now + period" — it adds one
     * period to whatever REAL_TARGET1 already holds, and clearing
     * TARGET1_WORK_EN freezes it. So a re-arm after a stop armed the comparator
     * one period past a timestamp from the previous run: measured at
     * -19,182,014 ticks, some 1.2 seconds in the past. An alarm behind the
     * counter matches once and never again.
     *
     * Left running, REAL_TARGET1 keeps tracking, and the accumulate lands about
     * two thousand ticks ahead of the counter every time: four consecutive
     * arms in one boot now measure 998, 998, 1000, 1000 Hz.
     *
     * Two fixes that look obviously right were tried on hardware first and both
     * failed, because both stayed inside period mode where nothing can rebase
     * the accumulator: clearing TARGET1_WORK_EN before reconfiguring (ESP-IDF's
     * systimer_hal_set_alarm_period ordering), and re-latching COMP1_LOAD after
     * enabling. A third — a one-shot absolute load of `now + period` to rebase,
     * then a switch back to period mode — did move REAL_TARGET1, and broke the
     * auto-reload instead: 0 Hz on the first arm, the alarm frozen where it was
     * placed. None of them are worth re-attempting.
     *
     * What this costs, stated so it reads as a decision rather than an
     * oversight: the comparator keeps running while Reflex is not using the
     * tick, setting INT_RAW at 1 kHz into a status bit nobody reads. Nothing is
     * delivered — INT_ENA is cleared above, the routed line is given back with
     * the handle below, and setup_systimer_tick clears INT_CLR before it
     * re-enables — so what is released is everything that can reach the core. */
    REFLEX_REG(SYSTIMER_INT_CLR) = (1u << 1);
    if (s_tick_intr) {
        reflex_hal_intr_free(s_tick_intr);
        s_tick_intr = NULL;
    }
}
#endif /* !REFLEX_HOST_BUILD */

void reflex_sched_tick_debug_conf(uint32_t *conf, uint32_t *target1_conf, uint32_t *int_clr) {
#ifndef REFLEX_HOST_BUILD
    if (conf) *conf = REFLEX_REG(SYSTIMER_CONF);
    if (target1_conf) *target1_conf = REFLEX_REG(SYSTIMER_TARGET1_CONF);
    /* What a read of the write-only INT_CLR returns.
     *
     * Not curiosity: ESP-IDF's systimer_ll_clear_alarm_int is
     * `dev->int_clr.val |= 1 << alarm_id`, a read-modify-write of a write-only
     * write-1-to-clear register. If that read returns the pending status rather
     * than zero, then every esp_timer interrupt writes back — and so clears —
     * whatever else was pending, Reflex's TARGET1 included. That would explain a
     * tick whose comparator matches and reloads correctly while its status bit
     * reads clear and no tick is ever counted. If it returns zero, the theory is
     * dead and this says so. */
    if (int_clr) *int_clr = REFLEX_REG(SYSTIMER_INT_CLR);
#else
    if (conf) *conf = 0;
    if (target1_conf) *target1_conf = 0;
    if (int_clr) *int_clr = 0;
#endif
}

uint32_t reflex_sched_tick_debug_sample_raw(uint32_t samples) {
#ifndef REFLEX_HOST_BUILD
    /* How often TARGET1's raw status is actually found set.
     *
     * A comparator matching at 1 kHz sets this bit every millisecond, and only
     * an acknowledgement clears it. So with no tick being counted, a bit that
     * reads set in nearly every sample means nobody is acknowledging it —
     * the interrupt is not reaching a handler. A bit that reads clear in nearly
     * every sample means somebody else is acknowledging it, which is a wholly
     * different fault with a wholly different suspect. One sample cannot tell
     * those apart; that is why this counts. */
    uint32_t set = 0;
    for (uint32_t i = 0; i < samples; i++) {
        if (REFLEX_REG(SYSTIMER_INT_RAW) & (1u << 1)) set++;
    }
    return set;
#else
    (void)samples;
    return 0;
#endif
}

void reflex_sched_tick_debug_target(uint64_t *unit_now, uint64_t *real_target, uint64_t *target) {
#ifndef REFLEX_HOST_BUILD
    REFLEX_REG(REFLEX_SYSTIMER_UNIT0_OP_REG) = REFLEX_SYSTIMER_UNIT0_UPDATE;
    while (!(REFLEX_REG(REFLEX_SYSTIMER_UNIT0_OP_REG) & REFLEX_SYSTIMER_UNIT0_VALUE_VALID)) {
    }
    if (unit_now) {
        *unit_now = ((uint64_t)REFLEX_REG(SYSTIMER_UNIT0_VALUE_HI) << 32) |
                    REFLEX_REG(SYSTIMER_UNIT0_VALUE_LO);
    }
    if (real_target) {
        *real_target = ((uint64_t)REFLEX_REG(SYSTIMER_REAL_TARGET1_HI) << 32) |
                       REFLEX_REG(SYSTIMER_REAL_TARGET1_LO);
    }
    if (target) {
        *target =
            ((uint64_t)REFLEX_REG(SYSTIMER_TARGET1_HI) << 32) | REFLEX_REG(SYSTIMER_TARGET1_LO);
    }
#else
    if (unit_now) *unit_now = 0;
    if (real_target) *real_target = 0;
    if (target) *target = 0;
#endif
}

void reflex_sched_tick_debug(uint32_t *ena, uint32_t *raw, uint32_t *st) {
#ifndef REFLEX_HOST_BUILD
    if (ena) *ena = REFLEX_REG(SYSTIMER_INT_ENA);
    if (raw) *raw = REFLEX_REG(SYSTIMER_INT_RAW);
    if (st) *st = REFLEX_REG(SYSTIMER_INT_ST);
#else
    if (ena) *ena = 0;
    if (raw) *raw = 0;
    if (st) *st = 0;
#endif
}

void reflex_sched_tick(void) {
    s_tick_count++;
}

void reflex_sched_ack_tick(void) {
    REFLEX_REG(SYSTIMER_INT_CLR) = (1 << 1); /* Clear TARGET1 interrupt */
}

void reflex_sched_yield(void) {
    if (!s_started) return;

    if (s_current) {
        /* A task yielding voluntarily returns to READY. One that has already
         * set its own state — BLOCKED via reflex_sched_delay_ms, or DEAD via
         * reflex_sched_delete_task(NULL) — keeps that state. */
        if (s_current->state == REFLEX_TASK_STATE_RUNNING) {
            s_current->state = REFLEX_TASK_STATE_READY;
        }

        /* Save unconditionally. The context save used to sit inside the
         * `state == RUNNING` test above, so reflex_sched_delay_ms — which
         * marks itself BLOCKED *before* yielding — never reached setjmp at
         * all. Its jmp_buf stayed zero-initialised, and when pick_next later
         * moved it back to READY the scheduler longjmp'd into a zeroed buffer.
         * Delaying is the primary blocking primitive, so this made any task
         * that slept unresumable.
         *
         * setjmp returns 0 on save and non-zero when longjmp lands here. */
        if (setjmp(s_current->context) != 0) {
            /* We've been restored — continue from where we yielded */
            return;
        }
    }

    /* Return to the scheduler loop */
    longjmp(s_scheduler_context, 1);
}

void reflex_sched_delay_ms(uint32_t ms) {
    if (!s_started || !s_current) return;
    uint32_t ticks = reflex_sched_ms_to_ticks(ms);
    if (ticks == 0) ticks = 1;
    s_current->wake_tick = s_tick_count + ticks;
    s_current->wake_deadline_valid = true;
    s_current->state = REFLEX_TASK_STATE_BLOCKED;
    reflex_sched_yield();
}

uint32_t reflex_sched_get_tick(void) {
    return s_tick_count;
}

uint32_t reflex_sched_ms_to_ticks(uint32_t ms) {
    /* 64-bit intermediate: ms * REFLEX_SCHED_TICK_HZ overflows a uint32 above
     * ms = 4,294,967, and the wrapped result is a *shorter* interval than was
     * asked for — so the failure is a timeout that fires early rather than one
     * that never fires. */
    uint64_t ticks = ((uint64_t)ms * REFLEX_SCHED_TICK_HZ) / 1000u;

    /* The clamp is unreachable at the current tick rate, and deliberately kept.
     *
     * At REFLEX_SCHED_TICK_HZ == 1000 the conversion is the identity, so the
     * largest uint32 input produces the largest uint32 output and nothing can
     * exceed the range. Mutation testing says as much: deleting this line does
     * not fail any test, because no input at this tick rate can reach it.
     *
     * It is defence against the one change that makes it live. At 2000 Hz the
     * same input yields 8,589,934,590, and without the clamp that truncates to
     * a *shorter* interval — the early-timeout failure above, reintroduced by
     * a Kconfig edit rather than by touching this file. The static assertion
     * below is what actually notices; the clamp is what survives it. */
    _Static_assert(REFLEX_SCHED_TICK_HZ <= 1000,
                   "Above 1000 Hz the clamp in reflex_sched_ms_to_ticks stops "
                   "being unreachable — check its tests still cover the range");
    return (ticks > UINT32_MAX) ? UINT32_MAX : (uint32_t)ticks;
}

bool reflex_sched_should_time_wake(const reflex_tcb_t *t, uint32_t now) {
    if (!t) return false;
    if (t->state != REFLEX_TASK_STATE_BLOCKED) return false;
    /* No deadline asked for: this task waits for a peer, not for the clock. */
    if (!t->wake_deadline_valid) return false;
    return reflex_sched_tick_reached(now, t->wake_tick);
}

bool reflex_sched_tick_reached(uint32_t now, uint32_t deadline) {
    /* Unsigned subtraction read as signed. Correct across the counter wrap for
     * any interval under half its range; see the header for the bound. */
    return (int32_t)(now - deadline) >= 0;
}

reflex_tcb_t *reflex_sched_get_current(void) {
    return s_current;
}

/* ---- Critical sections ---- */

void reflex_sched_enter_critical(void) {
#ifndef REFLEX_HOST_BUILD
    __asm__ volatile ("csrci mstatus, 0x8");
#endif
    s_critical_nesting++;
}

void reflex_sched_exit_critical(void) {
    if (s_critical_nesting > 0) s_critical_nesting--;
    if (s_critical_nesting == 0) {
#ifndef REFLEX_HOST_BUILD
        __asm__ volatile ("csrsi mstatus, 0x8");
#endif
    }
}

/* ---- Timer tick setup ---- */

#ifndef REFLEX_HOST_BUILD
static void setup_systimer_tick(void) {
    /* Configure, latch, then enable — the order ESP-IDF's systimer_hal uses.
     *
     * Two departures from it were tried on hardware and both were worse.
     * Enabling the comparator before latching COMP1_LOAD dropped the first
     * arming from 1000 Hz to a single tick; stopping the comparator first did
     * not help either. This sequence is the one that measures 1000 Hz, and it
     * is not a guess. */
    REFLEX_REG(SYSTIMER_TARGET1_CONF) = (1u << SYSTIMER_TARGET1_PERIOD_MODE) | SYSTIMER_TICK_PERIOD;
    REFLEX_REG(SYSTIMER_COMP1_LOAD) = 1; /* latch the period into the comparator */

    /* Enable the TARGET1 comparator itself. This is the line that was writing
     * bit 25, which is TIMER_UNIT1_CORE1_STALL_EN and does nothing here. */
    REFLEX_REG(SYSTIMER_CONF) |= (1u << SYSTIMER_TARGET1_WORK_EN_BIT);

    /* Clear any match that accumulated before the comparator was armed, so
     * enabling the interrupt does not immediately deliver a stale one. */
    REFLEX_REG(SYSTIMER_INT_CLR) = (1 << 1);
    REFLEX_REG(SYSTIMER_INT_ENA) |= (1 << 1);
}
#endif

/* ---- Init and start ---- */

/* The idle task sleeps, then gives the scheduler its turn back.
 *
 * The yield is the whole point of this function and it was missing. Without it
 * the loop is `while (1) { wfi; }`, which never returns to the scheduler — and
 * because reflex_sched_init always creates this task at priority 0, it is
 * always READY, so pick_next never returns NULL and the scheduler never reaches
 * its own idle path either. The first task to block therefore ended the
 * system: idle was picked, idle never came back, pick_next never ran again,
 * and the sweep that wakes timed sleepers lives inside pick_next.
 *
 * That is the whole of the long-standing "tasks run once and never wake from
 * delay_ms". Every part of the waking machinery was correct — the tick, the
 * volatile counter, reflex_sched_should_time_wake and its host tests, the sweep
 * itself — and none of it was reachable, because the lowest-priority task was a
 * black hole. Measured, not inferred: a probe on pick_next printed exactly once
 * per boot, at the first scheduling decision, and never again.
 *
 * wfi first, so a tick with nothing to do costs one wakeup rather than a spin;
 * yield second, so the scheduler re-runs pick_next, sweeps the deadlines, and
 * hands the CPU to whichever task that woke. When nothing is ready, pick_next
 * chooses idle again and this parks on wfi once more — one pass per interrupt,
 * which is what an idle task is for. */
static void idle_task(void *arg) {
    (void)arg;
    while (1) {
#ifndef REFLEX_HOST_BUILD
        __asm__ volatile("wfi");
#endif
        reflex_sched_yield();
    }
}

reflex_err_t reflex_sched_init(void) {
    memset(s_tasks, 0, sizeof(s_tasks));
    s_current = NULL;
    s_tick_count = 0;
    s_critical_nesting = 0;
    s_started = false;
    return reflex_sched_create_task(idle_task, "idle", REFLEX_SCHED_MIN_STACK,
                                    NULL, 0, NULL);
}

#ifdef REFLEX_HOST_BUILD
reflex_err_t reflex_sched_start(void) { return REFLEX_OK; }
#else
reflex_err_t reflex_sched_start(void) {
    /* Route the tick, do not merely arm it.
     *
     * This called setup_systimer_tick() directly, which enables the comparator
     * and the peripheral interrupt and stops there — nothing maps the source
     * through the interrupt matrix, sets a PLIC priority, or enables the mie
     * bit. Until reflex_hal_intr_alloc writes it, SYSTIMER_TARGET1's matrix
     * entry holds its reset value of 0, so the comparator asserts onto CPU
     * interrupt line 0. Level-triggered, so it stays asserted and the core
     * stops making progress; observed exactly that way from the shell path as
     * a task watchdog timeout with the idle task starved.
     *
     * reflex_sched_tick_start does the routing first and then arms, and that
     * path is measured — but not on both builds, which an earlier version of
     * this comment claimed. Re-measured 2026-09-08 with `make tick-measure`:
     * 5/5 verified cold starts at exactly 1000 Hz on the independence build
     * (802.15.4), and 0/5 on the default ESP-NOW build, where the routing
     * reads back entirely correct — matrix entry, PLIC enable, priority above
     * threshold, mie set, SYSTIMER asserting and latched — and nothing is
     * delivered. The console's interrupt was ruled out as the cause: it sits
     * on CPU line 10, the tick on 11. The Wi-Fi stack's own interrupt use is
     * the obvious next suspect and has not been investigated.
     *
     * The independence path is the one Tier C targets, so this does not block
     * the cutover; it does mean the tick cannot be exercised from the default
     * build. Sharing the code means the scheduler's tick and the diagnostic's
     * tick are the same, so `kernel tick` exercises what the scheduler will
     * use.
     *
     * A failure here is fatal to the scheduler rather than cosmetic: with no
     * tick, the loop below runs its first task and parks on wfi the moment
     * everything blocks, with nothing left to wake it. */
    reflex_err_t tick_rc = reflex_sched_tick_start();
    if (tick_rc != REFLEX_OK) {
        return tick_rc;
    }
    s_started = true;

    /* The scheduler loop: pick a task, run it until it yields,
     * then pick the next one. longjmp from yield returns here. */
    while (1) {
        if (setjmp(s_scheduler_context) == 0) {
            /* First time or after picking a new task */
        }
        /* A task yielded back to us, or we're starting fresh */

        reflex_tcb_t *next = pick_next();
        if (!next) {
            /* No ready tasks — wait for the tick to unblock one.
             *
             * No instrumentation here.
             *
             * Two attempts failed and both are worth knowing about.
             * esp_rom_printf busy-waits on the USB FIFO, so printing from this
             * loop can hang the loop being measured. Writing to LP_AON scratch
             * cannot block and survives a reset, which is the right shape — but
             * STORE2 and STORE3 are not ours: tagged values written here read
             * back as zero after a reset, so something in ROM or the boot path
             * owns them. The tags are what caught it; without them the zeros
             * looked exactly like a real measurement, and "interrupts are
             * masked in the scheduler loop" was very nearly reported as a
             * finding on the strength of two clobbered registers. */
            __asm__ volatile ("wfi");
            continue;
        }

        s_current = next;
        next->state = REFLEX_TASK_STATE_RUNNING;

        if (!next->started) {
            /* First time running this task — switch to its stack
             * and call the entry function. When the entry returns,
             * mark the task dead and yield back. */
            next->started = true;
            register uint32_t new_sp = (uint32_t)(next->stack_base + next->stack_size / sizeof(uint32_t));
            /* Align sp to 16 bytes (RISC-V ABI requirement) */
            new_sp &= ~0xF;
            __asm__ volatile ("mv sp, %0" : : "r"(new_sp) : "memory");
            next->entry(next->arg);
            next->state = REFLEX_TASK_STATE_DEAD;
            /* Return to scheduler */
            longjmp(s_scheduler_context, 1);
        } else {
            /* Resume a previously yielded task */
            longjmp(next->context, 1);
        }
    }

    return REFLEX_OK;
}
#endif /* !REFLEX_HOST_BUILD */

/* ---- Blocking queue operations ---- */

/* Wake one task waiting on @p q. Called after an operation that could satisfy
 * a waiter: a send makes an item available, a receive makes room.
 *
 * One, not all. Waking every waiter for a single freed slot means they all run,
 * all but one find the queue unchanged, and all but one block again — a
 * thundering herd that costs a context switch each. The woken task is chosen by
 * scan order, which is round-robin over the task table rather than a fairness
 * guarantee; if starvation ever matters, this is the place to make it FIFO.
 *
 * Waking "a waiter" rather than "a waiter of the right kind" is safe because
 * senders and receivers cannot both be parked on one queue at once: a send only
 * blocks when the queue is full and a receive only when it is empty, and a
 * queue of non-zero capacity cannot be both. If a zero-capacity rendezvous
 * queue is ever allowed, that invariant goes and this must distinguish them.
 *
 * Callers hold the critical section. */
static void wake_one_waiter(const void *q) {
    for (int i = 0; i < REFLEX_SCHED_MAX_TASKS; i++) {
        if (s_tasks[i].state == REFLEX_TASK_STATE_BLOCKED && s_tasks[i].blocked_on == q) {
            s_tasks[i].blocked_on = NULL;
            s_tasks[i].wake_deadline_valid = false;
            s_tasks[i].state = REFLEX_TASK_STATE_READY;
            return;
        }
    }
}

/* Shared body of send and recv: attempt, and if it fails, park until a peer
 * acts or the deadline passes.
 *
 * The attempt happens inside the critical section and the check for "did it
 * work" happens before yielding, which is what keeps this free of the classic
 * lost-wakeup: a peer cannot slip an item in between our test and our block,
 * because both are under the same lock. */
/* One of send_item / recv_item is non-NULL and selects the direction. The
 * earlier shape took a single void* plus a bool and had to launder away const
 * at the send call site, which is exactly the kind of cast that outlives the
 * reason for it. */
static reflex_err_t queue_wait(reflex_kqueue_t *q, const void *send_item, void *recv_item,
                               uint32_t timeout_ms) {
    const bool sending = (send_item != NULL);
    if (!q) return REFLEX_ERR_INVALID_ARG;

    const bool forever = (timeout_ms == REFLEX_SCHED_WAIT_FOREVER);
    uint32_t deadline = 0;
    if (!forever) {
        deadline = s_tick_count + reflex_sched_ms_to_ticks(timeout_ms);
    }

    for (;;) {
        reflex_sched_enter_critical();
        bool done =
            sending ? reflex_kqueue_try_send(q, send_item) : reflex_kqueue_try_recv(q, recv_item);
        if (done) {
            /* A successful send may have unblocked a receiver and vice versa. */
            wake_one_waiter(q);
            reflex_sched_exit_critical();
            return REFLEX_OK;
        }

        /* Nothing to yield to before the scheduler runs, and no peer can make
         * progress, so waiting would hang the boot rather than delay it. */
        if (!s_started || !s_current) {
            reflex_sched_exit_critical();
            return REFLEX_ERR_TIMEOUT;
        }

        if (!forever && reflex_sched_tick_reached(s_tick_count, deadline)) {
            reflex_sched_exit_critical();
            return REFLEX_ERR_TIMEOUT;
        }

        s_current->blocked_on = q;
        s_current->wake_tick = deadline;
        /* A timed wait must still expire if no peer ever comes; an untimed one
         * must not be woken by the clock at all. */
        s_current->wake_deadline_valid = !forever;
        s_current->state = REFLEX_TASK_STATE_BLOCKED;
        reflex_sched_exit_critical();

        reflex_sched_yield();

        /* Back on the CPU, from either a peer's wake or the deadline. Clear the
         * marker and retry: a woken task is not guaranteed to win the race for
         * the item against a task that was already READY. */
        s_current->blocked_on = NULL;
        s_current->wake_deadline_valid = false;
    }
}

reflex_err_t reflex_sched_queue_send(struct reflex_kqueue *q, const void *item,
                                     uint32_t timeout_ms) {
    if (!item) return REFLEX_ERR_INVALID_ARG;
    return queue_wait(q, item, NULL, timeout_ms);
}

reflex_err_t reflex_sched_queue_recv(struct reflex_kqueue *q, void *item, uint32_t timeout_ms) {
    return queue_wait(q, NULL, item, timeout_ms);
}
