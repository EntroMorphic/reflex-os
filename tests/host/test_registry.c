/**
 * @file test_registry.c
 * @brief Tests for the goonies registry's name and coordinate indexes.
 *
 * These replace two linear scans that were, between them, the largest cost
 * under loom_authority. Removing a scan removes the thing that used to make a
 * broken index invisible, so as with the lattice the core of this file is a
 * differential test: every operation is mirrored against brute-force search
 * over the entry array, and the two must agree after each step.
 *
 * The case that most needs it is removal. Entries stay compact, so removing
 * one swaps the last entry down into the hole — which changes that entry's
 * index, and therefore both of its bucket mappings. A subtly wrong update
 * strands a live name at an index outside count, where it simply stops
 * resolving. Nothing else in the system would notice.
 */

#include <stdio.h>
#include <string.h>

#include "goose_registry.h"

static int s_pass = 0, s_fail = 0;

#define CHECK(name, expr) do { \
    if (expr) { s_pass++; } \
    else { printf("  FAIL: %s\n", name); s_fail++; } \
} while (0)

#define BUCKETS  503
#define CAPACITY 256

static goose_registry_entry_t g_entries[CAPACITY];
static int16_t g_by_name[BUCKETS];
static int16_t g_by_coord[BUCKETS];
static goose_registry_t g_reg;

/* Oracles: what the old linear scans did. */
static int16_t ref_find_name(const char *name)
{
    for (size_t i = 0; i < g_reg.count; i++) {
        if (strcmp(g_entries[i].name, name) == 0) return (int16_t)i;
    }
    return GOOSE_REGISTRY_EMPTY;
}

static int16_t ref_find_coord(reflex_tryte9_t c)
{
    for (size_t i = 0; i < g_reg.count; i++) {
        if (goose_coord_equal(g_entries[i].coord, c)) return (int16_t)i;
    }
    return GOOSE_REGISTRY_EMPTY;
}

/* Names built the way the atlas builds them: a shared prefix and a varying
 * tail, so the hash is tested on the distribution it actually sees. */
static void name_of(int seed, char *out, size_t n)
{
    snprintf(out, n, "shadow.gpio.out.reg%d", seed);
}

static reflex_tryte9_t coord_of(int seed)
{
    reflex_tryte9_t c = {{0}};
    for (int i = 0; i < 6; i++) c.trits[i] = (reflex_trit_t)(((seed >> i) % 3) - 1);
    c.trits[6] = (reflex_trit_t)(seed % 128);
    c.trits[7] = (reflex_trit_t)((seed / 128) % 128);
    c.trits[8] = 1;   /* shadow namespace, as paged cells use */
    return c;
}

static void reset(void)
{
    memset(g_entries, 0, sizeof(g_entries));
    goose_registry_init(&g_reg, g_entries, CAPACITY, g_by_name, g_by_coord, BUCKETS);
}

