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

/* ---- Tick arithmetic ----
 *
 * Both of these were extracted from the scheduler because they were wrong and
 * unreachable: the host build never runs a task, so nothing that lived inside
 * reflex_sched_delay_ms or the queue wait could be tested at all. Pulled out,
 * they are ordinary functions with ordinary edge cases.
 */
static void test_ms_to_ticks(void) {
    CHECK("zero ms is zero ticks", reflex_sched_ms_to_ticks(0) == 0);
    CHECK("10 ms at 1000 Hz is 10 ticks", reflex_sched_ms_to_ticks(10) == 10);

    /* The last value where ms * REFLEX_SCHED_TICK_HZ still fits in a uint32. */
    CHECK("4294967 ms is exact", reflex_sched_ms_to_ticks(4294967) == 4294967);

    /* One past it. The naive expression wrapped to 705,032 ticks here, turning
     * an 83-minute timeout into a 12-minute one — a timeout that fires early is
     * far worse than one that is merely capped, because the caller believes it
     * waited. */
    CHECK("5,000,000 ms does not wrap short", reflex_sched_ms_to_ticks(5000000) == 5000000);
    CHECK("5,000,000 ms is not the wrapped value", reflex_sched_ms_to_ticks(5000000) != 705032);

    /* At REFLEX_SCHED_TICK_HZ == 1000 the conversion is the identity, so this
     * is the largest representable request rather than a clamped one — the
     * saturation branch in ms_to_ticks is genuinely unreachable at this tick
     * rate, and mutation testing confirms deleting it breaks nothing. It is
     * kept as defence against a tick-rate change, guarded by a _Static_assert
     * there. Noted here so the branch is not later removed as dead code. */
    CHECK("UINT32_MAX does not wrap", reflex_sched_ms_to_ticks(UINT32_MAX) == UINT32_MAX);

    /* Monotonic — a longer wait must never convert to a shorter one. */
    bool mono = true;
    uint32_t prev = 0;
    for (uint64_t ms = 0; ms <= UINT32_MAX; ms += 0x01000000u) {
        uint32_t t = reflex_sched_ms_to_ticks((uint32_t)ms);
        if (t < prev) {
            mono = false;
            break;
        }
        prev = t;
    }
    CHECK("conversion is monotonic across the range", mono);
}

static void test_tick_reached(void) {
    CHECK("past deadline is reached", reflex_sched_tick_reached(100, 50));
    CHECK("exact deadline is reached", reflex_sched_tick_reached(100, 100));
    CHECK("future deadline is not reached", !reflex_sched_tick_reached(50, 100));

    /* The case a plain `now >= deadline` gets wrong. The counter wraps every
     * 49.7 days at 1000 Hz; a deadline set just before the wrap compares a
     * small `now` against a huge `deadline` and reads as unreached forever,
     * so the sleeping task never wakes. */
    CHECK("deadline before the wrap is reached after it",
          reflex_sched_tick_reached(5, 0xFFFFFF00u));
    CHECK("naive comparison would have failed that", !(5u >= 0xFFFFFF00u));

    /* And the mirror: a deadline just after the wrap is genuinely in the
     * future and must not read as reached. */
    CHECK("deadline just past the wrap is not yet reached",
          !reflex_sched_tick_reached(0xFFFFFF00u, 5));

    /* Boundary of the technique: correct up to half the counter range. */
    CHECK("just under half-range ahead is not reached", !reflex_sched_tick_reached(0, 0x7FFFFFFFu));
    CHECK("just under half-range behind is reached", reflex_sched_tick_reached(0x7FFFFFFFu, 0));
}

/* ---- The deadline sweep's decision ----
 *
 * pick_next is not compiled on the host, so this predicate was untestable
 * where it lived — and it was got wrong: an earlier revision skipped every
 * task with blocked_on set, which quietly removed the timeout from a timed
 * queue wait. Extracted, it is checkable.
 */
