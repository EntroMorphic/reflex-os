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
    uint32_t *sp;
    reflex_task_state_t state;
    int priority;
    uint32_t wake_tick;
    const char *name;
    uint32_t *stack_base;
    uint32_t stack_size;
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

void reflex_sched_enter_critical(void);
void reflex_sched_exit_critical(void);

#ifdef __cplusplus
}
#endif

#endif /* REFLEX_SCHED_H */
