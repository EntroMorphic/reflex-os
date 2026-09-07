/**
 * @file test_policy.c
 * @brief Regression tests for the substrate's pure decision rules.
 *
 * Every case here corresponds to a defect that actually shipped. The rules in
 * goose_policy.c were each wrong at least once, and in every instance the error
 * was either a state that could never occur or a decision taken on the wrong
 * signal — neither of which a compiler notices and neither of which a
 * spec-mirroring test would catch, since the spec was what was wrong.
 *
 * So these assert on observable outcomes, and several assert *reachability*:
 * that a value the design claims to produce can actually be produced. That
 * check is the one that would have caught the largest share of this codebase's
 * historical bugs.
 */

#include <stdio.h>
#include <string.h>
#include "goose_policy.h"

static int s_pass = 0, s_fail = 0;

#define CHECK(name, expr) do { \
    if (expr) { s_pass++; } \
    else { printf("  FAIL: %s\n", name); s_fail++; } \
} while (0)

/* --- Eviction eligibility -------------------------------------------------
 *
 * Regression: paged cells were unreclaimable, so the Loom filled to its cap and
 * refused all further paging. Took three passes to fix — a type-based rule, then
 * one that still let PINNED veto, then one that still let SYSTEM_ONLY veto.
 * Both vetoes are wrong for cached cells: alloc_cell stamps PINNED on every
 * system weave and set_agency copies SYSTEM_ONLY out of the atlas. */
static void test_evictable(void) {
    printf("[evict]   ");

    /* Identity namespace keeps the original type rule. */
    CHECK("identity/pinned kept",
          !goose_policy_cell_evictable(GOOSE_POLICY_TYPE_PINNED, GOOSE_POLICY_NS_IDENTITY));
    CHECK("identity/system kept",
          !goose_policy_cell_evictable(GOOSE_POLICY_TYPE_SYSTEM_ONLY, GOOSE_POLICY_NS_IDENTITY));
    CHECK("identity/purpose kept",
          !goose_policy_cell_evictable(GOOSE_POLICY_TYPE_PURPOSE, GOOSE_POLICY_NS_IDENTITY));
    CHECK("identity/hardware_in kept",
          !goose_policy_cell_evictable(GOOSE_POLICY_TYPE_HARDWARE_IN, GOOSE_POLICY_NS_IDENTITY));
    CHECK("identity/hardware_out kept",
          !goose_policy_cell_evictable(GOOSE_POLICY_TYPE_HARDWARE_OUT, GOOSE_POLICY_NS_IDENTITY));
    CHECK("identity/virtual evictable",
          goose_policy_cell_evictable(GOOSE_POLICY_TYPE_VIRTUAL, GOOSE_POLICY_NS_IDENTITY));
    CHECK("identity/intent evictable",
          goose_policy_cell_evictable(GOOSE_POLICY_TYPE_INTENT, GOOSE_POLICY_NS_IDENTITY));

    /* Cache namespaces are reclaimable regardless of type. These four are the
     * exact combinations that stalled eviction at 64 and then at 139. */
    CHECK("shadow/pinned evictable",
          goose_policy_cell_evictable(GOOSE_POLICY_TYPE_PINNED, GOOSE_POLICY_NS_SHADOW));
    CHECK("shadow/system_only evictable",
          goose_policy_cell_evictable(GOOSE_POLICY_TYPE_SYSTEM_ONLY, GOOSE_POLICY_NS_SHADOW));
    CHECK("shadow/hardware_in evictable",
          goose_policy_cell_evictable(GOOSE_POLICY_TYPE_HARDWARE_IN, GOOSE_POLICY_NS_SHADOW));
    CHECK("shadow/hardware_out evictable",
          goose_policy_cell_evictable(GOOSE_POLICY_TYPE_HARDWARE_OUT, GOOSE_POLICY_NS_SHADOW));
    CHECK("peer/virtual evictable",
          goose_policy_cell_evictable(GOOSE_POLICY_TYPE_VIRTUAL, GOOSE_POLICY_NS_PEER));
    CHECK("peer/pinned evictable",
          goose_policy_cell_evictable(GOOSE_POLICY_TYPE_PINNED, GOOSE_POLICY_NS_PEER));

    /* Reachability: paging is only sustainable if *every* type the atlas can
     * produce is reclaimable once cached. Sweep them all. */
    int reclaimable = 0;
    for (int t = GOOSE_POLICY_TYPE_VIRTUAL; t <= GOOSE_POLICY_TYPE_PURPOSE; t++) {
        if (goose_policy_cell_evictable(t, GOOSE_POLICY_NS_SHADOW)) reclaimable++;
    }
    CHECK("every shadow type is reclaimable", reclaimable == 10);

    printf("ok\n");
}