static void test_should_time_wake(void) {
    reflex_tcb_t t;
    memset(&t, 0, sizeof(t));

    CHECK("NULL never wakes", !reflex_sched_should_time_wake(NULL, 100));

    t.state = REFLEX_TASK_STATE_READY;
    t.wake_deadline_valid = true;
    t.wake_tick = 0;
    CHECK("a READY task is not woken by the sweep", !reflex_sched_should_time_wake(&t, 100));

    /* Plain sleeper: reflex_sched_delay_ms. */
    t.state = REFLEX_TASK_STATE_BLOCKED;
    t.blocked_on = NULL;
    t.wake_deadline_valid = true;
    t.wake_tick = 50;
    CHECK("expired sleeper wakes", reflex_sched_should_time_wake(&t, 100));
    t.wake_tick = 200;
    CHECK("unexpired sleeper does not wake", !reflex_sched_should_time_wake(&t, 100));

    /* Timed queue wait. This is the regression: it has blocked_on set *and* a
     * real deadline, and it must still expire on its own. */
    int dummy_queue;
    t.blocked_on = &dummy_queue;
    t.wake_deadline_valid = true;
    t.wake_tick = 50;
    CHECK("timed queue waiter still times out", reflex_sched_should_time_wake(&t, 100));

    /* Untimed queue wait: the clock must never wake it. */
    t.wake_deadline_valid = false;
    t.wake_tick = 0;
    CHECK("untimed queue waiter is not woken at tick 0", !reflex_sched_should_time_wake(&t, 0));
    CHECK("untimed queue waiter is not woken later",
          !reflex_sched_should_time_wake(&t, 0xFFFFFFFFu));
    t.wake_tick = 0xFFFFFFFFu;
    CHECK("untimed waiter is not woken by a sentinel deadline either",
          !reflex_sched_should_time_wake(&t, 0));

    /* And it still works across the counter wrap. */
    t.wake_deadline_valid = true;
    t.wake_tick = 0xFFFFFF00u;
    CHECK("deadline before the wrap wakes after it", reflex_sched_should_time_wake(&t, 5));
}

/* ---- Task selection ----
 *
 * The scheduler's central decision, and the first tests ever to make it. It
 * lived inside pick_next, which is not compiled on the host, so no test had
 * selected a task before this.
 */
#define NTASK 4
static void mk(reflex_tcb_t *t, int n) {
    memset(t, 0, sizeof(*t) * (size_t)n);
    for (int i = 0; i < n; i++)
        t[i].state = REFLEX_TASK_STATE_FREE;
}

static void test_select(void) {
    reflex_tcb_t t[NTASK];

    CHECK("NULL table selects nothing", reflex_sched_select(NULL, NTASK, 0) == -1);
    mk(t, NTASK);
    CHECK("zero count selects nothing", reflex_sched_select(t, 0, 0) == -1);
    CHECK("no READY task selects nothing", reflex_sched_select(t, NTASK, 0) == -1);

    /* Only READY is runnable — BLOCKED, RUNNING and DEAD are not. */
    mk(t, NTASK);
    t[1].state = REFLEX_TASK_STATE_BLOCKED;
    t[2].state = REFLEX_TASK_STATE_DEAD;
    t[3].state = REFLEX_TASK_STATE_RUNNING;
    CHECK("only READY tasks are eligible", reflex_sched_select(t, NTASK, 0) == -1);

    mk(t, NTASK);
    t[2].state = REFLEX_TASK_STATE_READY;
    CHECK("the one READY task is chosen", reflex_sched_select(t, NTASK, 0) == 2);

    /* Priority beats scan order in both directions, so a passing result cannot
     * be an accident of where the scan happened to begin. */
    mk(t, NTASK);
    for (int i = 0; i < NTASK; i++)
        t[i].state = REFLEX_TASK_STATE_READY;
    t[0].priority = 1;
    t[1].priority = 9;
    t[2].priority = 3;
    t[3].priority = 2;
    CHECK("highest priority wins from slot 0", reflex_sched_select(t, NTASK, 0) == 1);
    CHECK("highest priority wins from slot 2", reflex_sched_select(t, NTASK, 2) == 1);
    t[0].priority = 9;
    t[1].priority = 1;
    CHECK("highest priority wins when it moves", reflex_sched_select(t, NTASK, 0) == 0);

    /* Equal priority: the scan start is what makes this round-robin. Starting
     * after the task that just ran is what stops it starving its equals. */
    mk(t, NTASK);
    for (int i = 0; i < NTASK; i++) {
        t[i].state = REFLEX_TASK_STATE_READY;
        t[i].priority = 5;
    }
    CHECK("equal priority, start 0 picks 0", reflex_sched_select(t, NTASK, 0) == 0);
    CHECK("equal priority, start 1 picks 1", reflex_sched_select(t, NTASK, 1) == 1);
    CHECK("equal priority, start 3 picks 3", reflex_sched_select(t, NTASK, 3) == 3);

    /* Rotating the start across the whole table must visit every task — this
     * is the property that actually says "round-robin". */
    bool seen[NTASK] = {false, false, false, false};
    for (int st = 0; st < NTASK; st++) {
        int idx = reflex_sched_select(t, NTASK, st);
        if (idx >= 0 && idx < NTASK) seen[idx] = true;
    }
    bool all = true;
    for (int i = 0; i < NTASK; i++)
        if (!seen[i]) all = false;
    CHECK("rotating the start reaches every equal-priority task", all);

    /* A start index past the end is normalised, not used to index out of
     * range: pick_next passes current+1, which is exactly count at the top slot. */
    CHECK("start == count wraps to 0", reflex_sched_select(t, NTASK, NTASK) == 0);
    CHECK("start beyond count wraps", reflex_sched_select(t, NTASK, NTASK + 2) == 2);

    /* A negative start is the only case the explicit normalisation is for: the
     * loop's own `% count` already handles anything non-negative, but C's `%`
     * truncates toward zero, so -1 % 4 is -1 and the first probe would read
     * tasks[-1]. pick_next never passes a negative today — the header promises
     * the index is wrapped, so the promise is tested rather than assumed. */
    CHECK("start -1 wraps to the last slot", reflex_sched_select(t, NTASK, -1) == 3);
    CHECK("start -NTASK wraps to 0", reflex_sched_select(t, NTASK, -NTASK) == 0);
    for (int st = -3 * NTASK; st < 3 * NTASK; st++) {
        int idx = reflex_sched_select(t, NTASK, st);
        if (idx < 0 || idx >= NTASK) {
            CHECK("every start yields an in-range index", false);
            break;
        }
    }
    CHECK("no start index escapes the table", true);

    /* Negative priorities are legal; a withheld task is still runnable. */
    mk(t, NTASK);
    t[0].state = REFLEX_TASK_STATE_READY;
    t[0].priority = -5;
    t[1].state = REFLEX_TASK_STATE_READY;
    t[1].priority = -1;
    CHECK("least-negative priority wins", reflex_sched_select(t, NTASK, 0) == 1);
}

