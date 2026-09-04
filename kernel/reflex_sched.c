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
#define SYSTIMER_INT_ENA        (SYSTIMER_BASE + 0x64)
#define SYSTIMER_INT_CLR (SYSTIMER_BASE + 0x6C)
#define SYSTIMER_TICK_PERIOD    (40000000 / REFLEX_SCHED_TICK_HZ)

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
    tcb->name = name;
    tcb->state = REFLEX_TASK_STATE_READY;
    tcb->wake_tick = 0;
    tcb->blocked_on = NULL;
    tcb->entry = entry;
    tcb->arg = arg;
    tcb->started = false;

    if (out_tcb) *out_tcb = tcb;
    return REFLEX_OK;
}

void reflex_sched_delete_task(reflex_tcb_t *tcb) {
    if (!tcb) {
        if (s_current) {
            s_current->state = REFLEX_TASK_STATE_DEAD;
            reflex_sched_yield();
        }
        return;
    }
    tcb->state = REFLEX_TASK_STATE_DEAD;
    if (tcb->stack_base) { free(tcb->stack_base); tcb->stack_base = NULL; }
    tcb->state = REFLEX_TASK_STATE_FREE;
}

/* ---- Scheduler core ---- */

/* Only reflex_sched_start consumes this, and that is compiled out of the host
 * build, so scope it the same way rather than leave an unused-function warning. */
#ifndef REFLEX_HOST_BUILD
static reflex_tcb_t *pick_next(void) {
    for (int i = 0; i < REFLEX_SCHED_MAX_TASKS; i++) {
        if (s_tasks[i].state == REFLEX_TASK_STATE_BLOCKED &&
            s_tick_count >= s_tasks[i].wake_tick) {
            s_tasks[i].state = REFLEX_TASK_STATE_READY;
        }
    }

    reflex_tcb_t *best = NULL;
    int start = s_current ? (int)(s_current - s_tasks) + 1 : 0;
    for (int i = 0; i < REFLEX_SCHED_MAX_TASKS; i++) {
        int idx = (start + i) % REFLEX_SCHED_MAX_TASKS;
        if (s_tasks[idx].state == REFLEX_TASK_STATE_READY) {
            if (!best || s_tasks[idx].priority > best->priority) {
                best = &s_tasks[idx];
            }
        }
    }
    return best;
}
#endif /* !REFLEX_HOST_BUILD */

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
    uint32_t ticks = (ms * REFLEX_SCHED_TICK_HZ) / 1000;
    if (ticks == 0) ticks = 1;
    s_current->wake_tick = s_tick_count + ticks;
    s_current->state = REFLEX_TASK_STATE_BLOCKED;
    reflex_sched_yield();
}

uint32_t reflex_sched_get_tick(void) {
    return s_tick_count;
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
    REFLEX_REG(SYSTIMER_CONF) |= (1 << 0);
    REFLEX_REG(SYSTIMER_TARGET1_CONF) = (1 << 30) | SYSTIMER_TICK_PERIOD;
    REFLEX_REG(SYSTIMER_COMP1_LOAD) = 1;
    REFLEX_REG(SYSTIMER_CONF) |= (1 << 25);
    REFLEX_REG(SYSTIMER_INT_CLR) = (1 << 1);
    REFLEX_REG(SYSTIMER_INT_ENA) |= (1 << 1);
}
#endif

/* ---- Init and start ---- */

static void idle_task(void *arg) {
    (void)arg;
    while (1) {
#ifndef REFLEX_HOST_BUILD
        __asm__ volatile ("wfi");
#endif
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
    setup_systimer_tick();
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
            /* No ready tasks — spin and wait for tick to unblock one */
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
 * Callers hold the critical section. */
static void wake_one_waiter(const void *q) {
    for (int i = 0; i < REFLEX_SCHED_MAX_TASKS; i++) {
        if (s_tasks[i].state == REFLEX_TASK_STATE_BLOCKED && s_tasks[i].blocked_on == q) {
            s_tasks[i].blocked_on = NULL;
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
static reflex_err_t queue_wait(reflex_kqueue_t *q, void *item, bool sending, uint32_t timeout_ms) {
    if (!q) return REFLEX_ERR_INVALID_ARG;

    const bool forever = (timeout_ms == REFLEX_SCHED_WAIT_FOREVER);
    uint32_t deadline = 0;
    if (!forever) {
        uint32_t ticks = (timeout_ms * REFLEX_SCHED_TICK_HZ) / 1000;
        deadline = s_tick_count + ticks;
    }

    for (;;) {
        reflex_sched_enter_critical();
        bool done = sending ? reflex_kqueue_try_send(q, item) : reflex_kqueue_try_recv(q, item);
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

        if (!forever && s_tick_count >= deadline) {
            reflex_sched_exit_critical();
            return REFLEX_ERR_TIMEOUT;
        }

        /* UINT32_MAX rather than 0 for an untimed wait: pick_next promotes any
         * BLOCKED task whose wake_tick has passed, so a zero here would be
         * woken on the very next scheduling decision and spin. */
        s_current->blocked_on = q;
        s_current->wake_tick = forever ? UINT32_MAX : deadline;
        s_current->state = REFLEX_TASK_STATE_BLOCKED;
        reflex_sched_exit_critical();

        reflex_sched_yield();

        /* Back on the CPU, from either a peer's wake or the deadline. Clear the
         * marker and retry: a woken task is not guaranteed to win the race for
         * the item against a task that was already READY. */
        s_current->blocked_on = NULL;
    }
}

reflex_err_t reflex_sched_queue_send(struct reflex_kqueue *q, const void *item,
                                     uint32_t timeout_ms) {
    if (!item) return REFLEX_ERR_INVALID_ARG;
    /* try_send takes a const pointer; the cast is discarded inside. */
    return queue_wait(q, (void *)(uintptr_t)item, true, timeout_ms);
}

reflex_err_t reflex_sched_queue_recv(struct reflex_kqueue *q, void *item, uint32_t timeout_ms) {
    return queue_wait(q, item, false, timeout_ms);
}