/* --- Ternary disposition --------------------------------------------------
 *
 * Regression: WITHHELD was derived from holon deactivation alone, but a holon
 * with a non-empty domain deactivates whenever no purpose is set — so a board
 * that had never been told what to do reported its fields as actively
 * suppressed rather than undecided. */
static void test_disposition(void) {
    printf("[stance]  ");

    /* No purpose: undecided, never suppressed — even for holon members. */
    CHECK("no purpose -> latent",
          goose_policy_disposition(false, false, false, false, false) == GOOSE_POLICY_LATENT);
    CHECK("no purpose + inactive holon -> latent, NOT withheld",
          goose_policy_disposition(false, false, true, false, false) == GOOSE_POLICY_LATENT);

    /* Purpose declared. */
    CHECK("purpose + domain route -> engaged",
          goose_policy_disposition(true, true, false, false, false) == GOOSE_POLICY_ENGAGED);
    CHECK("purpose, no domain route -> latent",
          goose_policy_disposition(true, false, false, false, false) == GOOSE_POLICY_LATENT);
    CHECK("purpose + holon outside it -> withheld",
          goose_policy_disposition(true, false, true, false, false) == GOOSE_POLICY_WITHHELD);
    CHECK("purpose + active holon + domain -> engaged",
          goose_policy_disposition(true, true, true, true, false) == GOOSE_POLICY_ENGAGED);

    /* Pain narrows attention; it does not halt purposeful work. */
    CHECK("pain, off-domain -> withheld",
          goose_policy_disposition(true, false, false, false, true) == GOOSE_POLICY_WITHHELD);
    CHECK("pain, on-domain -> stays engaged",
          goose_policy_disposition(true, true, false, false, true) == GOOSE_POLICY_ENGAGED);
    CHECK("pain with no purpose -> withheld (nothing serves a purpose)",
          goose_policy_disposition(false, false, false, false, true) == GOOSE_POLICY_WITHHELD);

    /* Reachability: all three stances must be producible. */
    int seen_engaged = 0, seen_latent = 0, seen_withheld = 0;
    for (int i = 0; i < 32; i++) {
        int d = goose_policy_disposition(i & 1, (i >> 1) & 1, (i >> 2) & 1,
                                         (i >> 3) & 1, (i >> 4) & 1);
        if (d > 0) seen_engaged++; else if (d < 0) seen_withheld++; else seen_latent++;
    }
    CHECK("engaged reachable",  seen_engaged  > 0);
    CHECK("latent reachable",   seen_latent   > 0);
    CHECK("withheld reachable", seen_withheld > 0);

    /* Aggregate is precedence, not unanimity — requiring every field to be
     * withheld made -1 unreachable while a field was demonstrably held back. */
    CHECK("aggregate any-engaged -> engaged",
          goose_policy_aggregate(1, 3) == GOOSE_POLICY_ENGAGED);
    CHECK("aggregate some-withheld, none engaged -> withheld",
          goose_policy_aggregate(0, 1) == GOOSE_POLICY_WITHHELD);
    CHECK("aggregate nothing -> latent",
          goose_policy_aggregate(0, 0) == GOOSE_POLICY_LATENT);

    printf("ok\n");
}

/* --- Mesh health windowing ------------------------------------------------
 *
 * Two regressions: the threshold was compared per-scan when a quiet mesh only
 * beacons once per ~10 scans (so +1 could never occur), and the isolation
 * threshold was a fixed 3 scans against a 10-scan beacon (so a healthy mesh
 * declared itself isolated most of the time). */
