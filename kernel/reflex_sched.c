/**
 * @file reflex_sched.c
 * @brief Reflex OS cooperative scheduler — setjmp/longjmp based.
 *
 * Each task has a jmp_buf for saving/restoring callee-saved registers.
 * New tasks are started by switching sp to their stack and calling
 * the entry function. Yielding saves the current context via setjmp
 * and restores the scheduler context via longjmp.
 */

#include "reflex_sched.h"
#include <string.h>
#include <stdlib.h>

/* SYSTIMER registers for tick generation */
#define SYSTIMER_BASE           0x60004000
#define SYSTIMER_CONF           (SYSTIMER_BASE + 0x00)
#define SYSTIMER_TARGET1_CONF   (SYSTIMER_BASE + 0x38)
#define SYSTIMER_COMP1_LOAD     (SYSTIMER_BASE + 0x54)
#define SYSTIMER_INT_ENA        (SYSTIMER_BASE + 0x64)
#define SYSTIMER_INT_CLR        (SYSTIMER_BASE + 0x6C)
#define REG32(addr) (*(volatile uint32_t *)(addr))
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

void reflex_sched_tick(void) {
    s_tick_count++;
}

void reflex_sched_ack_tick(void) {
    REG32(SYSTIMER_INT_CLR) = (1 << 1);  /* Clear TARGET1 interrupt */
}

void reflex_sched_yield(void) {
    if (!s_started) return;

    /* Save current task's context. setjmp returns 0 on save,
     * non-zero when longjmp restores us here. */
    if (s_current && s_current->state == REFLEX_TASK_STATE_RUNNING) {
        s_current->state = REFLEX_TASK_STATE_READY;
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
    REG32(SYSTIMER_CONF) |= (1 << 0);
    REG32(SYSTIMER_TARGET1_CONF) = (1 << 30) | SYSTIMER_TICK_PERIOD;
    REG32(SYSTIMER_COMP1_LOAD) = 1;
    REG32(SYSTIMER_CONF) |= (1 << 25);
    REG32(SYSTIMER_INT_CLR) = (1 << 1);
    REG32(SYSTIMER_INT_ENA) |= (1 << 1);
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
