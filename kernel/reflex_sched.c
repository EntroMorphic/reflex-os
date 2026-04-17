/**
 * @file reflex_sched.c
 * @brief Reflex OS preemptive scheduler — RISC-V implementation.
 *
 * Preemptive priority-based round-robin scheduler. The SYSTIMER
 * generates a periodic tick interrupt that calls the scheduler.
 * Context switching is done via assembly helpers that save/restore
 * the full RISC-V GP register set + mepc + mstatus.
 */

#include "reflex_sched.h"
#include <string.h>
#include <stdlib.h>

/* SYSTIMER registers for tick generation (Gap H) */
#define SYSTIMER_BASE           0x60004000
#define SYSTIMER_CONF           (SYSTIMER_BASE + 0x00)
#define SYSTIMER_UNIT0_OP       (SYSTIMER_BASE + 0x04)
#define SYSTIMER_UNIT0_VAL_LO   (SYSTIMER_BASE + 0x44)
#define SYSTIMER_UNIT0_VAL_HI   (SYSTIMER_BASE + 0x40)
#define SYSTIMER_TARGET0_LO     (SYSTIMER_BASE + 0x20)
#define SYSTIMER_TARGET0_HI     (SYSTIMER_BASE + 0x1C)
#define SYSTIMER_TARGET0_CONF   (SYSTIMER_BASE + 0x34)
#define SYSTIMER_COMP0_LOAD     (SYSTIMER_BASE + 0x50)
#define SYSTIMER_INT_ENA        (SYSTIMER_BASE + 0x64)
#define SYSTIMER_INT_CLR        (SYSTIMER_BASE + 0x6C)
#define REG32(addr) (*(volatile uint32_t *)(addr))

/* XTAL = 40 MHz. For 1 kHz tick: period = 40000 */
#define SYSTIMER_TICK_PERIOD    (40000000 / REFLEX_SCHED_TICK_HZ)

/* ---- Global scheduler state ---- */

static reflex_tcb_t s_tasks[REFLEX_SCHED_MAX_TASKS];
static reflex_tcb_t *s_current = NULL;
static volatile uint32_t s_tick_count = 0;
static volatile uint32_t s_critical_nesting = 0;
static bool s_started = false;

/* Context frame layout (must match the assembly save/restore).
 * 32 GP registers (x1-x31, x0 is hardwired zero) + mepc + mstatus = 33 words.
 * We save x1(ra) through x31, then mepc and mstatus. */
#define CONTEXT_WORDS 33

/* ---- Task management ---- */

static reflex_tcb_t *find_free_slot(void) {
    for (int i = 0; i < REFLEX_SCHED_MAX_TASKS; i++) {
        if (s_tasks[i].state == REFLEX_TASK_STATE_FREE) return &s_tasks[i];
    }
    return NULL;
}

static void task_wrapper(void (*entry)(void *), void *arg) {
    entry(arg);
    /* If the task returns, mark it dead */
    if (s_current) s_current->state = REFLEX_TASK_STATE_DEAD;
    reflex_sched_yield();
    while (1) {} /* Should never reach */
}

/* Initialize a task's stack frame so that context_restore will
 * "return" into task_wrapper(entry, arg). */