static void test_mesh_window(void) {
    printf("[mesh]    ");
    const uint32_t ISO = 20, SPARSE = 2;

    /* A healthy but quiet mesh: one arc every 10 scans. Must never read
     * isolated, and must reach connected. */
    goose_mesh_window_t w; memset(&w, 0, sizeof(w));
    int isolated = 0, connected = 0;
    for (int scan = 0; scan < 200; scan++) {
        int st = goose_policy_mesh_state(&w, (scan % 10 == 0) ? 1u : 0u, ISO, SPARSE);
        if (st < 0) isolated++;
        if (st > 0) connected++;
    }
    CHECK("healthy quiet mesh never reads isolated", isolated == 0);
    CHECK("healthy quiet mesh reaches connected", connected > 0);

    /* Genuine silence must eventually read isolated, and not before the
     * window has elapsed. */
    memset(&w, 0, sizeof(w));
    int first_isolated = -1;
    for (int scan = 0; scan < 100; scan++) {
        if (goose_policy_mesh_state(&w, 0, ISO, SPARSE) < 0) { first_isolated = scan; break; }
    }
    CHECK("silence eventually isolates", first_isolated >= 0);
    CHECK("does not isolate before the window elapses", first_isolated >= (int)ISO - 1);

    /* Degenerate isolated_ticks must not make the mesh permanently isolated.
     * The derived constant can reach 0 under a plausible retune. */
    memset(&w, 0, sizeof(w));
    int st0 = goose_policy_mesh_state(&w, 5, 0, SPARSE);
    CHECK("isolated_ticks=0 is floored, not taken literally", st0 >= 0);
    memset(&w, 0, sizeof(w));
    int st1 = goose_policy_mesh_state(&w, 5, 1, SPARSE);
    CHECK("isolated_ticks=1 is floored too", st1 >= 0);

    /* Reachability: all three states from one rule set.
     *
     * The trajectory matters. An earlier version of this sweep began with
     * traffic on the very first scan, which goes straight to connected and
     * never visits sparse — the assertion failed and the rule was fine. Sparse
     * is the state of a window that has not yet accumulated enough evidence
     * either way, so it has to be entered from quiet, not from traffic:
     *
     *   scans   0..9    quiet, window still filling      -> sparse
     *   scans  10..109  regular arcs                     -> connected
     *   scans 110..     silence past the isolation window -> isolated
     */
    memset(&w, 0, sizeof(w));
    int seen_pos = 0, seen_zero = 0, seen_neg = 0;
    for (int scan = 0; scan < 400; scan++) {
        uint32_t d = (scan >= 10 && scan < 110 && scan % 5 == 0) ? 2u : 0u;
        int st = goose_policy_mesh_state(&w, d, ISO, SPARSE);
        if (st > 0) seen_pos++; else if (st < 0) seen_neg++; else seen_zero++;
    }
    CHECK("connected reachable", seen_pos  > 0);
    CHECK("sparse reachable",    seen_zero > 0);
    CHECK("isolated reachable",  seen_neg  > 0);

    printf("ok\n");
}

/* --- Snapshot entry count -------------------------------------------------
 *
 * Regression: the header recorded the field's declared route_count while only
 * the capped number of entries followed, describing a payload that was absent. */
static void test_snapshot_count(void) {
    printf("[snap]    ");
    CHECK("under cap -> actual", goose_policy_snap_entry_count(3, 32) == 3);
    CHECK("at cap -> cap",       goose_policy_snap_entry_count(32, 32) == 32);
    CHECK("over cap -> cap, never the declared count",
                                 goose_policy_snap_entry_count(100, 32) == 32);
    CHECK("zero routes -> zero", goose_policy_snap_entry_count(0, 32) == 0);
    printf("ok\n");
}

/* --- Mesh replay-cache slot ------------------------------------------------
 *
 * Regression: replay_slot_index blended the trailing MAC bytes in at bit
 * positions 8-23 and then masked the low 6 bits, so the slot was exactly
 * `nonce & 63` for every sender and the MAC was discarded entirely. Two peers
 * with colliding nonce low bits shared a slot, letting one evict the other's
 * entry and reopen the replay window. */
static void test_replay_slot(void) {
    printf("[replay]  ");
    const uint32_t SLOTS = 64;

    uint8_t a[6] = {0x02, 0x11, 0x22, 0x33, 0x44, 0x55};
    uint8_t b[6] = {0x02, 0x11, 0x22, 0x33, 0x44, 0x56}; /* differs in last byte  */
    uint8_t c[6] = {0x02, 0x11, 0x22, 0x99, 0x44, 0x55}; /* differs in byte 3 only */

    /* The property the old hash could not satisfy: the same nonce from
     * different senders must not always collide. */
    CHECK("same nonce, different MAC -> different slot",
          goose_policy_replay_slot(0xDEADBEEFu, a, SLOTS) !=
              goose_policy_replay_slot(0xDEADBEEFu, b, SLOTS));

    /* Bytes 0-3 were ignored outright by the old hash. */
    CHECK("MAC bytes outside the trailing pair affect the slot",
          goose_policy_replay_slot(0xDEADBEEFu, a, SLOTS) !=
              goose_policy_replay_slot(0xDEADBEEFu, c, SLOTS));

    /* Determinism: the cache is a lookup, not a sampler. */
    CHECK("slot is stable for a fixed (mac, nonce)",
          goose_policy_replay_slot(0x1234u, a, SLOTS) ==
              goose_policy_replay_slot(0x1234u, a, SLOTS));

    /* Nonce must still matter, or the cache degenerates the other way. */
    CHECK("same MAC, different nonce -> different slot",
          goose_policy_replay_slot(1u, a, SLOTS) != goose_policy_replay_slot(2u, a, SLOTS));

    /* Range, including a non-power-of-two count, since the constant is
     * tunable and the implementation no longer masks. */
    int in_range = 1;
    for (uint32_t n = 0; n < 4096; n++) {
        if (goose_policy_replay_slot(n, a, SLOTS) >= SLOTS) in_range = 0;
        if (goose_policy_replay_slot(n, a, 100u) >= 100u) in_range = 0;
    }
    CHECK("slot always within the cache", in_range);

    /* Degenerate inputs must not divide by zero or dereference NULL. */
    CHECK("zero slot count is floored, not a division by zero",
          goose_policy_replay_slot(7u, a, 0u) == 0u);
    CHECK("NULL mac is handled", goose_policy_replay_slot(7u, NULL, SLOTS) == 0u);

    /* Spread: with the old hash, 64 senders sharing a nonce landed in one
     * slot. Require that they now occupy a substantial fraction of the ring. */
    int occupied[64] = {0};
    int distinct = 0;
    for (uint32_t i = 0; i < 64; i++) {
        uint8_t m[6] = {0x02, 0x11, 0x22, 0x33, (uint8_t)i, 0x55};
        uint32_t slot = goose_policy_replay_slot(0xABCDu, m, SLOTS);
        if (!occupied[slot]) {
            occupied[slot] = 1;
            distinct++;
        }
    }
    CHECK("64 peers sharing a nonce spread across the ring", distinct > 32);

    printf("ok\n");
}