static void test_basic(void)
{
    printf("[registry]");
    reset();
    char nm[GOOSE_REGISTRY_NAME_MAX];

    CHECK("empty: name miss", goose_registry_find_name(&g_reg, "sys.kernel") == GOOSE_REGISTRY_EMPTY);
    CHECK("empty: coord miss", goose_registry_find_coord(&g_reg, coord_of(1)) == GOOSE_REGISTRY_EMPTY);

    name_of(1, nm, sizeof(nm));
    int16_t a = goose_registry_add(&g_reg, nm, coord_of(1));
    CHECK("add returns an index", a == 0);
    CHECK("find by name", goose_registry_find_name(&g_reg, nm) == 0);
    CHECK("find by coord", goose_registry_find_coord(&g_reg, coord_of(1)) == 0);

    /* Re-keying a coordinate must move the coord mapping and leave the name
     * mapping alone. */
    goose_registry_set_coord(&g_reg, 0, coord_of(2));
    CHECK("re-key: new coord found", goose_registry_find_coord(&g_reg, coord_of(2)) == 0);
    CHECK("re-key: old coord gone", goose_registry_find_coord(&g_reg, coord_of(1)) == GOOSE_REGISTRY_EMPTY);
    CHECK("re-key: name still resolves", goose_registry_find_name(&g_reg, nm) == 0);

    CHECK("remove by coord", goose_registry_remove_coord(&g_reg, coord_of(2)));
    CHECK("removed: name gone", goose_registry_find_name(&g_reg, nm) == GOOSE_REGISTRY_EMPTY);
    CHECK("removed: coord gone", goose_registry_find_coord(&g_reg, coord_of(2)) == GOOSE_REGISTRY_EMPTY);
    CHECK("removing absent coord reports false",
          goose_registry_remove_coord(&g_reg, coord_of(99)) == false);

    /* The swap-down case, explicitly: remove entry 0 of three and confirm the
     * entry that moved into its slot is still reachable by both keys. */
    reset();
    char n0[40], n1[40], n2[40];
    name_of(10, n0, sizeof(n0)); name_of(11, n1, sizeof(n1)); name_of(12, n2, sizeof(n2));
    goose_registry_add(&g_reg, n0, coord_of(10));
    goose_registry_add(&g_reg, n1, coord_of(11));
    goose_registry_add(&g_reg, n2, coord_of(12));
    goose_registry_remove_coord(&g_reg, coord_of(10));
    CHECK("swap-down: count decremented", g_reg.count == 2);
    CHECK("swap-down: moved entry found by name", goose_registry_find_name(&g_reg, n2) == 0);
    CHECK("swap-down: moved entry found by coord", goose_registry_find_coord(&g_reg, coord_of(12)) == 0);
    CHECK("swap-down: untouched entry still found", goose_registry_find_name(&g_reg, n1) == 1);
    CHECK("swap-down: removed name gone", goose_registry_find_name(&g_reg, n0) == GOOSE_REGISTRY_EMPTY);

    /* Colliding names must both stay reachable — the whole point of probing. */
    reset();
    uint32_t target = goose_registry_name_hash("sys.kernel.tick", BUCKETS);
    char partner[40] = {0};
    for (int s = 0; s < 400000 && !partner[0]; s++) {
        char cand[40];
        name_of(s, cand, sizeof(cand));
        if (goose_registry_name_hash(cand, BUCKETS) == target) memcpy(partner, cand, sizeof(partner));
    }
    CHECK("found a colliding name pair", partner[0] != 0);
    if (partner[0]) {
        goose_registry_add(&g_reg, "sys.kernel.tick", coord_of(500));
        goose_registry_add(&g_reg, partner, coord_of(501));
        CHECK("colliding name A reachable", goose_registry_find_name(&g_reg, "sys.kernel.tick") == 0);
        CHECK("colliding name B reachable", goose_registry_find_name(&g_reg, partner) == 1);
        /* Removing A must not strand B behind an emptied bucket. */
        goose_registry_remove_coord(&g_reg, coord_of(500));
        CHECK("B survives removal of A", goose_registry_find_name(&g_reg, partner) != GOOSE_REGISTRY_EMPTY);
    }

    /* reindex must reproduce the same answers from the entries alone. */
    reset();
    for (int i = 0; i < 64; i++) { name_of(i, nm, sizeof(nm)); goose_registry_add(&g_reg, nm, coord_of(i)); }
    goose_registry_reindex(&g_reg);
    int reindex_ok = 1;
    for (int i = 0; i < 64; i++) {
        name_of(i, nm, sizeof(nm));
        if (goose_registry_find_name(&g_reg, nm) != ref_find_name(nm)) reindex_ok = 0;
        if (goose_registry_find_coord(&g_reg, coord_of(i)) != ref_find_coord(coord_of(i))) reindex_ok = 0;
    }
    CHECK("reindex reproduces both indexes", reindex_ok);

    printf(" ok\n");
}

/* Differential soak: fill, then churn the way eviction does, checking both
 * indexes against brute force after every step. */