static void init_stack_frame(reflex_tcb_t *tcb, void (*entry)(void *), void *arg) {
    uint32_t *sp = tcb->stack_base + (tcb->stack_size / sizeof(uint32_t));
    sp -= CONTEXT_WORDS;

    memset(sp, 0, CONTEXT_WORDS * sizeof(uint32_t));

    /* The context frame layout (indices from sp):
     * [0]  = x1 (ra) — return address = task_wrapper
     * [1]  = x2 (sp) — not used (sp is the frame pointer)
     * [2]  = x3 (gp) — global pointer (set by linker)
     * ...
     * [9]  = x10 (a0) — first argument = entry
     * [10] = x11 (a1) — second argument = arg
     * ...
     * [31] = mepc — program counter to resume at = task_wrapper
     * [32] = mstatus — machine status (interrupts enabled) */

    sp[0]  = (uint32_t)task_wrapper;      /* ra */
    sp[9]  = (uint32_t)entry;             /* a0 = entry */
    sp[10] = (uint32_t)arg;               /* a1 = arg */
    sp[31] = (uint32_t)task_wrapper;      /* mepc */
    sp[32] = 0x00001880;                  /* mstatus: MIE=1 (bit 3), MPIE=1 (bit 7), MPP=machine (bits 11-12) */

    tcb->sp = sp;
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

    init_stack_frame(tcb, entry, arg);

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
    /* Unblock tasks whose delay has expired */
    for (int i = 0; i < REFLEX_SCHED_MAX_TASKS; i++) {
        if (s_tasks[i].state == REFLEX_TASK_STATE_BLOCKED &&
            s_tick_count >= s_tasks[i].wake_tick) {
            s_tasks[i].state = REFLEX_TASK_STATE_READY;
        }
    }

    /* Find highest-priority ready task (round-robin within same priority) */
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

/* Called from the timer tick ISR or from yield. Performs the context
 * switch. In a real implementation, this would be in assembly.
 * For the initial integration, we use setjmp/longjmp-style switching
 * via the GCC __builtin_* or a small assembly trampoline. */

/* For now: cooperative scheduling via explicit yield points.
 * The preemptive tick will invoke this from the ISR context.
 * Full preemptive context switch requires assembly — see
 * reflex_context.S (Dependency 10). */

void reflex_sched_tick(void) {
    s_tick_count++;
}

void reflex_sched_yield(void) {
    if (!s_started) return;

    reflex_tcb_t *next = pick_next();
    if (!next || next == s_current) return;

    if (s_current && s_current->state == REFLEX_TASK_STATE_RUNNING) {
        s_current->state = REFLEX_TASK_STATE_READY;
    }

    next->state = REFLEX_TASK_STATE_RUNNING;
    reflex_tcb_t *prev = s_current;
    s_current = next;

    /* Context switch: save prev->sp, restore next->sp.
     * This is where assembly context_switch(prev, next) would go.
     * For cooperative mode, the C compiler's function call convention
     * handles the callee-saved registers. We need assembly for
     * preemptive (interrupt-driven) switching. */
    if (prev) {
        /* Cooperative switch using inline assembly */
        __asm__ volatile (
            "sw sp, 0(%0)"     /* save current sp to prev->sp */
            : : "r"(&prev->sp) : "memory"
        );
    }
    __asm__ volatile (
        "lw sp, 0(%0)"        /* restore sp from next->sp */
        : : "r"(&next->sp) : "memory"
    );
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

/* ---- Critical sections ---- */

void reflex_sched_enter_critical(void) {
    __asm__ volatile ("csrci mstatus, 0x8"); /* Clear MIE (bit 3) */
    s_critical_nesting++;
}

void reflex_sched_exit_critical(void) {
    if (s_critical_nesting > 0) s_critical_nesting--;
    if (s_critical_nesting == 0) {
        __asm__ volatile ("csrsi mstatus, 0x8"); /* Set MIE (bit 3) */
    }
}

/* ---- Init and start ---- */

/* Idle task — runs when no other task is ready */
static void idle_task(void *arg) {
    (void)arg;
    while (1) {
        __asm__ volatile ("wfi"); /* Wait for interrupt */
    }
}

reflex_err_t reflex_sched_init(void) {
    memset(s_tasks, 0, sizeof(s_tasks));
    s_current = NULL;
    s_tick_count = 0;
    s_critical_nesting = 0;
    s_started = false;

    /* Create the idle task at lowest priority */
    return reflex_sched_create_task(idle_task, "idle", REFLEX_SCHED_MIN_STACK,
                                    NULL, 0, NULL);
}

static void setup_systimer_tick(void) {
    /* Enable SYSTIMER clock */
    REG32(SYSTIMER_CONF) |= (1 << 0);  /* CLK_FO = force on */

    /* Configure TARGET0 in periodic mode */
    REG32(SYSTIMER_TARGET0_CONF) = (1 << 30)     /* period mode */
                                 | SYSTIMER_TICK_PERIOD;
    REG32(SYSTIMER_COMP0_LOAD) = 1;              /* load the config */

    /* Enable TARGET0 work */
    REG32(SYSTIMER_CONF) |= (1 << 24);           /* TARGET0_WORK_EN */

    /* Enable TARGET0 interrupt */
    REG32(SYSTIMER_INT_CLR) = (1 << 0);           /* clear pending */
    REG32(SYSTIMER_INT_ENA) |= (1 << 0);          /* enable TARGET0 int */
}

/* Called from the trap handler to acknowledge the tick */
void reflex_sched_ack_tick(void) {
    REG32(SYSTIMER_INT_CLR) = (1 << 0);
}

reflex_err_t reflex_sched_start(void) {
    reflex_tcb_t *first = pick_next();
    if (!first) return REFLEX_ERR_INVALID_STATE;

    setup_systimer_tick();
    s_started = true;
    first->state = REFLEX_TASK_STATE_RUNNING;
    s_current = first;

    /* Load the first task's stack and jump to it.
     * This never returns. */
    __asm__ volatile (
        "lw sp, 0(%0)\n"      /* Load sp from first task's TCB */
        "lw ra, 0(sp)\n"      /* Load ra from context frame */
        "ret\n"                /* Jump to ra (task_wrapper) */
        : : "r"(&first->sp) : "memory"
    );

    /* Should never reach */
    while (1) {}
    return REFLEX_OK;
}
