/**
 * @file test_queue.c
 * @brief Tests for the Reflex scheduler's message queue.
 *
 * The queue is the primitive that stands between Reflex and its own scheduler:
 * `reflex_task.h` requires it, `core/fabric.c` and `core/event_bus.c` use it,
 * and until it existed the only implementation behind that interface was
 * FreeRTOS's. So it is worth more than a few hand-picked cases.
 *
 * The core of this file is a differential test against a reference FIFO built
 * from a plain array, mirroring the approach in `test_lattice.c`. Ring buffers
 * fail on interleavings rather than on individual operations — the classic one
 * being the ambiguity at `head == tail`, which reads as both empty and full
 * unless something else distinguishes them — and a fixed script of sends and
 * receives will happily miss the wraparound that breaks it. Randomised
 * operations checked against a reference after *every* step will not.
 *
 * The queue is deliberately free of scheduler dependencies, which is what makes
 * any of this testable on a host at all.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "reflex_kqueue.h"
#include "reflex_sched.h"

static int s_pass = 0, s_fail = 0;

#define CHECK(name, expr)                                                                          \
    do {                                                                                           \
        if (expr) {                                                                                \
            s_pass++;                                                                              \
        } else {                                                                                   \
            printf("  FAIL: %s\n", name);                                                          \
            s_fail++;                                                                              \
        }                                                                                          \
    } while (0)

/* ---- Reference model: a FIFO with none of the ring's cleverness ---- */

#define REF_MAX 64
typedef struct {
    uint32_t items[REF_MAX];
    uint32_t count;
} ref_fifo_t;

static bool ref_send(ref_fifo_t *r, uint32_t cap, uint32_t v) {
    if (r->count >= cap) return false;
    r->items[r->count++] = v;
    return true;
}

static bool ref_recv(ref_fifo_t *r, uint32_t *out) {
    if (r->count == 0) return false;
    *out = r->items[0];
    memmove(&r->items[0], &r->items[1], (r->count - 1) * sizeof(uint32_t));
    r->count--;
    return true;
}

/* ---- Creation and argument handling ---- */

static void test_create(void) {
    CHECK("zero length refused", reflex_kqueue_create(0, 4) == NULL);
    CHECK("zero item_size refused", reflex_kqueue_create(4, 0) == NULL);

    /* length * item_size must not wrap. An overflowed size allocates a small
     * buffer that every later send writes past. */
    CHECK("overflowing capacity refused", reflex_kqueue_create(0x80000000u, 4) == NULL);

    reflex_kqueue_t *q = reflex_kqueue_create(4, sizeof(uint32_t));
    CHECK("create succeeds", q != NULL);
    CHECK("new queue is empty", reflex_kqueue_is_empty(q));
    CHECK("new queue is not full", !reflex_kqueue_is_full(q));
    CHECK("new queue count is 0", reflex_kqueue_count(q) == 0);
    reflex_kqueue_destroy(q);
}

static void test_pool_exhaustion(void) {
    reflex_kqueue_t *qs[REFLEX_KQUEUE_MAX + 1];
    int made = 0;
    for (int i = 0; i < REFLEX_KQUEUE_MAX + 1; i++) {
        qs[i] = reflex_kqueue_create(2, 4);
        if (qs[i]) made++;
    }
    CHECK("pool caps at REFLEX_KQUEUE_MAX", made == REFLEX_KQUEUE_MAX);
    CHECK("one past the pool returns NULL", qs[REFLEX_KQUEUE_MAX] == NULL);
    for (int i = 0; i < REFLEX_KQUEUE_MAX + 1; i++)
        reflex_kqueue_destroy(qs[i]);

    /* Destroy must return slots, or the pool leaks across a reboot-free run. */
    reflex_kqueue_t *again = reflex_kqueue_create(2, 4);
    CHECK("destroy releases pool slots", again != NULL);
    reflex_kqueue_destroy(again);
}

/* ---- NULL handling ---- */