static void test_differential(void)
{
    printf("[reg-diff]");
    reset();
    char nm[GOOSE_REGISTRY_NAME_MAX];

    /* Phase 1: fill to capacity. */
    for (int i = 0; i < CAPACITY; i++) {
        name_of(i, nm, sizeof(nm));
        goose_registry_add(&g_reg, nm, coord_of(i));
    }
    CHECK("filled to capacity", g_reg.count == CAPACITY);
    CHECK("full table rejects further adds",
          goose_registry_add(&g_reg, "one.too.many", coord_of(99999)) == GOOSE_REGISTRY_EMPTY);

    int lost = 0;
    for (int i = 0; i < CAPACITY; i++) {
        name_of(i, nm, sizeof(nm));
        if (goose_registry_find_name(&g_reg, nm) != ref_find_name(nm)) lost++;
        if (goose_registry_find_coord(&g_reg, coord_of(i)) != ref_find_coord(coord_of(i))) lost++;
    }
    CHECK("every entry reachable by both keys when full", lost == 0);

    /* Phase 2: remove-and-replace churn, as fabric_alloc_internal does once the
     * fabric saturates. */
    int mismatches = 0;
    unsigned rng = 987654321u;
    int next_seed = CAPACITY;

    for (int step = 0; step < 20000; step++) {
        rng = rng * 1103515245u + 12345u;
        int victim = (int)((rng >> 16) % g_reg.count);
        reflex_tryte9_t vc = g_entries[victim].coord;
        char vn[GOOSE_REGISTRY_NAME_MAX];
        memcpy(vn, g_entries[victim].name, sizeof(vn));

        if (!goose_registry_remove_coord(&g_reg, vc)) mismatches++;
        if (goose_registry_find_name(&g_reg, vn) != ref_find_name(vn)) mismatches++;
        if (goose_registry_find_coord(&g_reg, vc) != ref_find_coord(vc)) mismatches++;

        /* Replace with a fresh entry, as the new cell's registration would.
         * Seeds are monotonic so names and coords are unique by construction —
         * the registry, like the fabric, never holds a duplicate of either. */
        name_of(next_seed, nm, sizeof(nm));
        goose_registry_add(&g_reg, nm, coord_of(next_seed));
        next_seed++;

        /* Re-key an existing entry, as goonies_register does when a known name
         * is registered at a new coordinate (the warm-boot re-weave path).
         *
         * This was missing at first, and the omission mattered: deleting the
         * old-coordinate unmap from goose_registry_set_coord left every test
         * passing. Lookups still answered correctly — a stale bucket names an
         * entry that no longer holds that key, so the match check rejects it —
         * but the bucket is never reclaimed, and each one holds a probe run
         * open. Enough of them and the table stops having empty buckets to
         * terminate on, which is the failure the index exists to avoid. Only
         * the drain check at the end of this soak can see it. */
        if ((step % 3) == 0 && g_reg.count > 0) {
            rng = rng * 1103515245u + 12345u;
            int rk = (int)((rng >> 16) % g_reg.count);
            reflex_tryte9_t old_c = g_entries[rk].coord;
            reflex_tryte9_t new_c = coord_of(next_seed);
            next_seed++;
            goose_registry_set_coord(&g_reg, (int16_t)rk, new_c);
            if (goose_registry_find_coord(&g_reg, new_c) != (int16_t)rk) mismatches++;
            if (goose_registry_find_coord(&g_reg, old_c) != ref_find_coord(old_c)) mismatches++;
            if (goose_registry_find_name(&g_reg, g_entries[rk].name) != (int16_t)rk) mismatches++;
        }

        /* Spot-check live entries against both oracles. */
        for (int k = 0; k < 3; k++) {
            rng = rng * 1103515245u + 12345u;
            int probe = (int)((rng >> 16) % g_reg.count);
            const char *pn = g_entries[probe].name;
            reflex_tryte9_t pc = g_entries[probe].coord;
            if (goose_registry_find_name(&g_reg, pn) != ref_find_name(pn)) mismatches++;
            if (goose_registry_find_coord(&g_reg, pc) != ref_find_coord(pc)) mismatches++;
        }
    }
    CHECK("20000 remove/add cycles agree with brute force on both keys", mismatches == 0);

    /* Full sweep: nothing stranded by the shifting or the swap-down. */
    lost = 0;
    for (size_t i = 0; i < g_reg.count; i++) {
        if (goose_registry_find_name(&g_reg, g_entries[i].name) != (int16_t)i) lost++;
        if (goose_registry_find_coord(&g_reg, g_entries[i].coord) != (int16_t)i) lost++;
    }
    CHECK("no entry stranded after sustained churn", lost == 0);

    /* Absent keys must report absent. */
    int false_hits = 0;
    for (int s = 900000; s < 900500; s++) {
        name_of(s, nm, sizeof(nm));
        if (goose_registry_find_name(&g_reg, nm) != GOOSE_REGISTRY_EMPTY) false_hits++;
        if (goose_registry_find_coord(&g_reg, coord_of(s)) != ref_find_coord(coord_of(s))) false_hits++;
    }
    CHECK("absent keys report absent", false_hits == 0);

    /* Draining must leave both tables genuinely empty, not full of half-closed
     * runs that would slow every future miss. */
    while (g_reg.count > 0) goose_registry_remove_coord(&g_reg, g_entries[0].coord);
    int occupied = 0;
    for (int b = 0; b < BUCKETS; b++) {
        if (g_by_name[b] != GOOSE_REGISTRY_EMPTY) occupied++;
        if (g_by_coord[b] != GOOSE_REGISTRY_EMPTY) occupied++;
    }
    CHECK("both tables fully drain", occupied == 0);

    printf(" ok\n");
}

int test_registry(void)
{
    test_basic();
    test_differential();
    return s_fail;
}

int test_registry_passed(void) { return s_pass; }
