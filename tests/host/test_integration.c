/**
 * @file test_integration.c
 * @brief Integration test for the closed loop:
 *        purpose → Hebbian learning → priority adjustment.
 *
 * Tests the LOGIC of the interactions without compiling the full
 * GOOSE runtime (which has hardware dependencies). Exercises:
 * 1. Hebbian counter increment under reward with purpose amplification
 * 2. Orientation commitment at threshold
 * 3. Domain matching (reflex_domain_match)
 * 4. Priority computation
 */

#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <stdint.h>

/* Reproduce the core logic from goose_supervisor.c */
#define HEBBIAN_COMMIT_THRESHOLD 8
#define HEBBIAN_COUNTER_MAX      (HEBBIAN_COMMIT_THRESHOLD * 2)
#define PULSE_BASE_PRIORITY   10
#define PULSE_PURPOSE_BOOST    3
#define PULSE_HEBBIAN_BOOST    2

typedef int8_t reflex_trit_t;
#define REFLEX_TRIT_POS  1
#define REFLEX_TRIT_NEG -1

static bool reflex_domain_match(const char *name, const char *domain) {
    size_t dlen = strlen(domain);
    const char *p = name;
    while ((p = strstr(p, domain)) != NULL) {
        bool left_ok = (p == name || *(p - 1) == '.');
        bool right_ok = (p[dlen] == '\0' || p[dlen] == '.');
        if (left_ok && right_ok) return true;
        p++;
    }
    return false;
}

int test_integration(void) {
    int failures = 0;
    printf("[integ]   ");

    /* --- Test 1: Hebbian increment under reward with purpose_multiplier --- */
    int16_t counter = 0;
    int purpose_multiplier = 2;  /* purpose active */
    int source_state = 1;
    int sink_state = 1;  /* co-active */

    /* Simulate 4 reward cycles (should commit at threshold 8 with multiplier 2) */
    for (int i = 0; i < 4; i++) {
        int delta = ((source_state == sink_state) ? 1 : -1) * purpose_multiplier;
        int32_t next = (int32_t)counter + delta;
        if (next > HEBBIAN_COUNTER_MAX) next = HEBBIAN_COUNTER_MAX;
        if (next < -HEBBIAN_COUNTER_MAX) next = -HEBBIAN_COUNTER_MAX;
        counter = (int16_t)next;
    }
    if (counter != 8) { printf("FAIL hebb_counter=%d\n", counter); failures++; goto done; }

    /* Commit check */
    reflex_trit_t learned = 0;
    if (counter >= HEBBIAN_COMMIT_THRESHOLD) {
        learned = REFLEX_TRIT_POS;
        counter = 0;
    }
    if (learned != REFLEX_TRIT_POS) { printf("FAIL commit\n"); failures++; goto done; }
    if (counter != 0) { printf("FAIL reset\n"); failures++; goto done; }

    /* --- Test 2: Pain decay --- */
    counter = 5;
    for (int i = 0; i < 3; i++) {
        if (counter > 0) counter--;
    }
    if (counter != 2) { printf("FAIL pain_decay=%d\n", counter); failures++; goto done; }

    /* --- Test 3: Domain matching --- */
    if (!reflex_domain_match("agency.led.intent", "led")) {
        printf("FAIL domain_match_basic\n"); failures++; goto done;
    }
    if (reflex_domain_match("misled", "led")) {
        printf("FAIL domain_match_false_pos\n"); failures++; goto done;
    }
    if (!reflex_domain_match("led", "led")) {
        printf("FAIL domain_match_exact\n"); failures++; goto done;
    }
    if (!reflex_domain_match("comm.mesh.broadcast", "mesh")) {
        printf("FAIL domain_match_middle\n"); failures++; goto done;
    }
    if (reflex_domain_match("meshed.network", "mesh")) {
        printf("FAIL domain_match_suffix\n"); failures++; goto done;
    }

    /* --- Test 4: Priority computation --- */
    int priority = PULSE_BASE_PRIORITY;
    bool purpose_active = true;
    bool has_domain_route = true;
    int learned_routes = 3;

    if (purpose_active && has_domain_route) priority += PULSE_PURPOSE_BOOST;
    else if (purpose_active) priority += 1;
    if (learned_routes > 0) priority += PULSE_HEBBIAN_BOOST;

    /* Expected: 10 + 3 (domain match) + 2 (Hebbian) = 15 */
    if (priority != 15) { printf("FAIL priority=%d\n", priority); failures++; goto done; }

    /* Without domain match */
    priority = PULSE_BASE_PRIORITY;
    has_domain_route = false;
    if (purpose_active && has_domain_route) priority += PULSE_PURPOSE_BOOST;
    else if (purpose_active) priority += 1;
    if (learned_routes > 0) priority += PULSE_HEBBIAN_BOOST;
    /* Expected: 10 + 1 (no domain) + 2 (Hebbian) = 13 */
    if (priority != 13) { printf("FAIL priority_no_domain=%d\n", priority); failures++; goto done; }

done:
    if (failures == 0) printf("ok\n");
    return failures;
}
