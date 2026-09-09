/**
 * @file reflex_sched.h
 * @brief Reflex OS preemptive scheduler for RISC-V.
 *
 * A minimal preemptive scheduler that implements the reflex_task.h
 * interface. Tasks are preempted by the SYSTIMER tick interrupt.
 * Priority-based round-robin within each priority level.
 *
 * This is the Reflex OS kernel scheduler — not FreeRTOS, not Zephyr,
 * not any third-party RTOS. Every line is ours.
 */

#ifndef REFLEX_SCHED_H
#define REFLEX_SCHED_H

#include "reflex_types.h"
#include <stdbool.h>
#include <stdint.h>
#include <setjmp.h>

#ifdef __cplusplus
extern "C" {
#endif

#define REFLEX_SCHED_MAX_TASKS   16
#define REFLEX_SCHED_TICK_HZ     1000
#define REFLEX_SCHED_MIN_STACK   1024

typedef enum {
    REFLEX_TASK_STATE_FREE = 0,
    REFLEX_TASK_STATE_READY,
    REFLEX_TASK_STATE_RUNNING,
    REFLEX_TASK_STATE_BLOCKED,
    REFLEX_TASK_STATE_DEAD,
} reflex_task_state_t;

typedef struct reflex_tcb {
    jmp_buf context;
    reflex_task_state_t state;
    int priority;
    uint32_t wake_tick;
    const char *name;
    uint32_t *stack_base;
    uint32_t stack_size;
    void (*entry)(void *);
    void *arg;
    bool started;
    /* The queue this task is waiting on, or NULL. BLOCKED alone cannot say
     * whether a task is sleeping until a deadline or waiting for data, and a
     * queue waiter has to be findable by the peer that satisfies it. */
    void *blocked_on;
    /* Whether wake_tick means anything.
     *
     * "No deadline" cannot be encoded as a wake_tick sentinel. UINT32_MAX
     * fails under the wrap-safe comparison — (int32_t)(0 - UINT32_MAX) is 1,
     * so an untimed waiter reads as already expired and wakes immediately —
     * and excluding queue waiters from the time sweep instead would mean a
     * *timed* queue wait could never time out, only ever be woken by a peer.
     * Both cases need saying, so the deadline gets its own flag. */
    bool wake_deadline_valid;
} reflex_tcb_t;

reflex_err_t reflex_sched_init(void);
reflex_err_t reflex_sched_start(void);

reflex_err_t reflex_sched_create_task(void (*entry)(void *), const char *name,
                                      uint32_t stack_bytes, void *arg,
                                      int priority, reflex_tcb_t **out_tcb);
void reflex_sched_delete_task(reflex_tcb_t *tcb);

/** @brief Free the stacks of DEAD tasks and return their slots to FREE.
 *
 * A task cannot release its own stack while standing on it, so retiring is two
 * steps: the task marks itself DEAD and yields, and the scheduler reaps it once
 * execution is back on the scheduler's stack. Exposed so the host suite can
 * exercise it. */
void reflex_sched_reap(reflex_tcb_t *tasks, int count);
void reflex_sched_delay_ms(uint32_t ms);
void reflex_sched_yield(void);

uint32_t reflex_sched_get_tick(void);

/**
 * @brief Convert milliseconds to scheduler ticks, saturating rather than wrapping.
 *
 * The obvious `(ms * REFLEX_SCHED_TICK_HZ) / 1000` overflows a uint32 for any
 * `ms` above 4,294,967 — at 1000 Hz a 5,000,000 ms timeout came out as 705,032
 * ticks, so a timeout of just under 84 minutes fired after 12. Computed in 64
 * bits and clamped, so a too-large request becomes "as long as this can
 * express" instead of a short one.
 *
 * Exported because it is worth testing, and it cannot be tested through the
 * scheduler: the host build never runs tasks.
 */
uint32_t reflex_sched_ms_to_ticks(uint32_t ms);

/**
 * @brief Has the tick counter reached @p deadline, accounting for wraparound?
 *
 * `s_tick_count >= deadline` is wrong across the wrap. At 1000 Hz the counter
 * wraps every 49.7 days, and a task whose deadline landed just before the wrap
 * would compare a small current tick against a huge deadline and never be
 * woken — a sleep that silently becomes permanent.
 *
 * The subtraction is unsigned and its result read as signed, which is the
 * standard technique: correct for any interval shorter than half the counter's
 * range, so deadlines up to about 24.8 days behave. Beyond that no comparison
 * can distinguish "long past" from "far future" with one counter.
 */
bool reflex_sched_tick_reached(uint32_t now, uint32_t deadline);

/**
 * @brief Should the deadline sweep move @p t back to READY at tick @p now?
 *
 * Extracted from pick_next because pick_next is not compiled on the host — the
 * scheduler never runs there — which left this decision untestable, and it is
 * subtle enough to have been got wrong. An earlier revision excluded every
 * task with `blocked_on` set, on the reasoning that queue waiters are woken by
 * their peers. That silently removed the timeout from a *timed* queue wait: it
 * could then only ever end when a peer acted, never by expiring. Three states
 * have to be distinguished — sleeping on a deadline, waiting on a queue with a
 * deadline, and waiting on a queue without one — and the first two wake here.
 */
bool reflex_sched_should_time_wake(const reflex_tcb_t *t, uint32_t now);

/**
 * @brief Choose the next task to run: index into @p tasks, or -1 if none.
 *
 * The scheduler's central decision, and until now the least examined thing in
 * the kernel — it lived inside pick_next, which is not compiled on the host,
 * so no test has ever selected a task.
 *
 * Highest priority wins. Among equal priorities the scan starts at @p start
 * and wraps, which is what makes it round-robin rather than always returning
 * the lowest-numbered slot: passing the current task's index plus one is what
 * stops one task of a priority level starving its equals.
 *
 * @param tasks  Task table.
 * @param count  Entries in @p tasks.
 * @param start  Slot to begin scanning from; wrapped into range.
 */
int reflex_sched_select(const reflex_tcb_t *tasks, int count, int start);

/**
 * @brief Index of the live task named @p name, or -1.
 *
 * FREE and DEAD slots are skipped: a slot retains its name pointer after the
 * task is gone, so searching the raw table would resolve a name to a corpse.
 * Split from the lookup below so the search itself is testable — the table is
 * a parameter rather than the scheduler's private state.
 */
int reflex_sched_find_index(const reflex_tcb_t *tasks, int count, const char *name);

/** @brief The live task named @p name, or NULL. */
reflex_tcb_t *reflex_sched_find_by_name(const char *name);

/** @brief Set a task's scheduling priority. NULL is ignored. */
void reflex_sched_set_priority(reflex_tcb_t *t, int priority);

/** @return The task's priority, or 0 for NULL. */
int reflex_sched_get_priority(const reflex_tcb_t *t);

/* ---- Scheduler tick ----
 *
 * Routes SYSTIMER TARGET1 to the CPU and starts the 1 kHz tick, using Reflex's
 * own interrupt plumbing (`reflex_hal_intr_alloc`) rather than ESP-IDF's
 * allocator. That is the difference between a scheduler that ticks and a
 * scheduler that ticks *because the framework we are replacing arranged it*.
 *
 * Separate from reflex_sched_start so the tick can be proved on hardware
 * without also handing the machine to a scheduler that nothing yet starts.
 * Target-only: it programs registers.
 */
#ifndef REFLEX_HOST_BUILD
reflex_err_t reflex_sched_tick_start(void);
void reflex_sched_tick_stop(void);

/**
 * @brief Read back the tick peripheral's interrupt state.
 *
 * Exists so a diagnostic can report what the comparator is doing without
 * duplicating SYSTIMER offsets. The shell used to hardcode base+0x64, +0x68 and
 * +0x70 in its own file, which is the same register map written down twice and
 * the kind of duplication that goes stale silently — the offsets already live
 * beside the code that programs them.
 *
 * @param ena  SYSTIMER_INT_ENA, or NULL
 * @param raw  SYSTIMER_INT_RAW, or NULL   (TARGET1 is bit 1 in each)
 * @param st   SYSTIMER_INT_ST,  or NULL
 */
void reflex_sched_tick_debug(uint32_t *ena, uint32_t *raw, uint32_t *st);

/** @brief Read back the comparator's own configuration.
 *
 * The interrupt registers say whether the source fired; they say nothing about
 * whether the comparator is still armed to fire again. A re-arm that yields
 * exactly one tick and then stops is invisible in @ref reflex_sched_tick_debug
 * and visible here — and what it showed is that the comparator is not the
 * cause: a working arm and a failing one read identically. */
void reflex_sched_tick_debug_conf(uint32_t *conf, uint32_t *target1_conf, uint32_t *int_clr);

/** @brief Count how many of @p samples find TARGET1's raw status set.
 *
 * Distinguishes "nobody is acknowledging the interrupt" from "somebody else
 * is", which a single sample cannot do and which have different suspects. */
uint32_t reflex_sched_tick_debug_sample_raw(uint32_t samples);

/** @brief Read the comparator's live alarm target against the live counter.
 *
 * The last piece of comparator state that no register read so far exposed. In
 * period mode the hardware recomputes the alarm on every match, so this says
 * whether the comparator is waiting for a moment that will arrive or one that
 * has already passed — which a working and a failing arm cannot both be. */
void reflex_sched_tick_debug_target(uint64_t *unit_now, uint64_t *real_target, uint64_t *target);
#endif

/* ---- Trap-side tick routing (reflex_trap.c) ----
 *
 * Which CPU interrupt line the scheduler tick arrives on is decided by the
 * interrupt matrix, not by the architecture, so the trap handler has to be
 * told. Until it is, it claims no interrupt as the tick: guessing a line
 * number services some other peripheral's interrupt as though it were the
 * tick and leaves that peripheral asserted forever.
 */
void reflex_trap_install(void);

/** @brief Capture `mtvec` and `mscratch` before @ref reflex_trap_install.
 *
 * Taking the trap vector is the one step of the hand-off that cannot be undone
 * without knowing what was there before. Capturing it is what lets a failure
 * after the install hand the machine back instead of stranding it. */
void reflex_trap_snapshot(uint32_t *mtvec_out, uint32_t *mscratch_out);

/** @brief Put `mtvec` and `mscratch` back as @ref reflex_trap_snapshot found
 *  them, returning trap handling to whoever owned it before. */
void reflex_trap_restore(uint32_t mtvec_val, uint32_t mscratch_val);
void reflex_trap_set_tick_line(int cpu_int);
int reflex_trap_get_tick_line(void);
void reflex_sched_tick(void);
void reflex_sched_ack_tick(void);
reflex_tcb_t *reflex_sched_get_current(void);

void reflex_sched_enter_critical(void);
void reflex_sched_exit_critical(void);

/* ---- Blocking queue operations ----
 *
 * The ring itself is in reflex_kqueue.h and knows nothing about tasks. These
 * add the waiting: a full send or an empty receive parks the caller until a
 * peer makes room or supplies an item, or until @p timeout_ms elapses.
 *
 * REFLEX_SCHED_WAIT_FOREVER waits without a deadline. Before the scheduler is
 * started there is nothing to yield to, so both degrade to a single attempt
 * and report REFLEX_ERR_TIMEOUT rather than spinning forever during boot.
 */
#define REFLEX_SCHED_WAIT_FOREVER UINT32_MAX

struct reflex_kqueue;

/** @return REFLEX_OK, or REFLEX_ERR_TIMEOUT if no room appeared in time. */
reflex_err_t reflex_sched_queue_send(struct reflex_kqueue *q, const void *item,
                                     uint32_t timeout_ms);

/** @return REFLEX_OK, or REFLEX_ERR_TIMEOUT if no item arrived in time. */
reflex_err_t reflex_sched_queue_recv(struct reflex_kqueue *q, void *item, uint32_t timeout_ms);

#ifdef __cplusplus
}
#endif

#endif /* REFLEX_SCHED_H */
