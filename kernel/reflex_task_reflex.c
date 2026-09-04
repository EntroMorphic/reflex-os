/**
 * @file reflex_task_reflex.c
 * @brief reflex_task.h implemented on the Reflex scheduler, with no FreeRTOS.
 *
 * C3 of the independence work: the point at which Reflex owns its own task
 * scheduling rather than delegating it.
 *
 * `reflex_task_kernel.c` implements the same thirteen functions and forwards
 * every one of them to FreeRTOS. This file forwards them to `reflex_sched_*`
 * and `reflex_kqueue_*` instead. Exactly one of the two is compiled — they
 * define the same symbols — and which one is `CONFIG_REFLEX_TASK_BACKEND_REFLEX`.
 *
 * ## It is off by default, and must stay off until the tick works
 *
 * Not caution for its own sake, and the reason is blunter than "the tick is
 * incomplete". *Nothing starts the scheduler.* `reflex_sched_start` is called
 * only from `reflex_startup.c` and `reflex_kernel_test.c`, and nothing calls
 * either — `nm` on a build with this option selected shows
 * `reflex_sched_create_task` linked and `reflex_sched_start` absent. So
 * `reflex_task_create` files a TCB into the scheduler's table and no task ever
 * runs. A board with this enabled boots ESP-IDF, creates tasks that never
 * execute, and does nothing at all.
 *
 * Behind that sits the tick: `setup_systimer_tick` enables the interrupt at
 * the SYSTIMER peripheral, but nothing routes it through the interrupt matrix
 * to a CPU line and the trap handler is not told which line to expect, so even
 * once something does start the scheduler, delays never expire. See
 * reflex_trap.c for the three specific missing pieces.
 *
 * What this file *is* good for now: it compiles against nothing but Reflex's
 * own headers, so `make warn-check` builds it on every commit, and it makes
 * the remaining distance concrete — the cutover is a Kconfig flip once the
 * tick lands, not a rewrite.
 *
 * ## Two deliberate differences from the FreeRTOS backend
 *
 * Critical sections are global here. FreeRTOS's `portMUX_TYPE` is a per-object
 * spinlock; `reflex_sched_enter_critical` disables interrupts for the whole
 * core and counts nesting. On the single-core C6 that provides the mutual
 * exclusion callers actually rely on, but it is coarser: two unrelated
 * critical sections serialise against each other. The `reflex_mutex_t` value
 * is therefore unused rather than misused — it is accepted, ignored, and
 * documented, rather than quietly reinterpreted.
 *
 * Return codes match the FreeRTOS backend exactly, including the asymmetry
 * that a failed send reports REFLEX_ERR_TIMEOUT and a failed receive reports
 * REFLEX_ERR_NOT_FOUND. That asymmetry is not obviously right, but callers
 * compare against REFLEX_OK and changing it here would make the two backends
 * differ in a way no test would catch.
 */

#include "reflex_task.h"

#include "reflex_kqueue.h"
#include "reflex_sched.h"

#include <stddef.h>

/* The interface promises this much storage; the scheduler's critical section
 * needs none of it. Asserted anyway, so a future per-object lock cannot be
 * added here without noticing the interface has to grow too. */
_Static_assert(sizeof(reflex_mutex_t) >= sizeof(uint32_t) * 2,
               "reflex_mutex_t smaller than the interface documents");

/* ---- Tasks ---- */

reflex_err_t reflex_task_create(void (*fn)(void *), const char *name, uint32_t stack_bytes,
                                void *arg, int priority, reflex_task_handle_t *out_handle) {
    reflex_tcb_t *tcb = NULL;
    reflex_err_t rc = reflex_sched_create_task(fn, name, stack_bytes, arg, priority, &tcb);
    if (rc != REFLEX_OK) return rc;
    if (out_handle) *out_handle = (reflex_task_handle_t)tcb;
    return REFLEX_OK;
}

void reflex_task_delete(reflex_task_handle_t handle) {
    /* NULL means "the caller", which is what reflex_sched_delete_task already
     * does with NULL — the interface and the scheduler agree here. */
    reflex_sched_delete_task((reflex_tcb_t *)handle);
}

void reflex_task_delay_ms(uint32_t ms) {
    reflex_sched_delay_ms(ms);
}

void reflex_task_yield(void) {
    reflex_sched_yield();
}

reflex_task_handle_t reflex_task_get_by_name(const char *name) {
    return (reflex_task_handle_t)reflex_sched_find_by_name(name);
}

void reflex_task_set_priority(reflex_task_handle_t handle, int priority) {
    reflex_sched_set_priority((reflex_tcb_t *)handle, priority);
}

int reflex_task_get_priority(reflex_task_handle_t handle) {
    return reflex_sched_get_priority((const reflex_tcb_t *)handle);
}

/* ---- Queues ---- */

reflex_queue_handle_t reflex_queue_create(uint32_t length, uint32_t item_size) {
    return (reflex_queue_handle_t)reflex_kqueue_create(length, item_size);
}

reflex_err_t reflex_queue_send(reflex_queue_handle_t q, const void *item, uint32_t timeout_ms) {
    if (!q) return REFLEX_ERR_INVALID_ARG;
    reflex_err_t rc = reflex_sched_queue_send((reflex_kqueue_t *)q, item, timeout_ms);
    /* UINT32_MAX is "wait forever" in both interfaces, so it needs no
     * translation — reflex_sched.h defines REFLEX_SCHED_WAIT_FOREVER as the
     * same value the callers already pass. */
    return rc;
}

reflex_err_t reflex_queue_recv(reflex_queue_handle_t q, void *item, uint32_t timeout_ms) {
    if (!q) return REFLEX_ERR_INVALID_ARG;
    reflex_err_t rc = reflex_sched_queue_recv((reflex_kqueue_t *)q, item, timeout_ms);
    /* Match the FreeRTOS backend: an empty receive is NOT_FOUND, not TIMEOUT. */
    return (rc == REFLEX_ERR_TIMEOUT) ? REFLEX_ERR_NOT_FOUND : rc;
}

/* ---- Critical sections ---- */

reflex_mutex_t reflex_mutex_init(void) {
    /* Zeroed rather than left indeterminate. The value is unused, but a caller
     * copying or comparing one should see something defined. */
    reflex_mutex_t m = {{0, 0}};
    return m;
}

void reflex_critical_enter(reflex_mutex_t *m) {
    (void)m; /* global critical section; see the file comment */
    reflex_sched_enter_critical();
}

void reflex_critical_exit(reflex_mutex_t *m) {
    (void)m;
    reflex_sched_exit_critical();
}
