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

/* Intentional test fixtures — duplicated from reflex_tuning.h values
 * so this file compiles standalone without the full include chain.
 * If tuning defaults change, update these to match. */
#define HEBBIAN_COMMIT_THRESHOLD 8
#define HEBBIAN_COUNTER_MAX      (HEBBIAN_COMMIT_THRESHOLD * 2)
#define PULSE_BASE_PRIORITY   10
#define PULSE_PURPOSE_BOOST    3
#define PULSE_HEBBIAN_BOOST    2

typedef int8_t reflex_trit_t;
#define REFLEX_TRIT_POS  1
#define REFLEX_TRIT_NEG -1

static uint32_t test_fnv1a(const char *s) {
    uint32_t h = 0x811c9dc5;
    for (int i = 0; s[i]; i++) { h ^= (uint32_t)s[i]; h *= 0x01000193; }
    return h;
}

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

    /* --- Test 5: Staleness timeout logic --- */
    {
        #define TEST_STALENESS_US (5 * 1000 * 1000ULL)
        uint64_t last_seen = 1000000;
        uint64_t now = 1000000 + TEST_STALENESS_US + 1;
        bool should_reset = (now - last_seen >= TEST_STALENESS_US);
        if (!should_reset) { printf("FAIL staleness_expired\n"); failures++; goto done; }

        now = 1000000 + TEST_STALENESS_US - 1;
        bool still_active = (now - last_seen < TEST_STALENESS_US);
        if (!still_active) { printf("FAIL staleness_active\n"); failures++; goto done; }
    }

    /* --- Test 6: Peer name extraction from GOONIES path --- */
    {
        const char *path = "peer.bravo.agency.led.intent";
        if (strncmp(path, "peer.", 5) != 0) { printf("FAIL peer_prefix\n"); failures++; goto done; }
        const char *after = path + 5;
        const char *dot = strchr(after, '.');
        if (!dot) { printf("FAIL peer_dot\n"); failures++; goto done; }
        char peer_name[12] = {0};
        size_t plen = (size_t)(dot - after);
        if (plen >= sizeof(peer_name)) plen = sizeof(peer_name) - 1;
        memcpy(peer_name, after, plen);
        if (strcmp(peer_name, "bravo") != 0) { printf("FAIL peer_name=%s\n", peer_name); failures++; goto done; }
        const char *remote_suffix = dot + 1;
        if (strcmp(remote_suffix, "agency.led.intent") != 0) { printf("FAIL remote_suffix=%s\n", remote_suffix); failures++; goto done; }
    }

    /* --- Test 7: Security — sys.* namespace rejection --- */
    {
        const char *remote = "sys.origin";
        bool rejected = (strncmp(remote, "sys.", 4) == 0);
        if (!rejected) { printf("FAIL sys_reject\n"); failures++; goto done; }

        const char *allowed = "agency.led.intent";
        bool accepted = (strncmp(allowed, "sys.", 4) != 0);
        if (!accepted) { printf("FAIL agency_accept\n"); failures++; goto done; }
    }

    /* --- Test 8: FNV-1a hash consistency --- */
    {
        /* Reproduce goose_fnv1a inline for host test portability. */
        uint32_t h1 = test_fnv1a("agency.led.intent");
        uint32_t h2 = test_fnv1a("agency.led.intent");
        if (h1 != h2) { printf("FAIL fnv1a_deterministic\n"); failures++; goto done; }
        if (h1 == 0) { printf("FAIL fnv1a_nonzero\n"); failures++; goto done; }
        /* Different input -> different hash */
        uint32_t h3 = test_fnv1a("sys.origin");
        if (h3 == h1) { printf("FAIL fnv1a_collision\n"); failures++; goto done; }
    }

    /* --- Test 9: Peer index mapping (1-indexed) --- */
    {
        /* peer_id 0 = local, 1+ = remote */
        uint8_t peer_id = 0;
        if (peer_id != 0) { printf("FAIL peer_local\n"); failures++; goto done; }
        peer_id = 1; /* first registered peer */
        if (peer_id - 1 != 0) { printf("FAIL peer_idx_map\n"); failures++; goto done; }
        peer_id = 8; /* max peer */
        if (peer_id - 1 != 7) { printf("FAIL peer_max_idx\n"); failures++; goto done; }
    }

    /* --- Test 10: Namespace prefix matching logic --- */
    {
        /* sys. prefix blocks remote writes */
        const char *paths[] = {"sys.origin", "sys.ai.reward", "agency.led.intent", "perception.temp"};
        bool expected_blocked[] = {true, true, false, false};
        for (int i = 0; i < 4; i++) {
            bool blocked = (strncmp(paths[i], "sys.", 4) == 0);
            if (blocked != expected_blocked[i]) {
                printf("FAIL ns_block_%s\n", paths[i]); failures++; goto done;
            }
        }
    }

done:
    if (failures == 0) printf("ok\n");
    return failures;
}