static void test_null_safety(void) {
    uint32_t v = 0;
    CHECK("send to NULL fails", !reflex_kqueue_try_send(NULL, &v));
    CHECK("recv from NULL fails", !reflex_kqueue_try_recv(NULL, &v));
    CHECK("count of NULL is 0", reflex_kqueue_count(NULL) == 0);
    /* Both true is intentional: neither operation can succeed. */
    CHECK("NULL reads as full", reflex_kqueue_is_full(NULL));
    CHECK("NULL reads as empty", reflex_kqueue_is_empty(NULL));
    reflex_kqueue_destroy(NULL); /* must not crash */
    CHECK("destroy(NULL) survives", true);

    reflex_kqueue_t *q = reflex_kqueue_create(2, 4);
    CHECK("send of NULL item fails", !reflex_kqueue_try_send(q, NULL));
    reflex_kqueue_destroy(q);
}

/* ---- Ordering, fullness, and the discard path ---- */

static void test_fifo_and_bounds(void) {
    reflex_kqueue_t *q = reflex_kqueue_create(3, sizeof(uint32_t));
    uint32_t a = 10, b = 20, c = 30, d = 40, out = 0;

    CHECK("send 1", reflex_kqueue_try_send(q, &a));
    CHECK("send 2", reflex_kqueue_try_send(q, &b));
    CHECK("send 3", reflex_kqueue_try_send(q, &c));
    CHECK("full after capacity reached", reflex_kqueue_is_full(q));
    CHECK("send past capacity refused", !reflex_kqueue_try_send(q, &d));
    CHECK("refused send did not change count", reflex_kqueue_count(q) == 3);

    CHECK("recv is FIFO 1", reflex_kqueue_try_recv(q, &out) && out == 10);
    CHECK("recv is FIFO 2", reflex_kqueue_try_recv(q, &out) && out == 20);
    /* NULL discards the head rather than refusing. */
    CHECK("recv(NULL) discards", reflex_kqueue_try_recv(q, NULL));
    CHECK("empty after draining", reflex_kqueue_is_empty(q));
    CHECK("recv on empty refused", !reflex_kqueue_try_recv(q, &out));

    reflex_kqueue_reset(q);
    CHECK("reset leaves it empty", reflex_kqueue_is_empty(q));
    reflex_kqueue_destroy(q);
}

/* ---- Item fidelity: anything wider than a word must survive intact ---- */

static void test_item_fidelity(void) {
    typedef struct {
        uint8_t tag;
        uint32_t a;
        uint16_t b;
        char s[7];
    } item_t;
    reflex_kqueue_t *q = reflex_kqueue_create(2, sizeof(item_t));
    item_t in = {.tag = 0xA5, .a = 0xDEADBEEF, .b = 0x1234, .s = "reflex"};
    item_t out;
    memset(&out, 0, sizeof(out));

    CHECK("struct send", reflex_kqueue_try_send(q, &in));
    CHECK("struct recv", reflex_kqueue_try_recv(q, &out));
    CHECK("struct survives byte-for-byte", memcmp(&in, &out, sizeof(item_t)) == 0);
    reflex_kqueue_destroy(q);
}

/* ---- Wraparound: the case a fixed script misses ---- */

static void test_wraparound(void) {
    reflex_kqueue_t *q = reflex_kqueue_create(4, sizeof(uint32_t));
    uint32_t out = 0;
    bool ok = true;

    /* Send and receive one at a time, far more times than the capacity, so
     * head and tail lap the storage repeatedly. */
    for (uint32_t i = 0; i < 1000; i++) {
        if (!reflex_kqueue_try_send(q, &i)) {
            ok = false;
            break;
        }
        if (!reflex_kqueue_try_recv(q, &out) || out != i) {
            ok = false;
            break;
        }
    }
    CHECK("1000 send/recv cycles wrap correctly", ok);
    CHECK("queue empty after cycling", reflex_kqueue_is_empty(q));

    /* Now keep it partly full while wrapping, which moves head and tail
     * independently rather than in lockstep. */
    ok = true;
    uint32_t next_send = 0, next_recv = 0;
    for (int step = 0; step < 500; step++) {
        if (reflex_kqueue_count(q) < 2) {
            if (!reflex_kqueue_try_send(q, &next_send)) {
                ok = false;
                break;
            }
            next_send++;
        } else {
            if (!reflex_kqueue_try_recv(q, &out) || out != next_recv) {
                ok = false;
                break;
            }
            next_recv++;
        }
    }
    CHECK("partial-fill wrapping preserves order", ok);
    reflex_kqueue_destroy(q);
}

