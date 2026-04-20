/**
 * @file test_scheduler.c
 * @brief Tests the real kernel scheduler (setjmp/longjmp) on host.
 */

#include <stdio.h>
#include <string.h>
#include "reflex_sched.h"

static int s_task_a_ran = 0;
static int s_task_b_ran = 0;

static void task_a(void *arg) {
    (void)arg;
    s_task_a_ran++;
    reflex_sched_yield();
    s_task_a_ran++;
}

static void task_b(void *arg) {
    (void)arg;
    s_task_b_ran++;
    reflex_sched_yield();
    s_task_b_ran++;
}

int test_scheduler(void) {
    int failures = 0;

    printf("[sched]   ");

    reflex_sched_init();

    reflex_tcb_t *tcb_a = NULL;
    reflex_tcb_t *tcb_b = NULL;
    reflex_err_t rc;

    rc = reflex_sched_create_task(task_a, "task_a", 2048, NULL, 5, &tcb_a);
    if (rc != REFLEX_OK || !tcb_a) { printf("FAIL create_a\n"); return 1; }

    rc = reflex_sched_create_task(task_b, "task_b", 2048, NULL, 5, &tcb_b);
    if (rc != REFLEX_OK || !tcb_b) { printf("FAIL create_b\n"); return 1; }

    /* Verify priority */
    if (tcb_a->priority != 5) { printf("FAIL priority\n"); failures++; }

    /* Verify names */
    if (strcmp(tcb_a->name, "task_a") != 0) { printf("FAIL name\n"); failures++; }

    /* Verify states */
    if (tcb_a->state != REFLEX_TASK_STATE_READY) { printf("FAIL state\n"); failures++; }

    if (failures == 0) printf("ok\n");
    return failures;
}