/* ---- Task lookup by name ---- */
static void test_find_index(void) {
    reflex_tcb_t t[NTASK];
    mk(t, NTASK);

    CHECK("NULL table finds nothing", reflex_sched_find_index(NULL, NTASK, "a") == -1);
    CHECK("NULL name finds nothing", reflex_sched_find_index(t, NTASK, NULL) == -1);
    CHECK("empty table finds nothing", reflex_sched_find_index(t, NTASK, "a") == -1);

    t[1].state = REFLEX_TASK_STATE_READY;
    t[1].name = "supervisor";
    t[2].state = REFLEX_TASK_STATE_BLOCKED;
    t[2].name = "shell";
    t[3].state = REFLEX_TASK_STATE_RUNNING;
    t[3].name = "main";

    CHECK("finds a READY task", reflex_sched_find_index(t, NTASK, "supervisor") == 1);
    CHECK("finds a BLOCKED task", reflex_sched_find_index(t, NTASK, "shell") == 2);
    CHECK("finds a RUNNING task", reflex_sched_find_index(t, NTASK, "main") == 3);
    CHECK("unknown name is not found", reflex_sched_find_index(t, NTASK, "nope") == -1);

    /* Exact match only — a prefix must not resolve. */
    CHECK("prefix does not match", reflex_sched_find_index(t, NTASK, "shel") == -1);
    CHECK("superstring does not match", reflex_sched_find_index(t, NTASK, "shells") == -1);

    /* A dead or freed slot keeps its name pointer, and must not resolve: the
     * caller would get a TCB that is about to be handed to another task. */
    t[2].state = REFLEX_TASK_STATE_DEAD;
    CHECK("a DEAD task is not found", reflex_sched_find_index(t, NTASK, "shell") == -1);
    t[2].state = REFLEX_TASK_STATE_FREE;
    CHECK("a FREE slot is not found", reflex_sched_find_index(t, NTASK, "shell") == -1);

    /* A live slot with no name must not be matched by a NULL-ish probe. */
    t[0].state = REFLEX_TASK_STATE_READY;
    t[0].name = NULL;
    CHECK("an unnamed live task is skipped safely",
          reflex_sched_find_index(t, NTASK, "supervisor") == 1);
}

static void test_priority_accessors(void) {
    reflex_tcb_t t;
    memset(&t, 0, sizeof(t));
    t.priority = 3;
    CHECK("get returns the priority", reflex_sched_get_priority(&t) == 3);
    reflex_sched_set_priority(&t, 9);
    CHECK("set changes the priority", reflex_sched_get_priority(&t) == 9);
    reflex_sched_set_priority(&t, -4);
    CHECK("negative priorities are allowed", reflex_sched_get_priority(&t) == -4);
    CHECK("get of NULL is 0", reflex_sched_get_priority(NULL) == 0);
    reflex_sched_set_priority(NULL, 5); /* must not crash */
    CHECK("set of NULL survives", true);
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
    test_ms_to_ticks();
    test_tick_reached();
    test_should_time_wake();
    test_select();
    test_find_index();
    test_priority_accessors();
    if (s_fail == 0) printf("ok\n");
    return s_fail;
}

int test_reflex_queue_passed(void) {
    return s_pass;
}
