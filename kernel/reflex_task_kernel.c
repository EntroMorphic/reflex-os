/**
 * @file reflex_task_kernel.c
 * @brief reflex_task.h implementation backed by the Reflex kernel scheduler.
 *
 * This replaces reflex_task_esp32c6.c (FreeRTOS backend) when
 * CONFIG_REFLEX_KERNEL_SCHEDULER is enabled.
 */

#include "reflex_task.h"
#include "reflex_sched.h"
#include <string.h>

/* ---- Queue implementation (simple ring buffer) ---- */

typedef struct {
    uint8_t *buf;
    uint32_t item_size;
    uint32_t capacity;
    volatile uint32_t head;
    volatile uint32_t tail;
    volatile uint32_t count;
} reflex_queue_impl_t;

/* ---- Tasks ---- */

reflex_err_t reflex_task_create(void (*fn)(void *), const char *name,
                                uint32_t stack_bytes, void *arg, int priority,
                                reflex_task_handle_t *out_handle) {
    reflex_tcb_t *tcb = NULL;
    reflex_err_t rc = reflex_sched_create_task(fn, name, stack_bytes, arg, priority, &tcb);
    if (rc != REFLEX_OK) return rc;
    if (out_handle) *out_handle = (reflex_task_handle_t)tcb;
    return REFLEX_OK;
}

void reflex_task_delete(reflex_task_handle_t handle) {
    reflex_sched_delete_task((reflex_tcb_t *)handle);
}

void reflex_task_delay_ms(uint32_t ms) {
    reflex_sched_delay_ms(ms);
}

void reflex_task_yield(void) {
    reflex_sched_yield();
}

reflex_task_handle_t reflex_task_get_by_name(const char *name) {
    if (!name) return NULL;
    extern reflex_tcb_t s_tasks[];
    for (int i = 0; i < REFLEX_SCHED_MAX_TASKS; i++) {
        if (s_tasks[i].state != REFLEX_TASK_STATE_FREE &&
            s_tasks[i].name && strcmp(s_tasks[i].name, name) == 0) {
            return (reflex_task_handle_t)&s_tasks[i];
        }
    }
    return NULL;
}

void reflex_task_set_priority(reflex_task_handle_t handle, int priority) {
    if (handle) ((reflex_tcb_t *)handle)->priority = priority;
}

int reflex_task_get_priority(reflex_task_handle_t handle) {
    if (!handle) return 0;
    return ((reflex_tcb_t *)handle)->priority;
}

/* ---- Queues ---- */

reflex_queue_handle_t reflex_queue_create(uint32_t length, uint32_t item_size) {
    reflex_queue_impl_t *q = malloc(sizeof(reflex_queue_impl_t));
    if (!q) return NULL;
    q->buf = malloc(length * item_size);
    if (!q->buf) { free(q); return NULL; }
    q->item_size = item_size;
    q->capacity = length;
    q->head = 0;
    q->tail = 0;
    q->count = 0;
    return (reflex_queue_handle_t)q;
}

reflex_err_t reflex_queue_send(reflex_queue_handle_t qh, const void *item,
                               uint32_t timeout_ms) {
    reflex_queue_impl_t *q = (reflex_queue_impl_t *)qh;
    if (!q || !item) return REFLEX_ERR_INVALID_ARG;

    uint32_t deadline = reflex_sched_get_tick() + (timeout_ms * REFLEX_SCHED_TICK_HZ / 1000);
    while (q->count >= q->capacity) {
        if (timeout_ms == 0) return REFLEX_ERR_TIMEOUT;
        if (reflex_sched_get_tick() >= deadline) return REFLEX_ERR_TIMEOUT;
        reflex_sched_yield();
    }

    reflex_sched_enter_critical();
    memcpy(q->buf + (q->head * q->item_size), item, q->item_size);
    q->head = (q->head + 1) % q->capacity;
    q->count++;
    reflex_sched_exit_critical();
    return REFLEX_OK;
}

reflex_err_t reflex_queue_recv(reflex_queue_handle_t qh, void *item,
                               uint32_t timeout_ms) {
    reflex_queue_impl_t *q = (reflex_queue_impl_t *)qh;
    if (!q || !item) return REFLEX_ERR_INVALID_ARG;

    uint32_t deadline = reflex_sched_get_tick() + (timeout_ms * REFLEX_SCHED_TICK_HZ / 1000);
    while (q->count == 0) {
        if (timeout_ms == 0) return REFLEX_ERR_NOT_FOUND;
        if (timeout_ms != UINT32_MAX && reflex_sched_get_tick() >= deadline)
            return REFLEX_ERR_NOT_FOUND;
        reflex_sched_yield();
    }

    reflex_sched_enter_critical();
    memcpy(item, q->buf + (q->tail * q->item_size), q->item_size);
    q->tail = (q->tail + 1) % q->capacity;
    q->count--;
    reflex_sched_exit_critical();
    return REFLEX_OK;
}

/* ---- Critical sections ---- */

reflex_mutex_t reflex_mutex_init(void) {
    reflex_mutex_t m = {{0}};
    return m;
}

void reflex_critical_enter(reflex_mutex_t *m) {
    (void)m;
    reflex_sched_enter_critical();
}

void reflex_critical_exit(reflex_mutex_t *m) {
    (void)m;
    reflex_sched_exit_critical();
}
