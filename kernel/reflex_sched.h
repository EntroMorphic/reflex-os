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
    /* The queue this task is waiting on, or NULL. BLOCKED alone is not enough
     * to tell "sleeping until a deadline" from "waiting for data": pick_next
     * wakes a BLOCKED task once s_tick_count >= wake_tick, so a queue waiter
     * needs this to be found and woken by its peer, and an untimed wait sets
     * wake_tick to UINT32_MAX so the deadline never arrives on its own. */
    void *blocked_on;
} reflex_tcb_t;

reflex_err_t reflex_sched_init(void);
reflex_err_t reflex_sched_start(void);

reflex_err_t reflex_sched_create_task(void (*entry)(void *), const char *name,
                                      uint32_t stack_bytes, void *arg,
                                      int priority, reflex_tcb_t **out_tcb);
void reflex_sched_delete_task(reflex_tcb_t *tcb);
void reflex_sched_delay_ms(uint32_t ms);
void reflex_sched_yield(void);

uint32_t reflex_sched_get_tick(void);
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
