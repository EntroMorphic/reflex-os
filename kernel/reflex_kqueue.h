/**
 * @file reflex_kqueue.h
 * @brief Fixed-capacity message queue for the Reflex scheduler.
 *
 * The one primitive `reflex_sched.h` was missing. `reflex_task.h` requires
 * queues, `core/fabric.c` and `core/event_bus.c` both use them, and until now
 * the only implementation behind that interface was FreeRTOS's — which is why
 * `reflex_task_kernel.c` still delegates there.
 *
 * The ring itself is deliberately separate from any notion of blocking. Every
 * function here is synchronous, allocation-free after creation, and has no
 * dependency on the scheduler at all, which means the part where the bugs
 * actually live — wraparound, fullness, the empty/full ambiguity at
 * `head == tail` — is exercised by the host suite on every commit rather than
 * only on hardware. Blocking send and receive are built on top of these in
 * `reflex_sched.c`, where the scheduler is.
 *
 * That split follows `platform/reflex_log_format.c`, which was extracted from
 * the logging path for the same reason and proved by mutation.
 *
 * Named `kqueue` rather than `queue` because `reflex_task.h` already declares
 * `reflex_queue_create` — the public interface, returning an opaque
 * `reflex_queue_handle_t`. Reusing that name here would be two different
 * functions with the same symbol and different return types, which is a
 * one-definition-rule violation the linker catches only once both land in the
 * same image. The layering is deliberate: `reflex_queue_*` is the interface
 * the OS calls, and once `reflex_task_kernel.c` stops delegating to FreeRTOS
 * it will be implemented in terms of the `reflex_kqueue_*` primitive here.
 *
 * Not interrupt-safe on its own. Callers that share a queue between a task and
 * an ISR must wrap these in `reflex_sched_enter_critical` /
 * `reflex_sched_exit_critical`; the blocking wrappers do.
 */
#ifndef REFLEX_KQUEUE_H
#define REFLEX_KQUEUE_H

#include "reflex_types.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Queue control blocks available. Matches the scale of REFLEX_SCHED_MAX_TASKS. */
/* Sized against what the system actually creates, which 8 was not.
 *
 * The census at the time of writing: reflex_fabric_init creates one inbox per
 * node — REFLEX_NODE_MAX, which is 8 — and the event bus creates one, so a
 * boot needs 9 before anything else asks. At 8 the fabric could not finish
 * initialising, and because the pool and a failed malloc both return NULL the
 * caller reported REFLEX_ERR_NO_MEM, which reads as "out of heap" rather than
 * "out of pool slots" on a board with 350 KiB free.
 *
 * That was the first thing to stop the boot once Reflex's own task backend was
 * running under Reflex's own entry point: it cost the fabric, the loom, the
 * services, the radio and the self-checks, and main.c dropped to a shell
 * without saying so.
 *
 * 16 leaves headroom for the same again. The control blocks are static and the
 * item storage is not, so the cost of the headroom is sizeof(reflex_kqueue_t)
 * per unused slot and nothing else. */
#define REFLEX_KQUEUE_MAX 16

typedef struct reflex_kqueue {
    uint8_t *storage;   /**< length * item_size bytes, owned by the queue. */
    uint32_t length;    /**< Capacity in items. */
    uint32_t item_size; /**< Bytes per item. */
    uint32_t count;     /**< Items currently held. */
    uint32_t head;      /**< Next slot to write. */
    uint32_t tail;      /**< Next slot to read. */
    bool in_use;        /**< Control block allocated. */
} reflex_kqueue_t;

/**
 * @brief Allocate a queue holding @p length items of @p item_size bytes.
 *
 * Storage comes from the heap once, at creation; send and receive never
 * allocate. Returns NULL if either argument is zero, if the control-block pool
 * is exhausted, or if the allocation fails.
 */
reflex_kqueue_t *reflex_kqueue_create(uint32_t length, uint32_t item_size);

/**
 * @brief Release a queue and its storage. Safe on NULL, and safe to repeat.
 *
 * Does **not** release tasks parked on the queue by
 * `reflex_sched_queue_send`/`_recv`. The control block is zeroed, so a waiter
 * that runs afterwards sees a queue with no storage and simply fails its
 * attempt — a timed wait then expires, but an untimed one waits forever for a
 * peer that can no longer exist.
 *
 * Latent rather than live: nothing in the OS destroys a queue today, since the
 * ones that exist are created during init and live for the uptime. If that
 * changes, destroy needs to wake every waiter first, and they need a way to
 * report that the queue went away rather than that they timed out.
 */
void reflex_kqueue_destroy(reflex_kqueue_t *q);

/**
 * @brief Copy one item in. Returns false if the queue is full or @p q is NULL.
 *
 * Never blocks and never partially writes: on a full queue the item is not
 * consumed and the caller can retry or give up.
 */
bool reflex_kqueue_try_send(reflex_kqueue_t *q, const void *item);

/**
 * @brief Copy one item out. Returns false if the queue is empty or @p q is NULL.
 *
 * @p item may be NULL to discard the head item rather than copy it.
 */
bool reflex_kqueue_try_recv(reflex_kqueue_t *q, void *item);

/** @return Items currently held, or 0 for NULL. */
uint32_t reflex_kqueue_count(const reflex_kqueue_t *q);

/** @return true when no further item fits. NULL reads as full: nothing fits. */
bool reflex_kqueue_is_full(const reflex_kqueue_t *q);

/** @return true when nothing can be received. NULL reads as empty. */
bool reflex_kqueue_is_empty(const reflex_kqueue_t *q);

/** @brief Discard all items. Capacity and storage are unchanged. */
void reflex_kqueue_reset(reflex_kqueue_t *q);

#ifdef __cplusplus
}
#endif

#endif /* REFLEX_KQUEUE_H */
