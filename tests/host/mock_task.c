/**
 * @file mock_task.c
 * @brief Mock implementation of reflex_task.h for host-side testing.
 *
 * Wraps the kernel scheduler (setjmp/longjmp works on host).
 * Queue and critical section implementations from reflex_task_kernel.c.
 */

#include "reflex_task.h"
#include <stdlib.h>
#include <string.h>

/* Simplified task for host — no real concurrency, just track state */
typedef struct {
    void (*fn)(void *);
    void *arg;
    const char *name;
    int priority;
    bool active;
} mock_task_t;

#define MAX_MOCK_TASKS 16
static mock_task_t s_tasks[MAX_MOCK_TASKS];
static int s_task_count = 0;

reflex_err_t reflex_task_create(void (*fn)(void *), const char *name,
                                uint32_t stack_bytes, void *arg, int priority,
                                reflex_task_handle_t *out_handle) {
    (void)stack_bytes;
    if (s_task_count >= MAX_MOCK_TASKS) return REFLEX_ERR_NO_MEM;
    mock_task_t *t = &s_tasks[s_task_count];
    t->fn = fn;
    t->arg = arg;
    t->name = name;
    t->priority = priority;
    t->active = true;
    if (out_handle) *out_handle = (reflex_task_handle_t)t;
    s_task_count++;
    return REFLEX_OK;
}

void reflex_task_delete(reflex_task_handle_t handle) {
    if (handle) ((mock_task_t *)handle)->active = false;
}

void reflex_task_delay_ms(uint32_t ms) { (void)ms; }
void reflex_task_yield(void) { }

reflex_task_handle_t reflex_task_get_by_name(const char *name) {
    for (int i = 0; i < s_task_count; i++) {
        if (s_tasks[i].active && s_tasks[i].name &&
            strcmp(s_tasks[i].name, name) == 0) {
            return (reflex_task_handle_t)&s_tasks[i];
        }
    }
    return NULL;
}

void reflex_task_set_priority(reflex_task_handle_t handle, int priority) {
    if (handle) ((mock_task_t *)handle)->priority = priority;
}

int reflex_task_get_priority(reflex_task_handle_t handle) {
    if (!handle) return 0;
    return ((mock_task_t *)handle)->priority;
}

/* Simple ring-buffer queue (same as kernel backend) */
typedef struct {
    uint8_t *buf;
    uint32_t item_size, capacity, head, tail, count;
} mock_queue_t;

reflex_queue_handle_t reflex_queue_create(uint32_t length, uint32_t item_size) {
    mock_queue_t *q = calloc(1, sizeof(mock_queue_t));
    if (!q) return NULL;
    q->buf = calloc(length, item_size);
    if (!q->buf) { free(q); return NULL; }
    q->item_size = item_size;
    q->capacity = length;
    return (reflex_queue_handle_t)q;
}

reflex_err_t reflex_queue_send(reflex_queue_handle_t qh, const void *item, uint32_t timeout_ms) {
    (void)timeout_ms;
    mock_queue_t *q = (mock_queue_t *)qh;
    if (!q || !item || q->count >= q->capacity) return REFLEX_ERR_TIMEOUT;
    memcpy(q->buf + (q->head * q->item_size), item, q->item_size);
    q->head = (q->head + 1) % q->capacity;
    q->count++;
    return REFLEX_OK;
}

reflex_err_t reflex_queue_recv(reflex_queue_handle_t qh, void *item, uint32_t timeout_ms) {
    (void)timeout_ms;
    mock_queue_t *q = (mock_queue_t *)qh;
    if (!q || !item || q->count == 0) return REFLEX_ERR_NOT_FOUND;
    memcpy(item, q->buf + (q->tail * q->item_size), q->item_size);
    q->tail = (q->tail + 1) % q->capacity;
    q->count--;
    return REFLEX_OK;
}

reflex_mutex_t reflex_mutex_init(void) {
    reflex_mutex_t m = {{0}};
    return m;
}

void reflex_critical_enter(reflex_mutex_t *m) { (void)m; }
void reflex_critical_exit(reflex_mutex_t *m) { (void)m; }