/* ---- Differential test against the reference FIFO ---- */

static void test_differential(void) {
    const uint32_t cap = 5;
    reflex_kqueue_t *q = reflex_kqueue_create(cap, sizeof(uint32_t));
    ref_fifo_t ref = {.count = 0};
    uint32_t next = 1;
    bool agree = true;

    srand(20260904);
    for (int step = 0; step < 20000 && agree; step++) {
        if (rand() % 2) {
            uint32_t v = next++;
            bool got = reflex_kqueue_try_send(q, &v);
            bool want = ref_send(&ref, cap, v);
            if (got != want) {
                agree = false;
                break;
            }
        } else {
            uint32_t got_v = 0, want_v = 0;
            bool got = reflex_kqueue_try_recv(q, &got_v);
            bool want = ref_recv(&ref, &want_v);
            if (got != want) {
                agree = false;
                break;
            }
            if (got && got_v != want_v) {
                agree = false;
                break;
            }
        }
        /* Agreement is checked after every single operation, not at the end:
         * a ring that diverges and then re-converges would pass a final-state
         * comparison. */
        if (reflex_kqueue_count(q) != ref.count) {
            agree = false;
            break;
        }
        if (reflex_kqueue_is_empty(q) != (ref.count == 0)) {
            agree = false;
            break;
        }
        if (reflex_kqueue_is_full(q) != (ref.count >= cap)) {
            agree = false;
            break;
        }
    }
    CHECK("20000 random ops agree with the reference FIFO", agree);
    reflex_kqueue_destroy(q);
}

/* ---- The blocking API before the scheduler is running ----
 *
 * reflex_sched_queue_send/recv park the caller when the queue cannot satisfy
 * it. Before reflex_sched_start there is nothing to yield to and no peer that
 * could make progress, so waiting would hang the boot rather than delay it —
 * both degrade to a single attempt. That degraded path is reachable on the
 * host, which the scheduler proper is not, so it is worth pinning: it is the
 * path every queue operation during early init takes.
 */
static void test_blocking_before_start(void) {
    reflex_kqueue_t *q = reflex_kqueue_create(1, sizeof(uint32_t));
    uint32_t in = 7, out = 0;

    CHECK("send succeeds when there is room", reflex_sched_queue_send(q, &in, 10) == REFLEX_OK);
    /* Queue is now full; with no scheduler, a blocking send must give up
     * rather than spin. */
    CHECK("full send times out instead of hanging",
          reflex_sched_queue_send(q, &in, 10) == REFLEX_ERR_TIMEOUT);
    /* Including the untimed form — "forever" cannot mean forever here. */
    CHECK("full send with WAIT_FOREVER still returns",
          reflex_sched_queue_send(q, &in, REFLEX_SCHED_WAIT_FOREVER) == REFLEX_ERR_TIMEOUT);

    CHECK("recv returns the item", reflex_sched_queue_recv(q, &out, 10) == REFLEX_OK);
    CHECK("recv value is correct", out == 7);
    CHECK("empty recv times out", reflex_sched_queue_recv(q, &out, 10) == REFLEX_ERR_TIMEOUT);
    CHECK("empty recv with WAIT_FOREVER still returns",
          reflex_sched_queue_recv(q, &out, REFLEX_SCHED_WAIT_FOREVER) == REFLEX_ERR_TIMEOUT);

    CHECK("send to NULL queue is an argument error",
          reflex_sched_queue_send(NULL, &in, 0) == REFLEX_ERR_INVALID_ARG);
    CHECK("send of NULL item is an argument error",
          reflex_sched_queue_send(q, NULL, 0) == REFLEX_ERR_INVALID_ARG);
    CHECK("recv from NULL queue is an argument error",
          reflex_sched_queue_recv(NULL, &out, 0) == REFLEX_ERR_INVALID_ARG);

    reflex_kqueue_destroy(q);
}

int test_reflex_queue(void) {
    printf("[kqueue] ");
    test_create();
    test_pool_exhaustion();
    test_null_safety();
    test_fifo_and_bounds();
    test_item_fidelity();
    test_wraparound();
    test_differential();
    test_blocking_before_start();
    if (s_fail == 0) printf("ok\n");
    return s_fail;
}

int test_reflex_queue_passed(void) {
    return s_pass;
}
