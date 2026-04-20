#ifndef REFLEX_TASK_H
#define REFLEX_TASK_H

/**
 * @file reflex_task.h
 * @brief Reflex OS portable task, queue, and critical-section primitives.
 *
 * Platform implementations live in platform/<target>/. The substrate
 * and VM use only these interfaces for concurrency — never FreeRTOS
 * or pthreads directly.
 */

#include "reflex_types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef void *reflex_task_handle_t;
typedef void *reflex_queue_handle_t;

/* Opaque mutex storage. Sized for ESP-IDF's portMUX_TYPE (8 bytes).
 * Backends with larger primitives (e.g., pthreads) should heap-allocate
 * inside reflex_mutex_init and store a pointer in _opaque[0]. The
 * _Static_assert in each backend enforces the size contract at compile
 * time. */
typedef struct {
    uint32_t _opaque[2];
} reflex_mutex_t;

/* --- Tasks --- */

/** @param out_handle Receives the task handle (may be NULL if caller doesn't need it). */
reflex_err_t reflex_task_create(void (*fn)(void *), const char *name,
                                uint32_t stack_bytes, void *arg, int priority,
                                reflex_task_handle_t *out_handle);
/** @param handle Task to delete; NULL deletes the calling task. */
void reflex_task_delete(reflex_task_handle_t handle);
void reflex_task_delay_ms(uint32_t ms);
void reflex_task_yield(void);
/** @return Handle or NULL if not found. */
reflex_task_handle_t reflex_task_get_by_name(const char *name);
void reflex_task_set_priority(reflex_task_handle_t handle, int priority);
int reflex_task_get_priority(reflex_task_handle_t handle);

/* --- Queues --- */

reflex_queue_handle_t reflex_queue_create(uint32_t length, uint32_t item_size);
reflex_err_t reflex_queue_send(reflex_queue_handle_t q, const void *item,
                               uint32_t timeout_ms);
reflex_err_t reflex_queue_recv(reflex_queue_handle_t q, void *item,
                               uint32_t timeout_ms);

/* --- Critical sections --- */

reflex_mutex_t reflex_mutex_init(void);
void reflex_critical_enter(reflex_mutex_t *m);
void reflex_critical_exit(reflex_mutex_t *m);

#ifdef __cplusplus
}
#endif

#endif /* REFLEX_TASK_H */