/* --- LoomScript route acceptance -------------------------------------------
 *
 * Regression: goose_weave_loom hardened its structural fields but copied
 * orientation and coupling straight off the wire. orientation is used as a
 * multiplicand, so a non-trit produces cell states outside the ternary set;
 * coupling RADIO makes every pulse of a 100 Hz REACTIVE field emit a mesh arc,
 * a permanent transmit loop woven by one admin-role upload. */
static void test_loom_route_acceptance(void) {
    printf("[loomacc] ");

    /* What loomc.py actually emits must keep working. */
    CHECK("orientation +1, SOFTWARE accepted",
          goose_policy_loom_route_acceptable(1, GOOSE_POLICY_COUPLING_SOFTWARE));
    CHECK("orientation 0 accepted",
          goose_policy_loom_route_acceptable(0, GOOSE_POLICY_COUPLING_SOFTWARE));
    CHECK("orientation -1 accepted",
          goose_policy_loom_route_acceptable(-1, GOOSE_POLICY_COUPLING_SOFTWARE));

    /* The audit's example value. */
    CHECK("orientation 7 rejected",
          !goose_policy_loom_route_acceptable(7, GOOSE_POLICY_COUPLING_SOFTWARE));
    CHECK("orientation -128 rejected (int8_t floor)",
          !goose_policy_loom_route_acceptable(-128, GOOSE_POLICY_COUPLING_SOFTWARE));
    CHECK("orientation 2 rejected (just outside the set)",
          !goose_policy_loom_route_acceptable(2, GOOSE_POLICY_COUPLING_SOFTWARE));

    /* The transmit loop. */
    CHECK("coupling RADIO rejected",
          !goose_policy_loom_route_acceptable(1, GOOSE_POLICY_COUPLING_RADIO));
    CHECK("coupling HARDWARE rejected",
          !goose_policy_loom_route_acceptable(1, GOOSE_POLICY_COUPLING_HARDWARE));

    /* Whitelist, not blacklist: every other uint8_t must be refused, including
     * modes the enum does not define yet. */
    int refused_all = 1;
    for (unsigned c = 0; c <= 255; c++) {
        if (c == GOOSE_POLICY_COUPLING_SOFTWARE) continue;
        if (goose_policy_loom_route_acceptable(1, c)) refused_all = 0;
    }
    CHECK("every coupling but SOFTWARE refused, defined or not", refused_all);

    /* And the full int8_t orientation range, so neither field can be waved
     * through by the other being valid. */
    int only_trits = 1;
    for (int o = -128; o <= 127; o++) {
        int ok = goose_policy_loom_route_acceptable(o, GOOSE_POLICY_COUPLING_SOFTWARE);
        if (ok != (o >= -1 && o <= 1)) only_trits = 0;
    }
    CHECK("exactly the three trits accepted across int8_t", only_trits);

    printf("ok\n");
}

/* Matches the harness convention: suites return their failure count. The
 * pass count is exposed separately because the runner's total otherwise
 * under-reports, counting only test_main's own assertions. */
int test_policy(void) {
    test_evictable();
    test_disposition();
    test_mesh_window();
    test_snapshot_count();
    test_replay_slot();
    test_loom_route_acceptance();
    return s_fail;
}

int test_policy_passed(void) { return s_pass; }
