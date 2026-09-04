/**
 * @file reflex_kqueue.c
 * @brief Fixed-capacity message queue. See reflex_kqueue.h.
 */

#include "reflex_kqueue.h"

#include <stdlib.h>
#include <string.h>

/* Control blocks are a static pool rather than heap-allocated. The pool bounds
 * how many queues can exist, which is what a kernel wants to know at build
 * time; only the item storage is dynamic, and only once, at creation. */
static reflex_kqueue_t s_queues[REFLEX_KQUEUE_MAX];

reflex_kqueue_t *reflex_kqueue_create(uint32_t length, uint32_t item_size) {
    if (length == 0 || item_size == 0) return NULL;

    /* Reject a capacity whose byte size would wrap. length and item_size are
     * both caller-supplied and both 32-bit, so their product overflows well
     * before the allocation fails — and an overflowed size allocates a small
     * buffer that every subsequent send then writes past. */
    if (length > UINT32_MAX / item_size) return NULL;

    reflex_kqueue_t *q = NULL;
    for (int i = 0; i < REFLEX_KQUEUE_MAX; i++) {
        if (!s_queues[i].in_use) {
            q = &s_queues[i];
            break;
        }
    }
    if (!q) return NULL;

    uint8_t *storage = malloc((size_t)length * item_size);
    if (!storage) return NULL; /* pool slot stays free: in_use not yet set */

    q->storage = storage;
    q->length = length;
    q->item_size = item_size;
    q->count = 0;
    q->head = 0;
    q->tail = 0;
    q->in_use = true;
    return q;
}

void reflex_kqueue_destroy(reflex_kqueue_t *q) {
    if (!q) return;
    free(q->storage);
    /* Zero the whole block, not just in_use. A stale length or storage pointer
     * left behind is the kind of thing a later use-after-free reads as valid. */
    memset(q, 0, sizeof(*q));
}

bool reflex_kqueue_try_send(reflex_kqueue_t *q, const void *item) {
    if (!q || !q->storage || !item) return false;
    if (q->count >= q->length) return false;

    memcpy(q->storage + ((size_t)q->head * q->item_size), item, q->item_size);
    q->head = (q->head + 1) % q->length;
    q->count++;
    return true;
}

bool reflex_kqueue_try_recv(reflex_kqueue_t *q, void *item) {
    if (!q || !q->storage) return false;
    if (q->count == 0) return false;

    /* A NULL item discards the head rather than refusing: callers draining a
     * queue should not have to supply a scratch buffer they will not read. */
    if (item) {
        memcpy(item, q->storage + ((size_t)q->tail * q->item_size), q->item_size);
    }
    q->tail = (q->tail + 1) % q->length;
    q->count--;
    return true;
}

uint32_t reflex_kqueue_count(const reflex_kqueue_t *q) {
    return q ? q->count : 0;
}

/* NULL reads as full and as empty at the same time, which is not a
 * contradiction: nothing can be sent to it and nothing can be received from
 * it. Both callers are asking "will my operation succeed", and the answer is
 * no either way. */
bool reflex_kqueue_is_full(const reflex_kqueue_t *q) {
    return q ? (q->count >= q->length) : true;
}

bool reflex_kqueue_is_empty(const reflex_kqueue_t *q) {
    return q ? (q->count == 0) : true;
}

void reflex_kqueue_reset(reflex_kqueue_t *q) {
    if (!q) return;
    q->count = 0;
    q->head = 0;
    q->tail = 0;
}
