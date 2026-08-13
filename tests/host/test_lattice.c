/**
 * @file test_lattice.c
 * @brief Tests for the fabric coordinate index.
 *
 * The index this replaces was direct-mapped and overwrote on collision, which
 * left roughly a fifth of cells reachable only by a full linear scan. Removing
 * that scan is the point of the change — so the index has to be right, because
 * nothing catches it being wrong any more.
 *
 * The core of this file is therefore a differential test: every operation is
 * mirrored against a brute-force reference (linear search over the cell array,
 * which is exactly what the old fallback did) and the two must agree after
 * every single step. Probing and backward-shift deletion are easy to get
 * subtly wrong in ways that only show up after a specific interleaving, and
 * hand-picked cases would not find those.
 */

#include <stdio.h>
#include <string.h>

#include "goose_lattice.h"

static int s_pass = 0, s_fail = 0;

/* Fill-phase seeds are distinct by construction; assert rather than ignore. */
#define CHECK_SILENT(expr) do { if (!(expr)) { printf("  FAIL: duplicate seed in fill\n"); s_fail++; } } while (0)

#define CHECK(name, expr) do { \
    if (expr) { s_pass++; } \
    else { printf("  FAIL: %s\n", name); s_fail++; } \
} while (0)

#define BUCKETS 503
#define CELLS   256

static int16_t g_slots[BUCKETS];
static goose_cell_t g_cells[CELLS];
static bool g_live[CELLS];

/* The old linear fallback, kept as the oracle. */
static int16_t reference_find(reflex_tryte9_t coord)
{
    for (size_t i = 0; i < CELLS; i++) {
        if (g_live[i] && goose_coord_equal(g_cells[i].coord, coord)) return (int16_t)i;
    }
    return GOOSE_LATTICE_EMPTY;
}

static reflex_tryte9_t coord_of(int seed)
{
    /* Spread across all nine trits so buckets collide realistically rather
     * than degenerately. */
    reflex_tryte9_t c = {{0}};
    for (int i = 0; i < 9; i++) c.trits[i] = (reflex_trit_t)(((seed >> i) % 3) - 1);
    c.trits[0] = (reflex_trit_t)((seed % 3) - 1);
    c.trits[6] = (reflex_trit_t)((seed / 3) % 128);
    c.trits[7] = (reflex_trit_t)((seed / 384) % 128);
    return c;
}

static void reset(void)
{
    goose_lattice_clear(g_slots, BUCKETS);
    memset(g_cells, 0, sizeof(g_cells));
    memset(g_live, 0, sizeof(g_live));
}

/* Add a cell, preserving the invariant the real fabric enforces: no two live
 * cells share a coordinate. goose_fabric_alloc_cell refuses an occupied
 * coordinate outright, so a duplicate cannot arise there.
 *
 * The first version of this helper ignored that and inserted whatever
 * coord_of(seed) produced. The differential test duly failed — 116 of 20000
 * churn steps reused a live coordinate, and with two cells holding the same
 * key the brute-force oracle (first match wins) and the index (whichever
 * mapping was written last) disagree by construction. That was the test
 * modelling something the system cannot do, not a defect in the index.
 *
 * @return true if the cell was added. */
static bool add(int slot, int seed)
{
    reflex_tryte9_t c = coord_of(seed);
    for (int i = 0; i < CELLS; i++) {
        if (g_live[i] && goose_coord_equal(g_cells[i].coord, c)) return false;
    }
    g_cells[slot].coord = c;
    g_live[slot] = true;
    goose_lattice_insert(g_slots, BUCKETS, g_cells, CELLS, c, (int16_t)slot);
    return true;
}

static void evict(int slot)
{
    goose_lattice_remove(g_slots, BUCKETS, g_cells, CELLS, g_cells[slot].coord);
    g_live[slot] = false;
    memset(&g_cells[slot], 0, sizeof(g_cells[slot]));
}

static void test_basic(void)
{
    printf("[lattice] ");
    reset();

    CHECK("empty table: miss", goose_lattice_find(g_slots, BUCKETS, g_cells, CELLS, coord_of(1))
                               == GOOSE_LATTICE_EMPTY);
    add(0, 1);
    CHECK("insert then find", goose_lattice_find(g_slots, BUCKETS, g_cells, CELLS, coord_of(1)) == 0);
    CHECK("absent key still misses",
          goose_lattice_find(g_slots, BUCKETS, g_cells, CELLS, coord_of(2)) == GOOSE_LATTICE_EMPTY);

    evict(0);
    CHECK("after remove: miss",
          goose_lattice_find(g_slots, BUCKETS, g_cells, CELLS, coord_of(1)) == GOOSE_LATTICE_EMPTY);

    /* Force a collision run: find seeds that share a bucket, then verify both
     * are reachable — the exact case the direct-mapped table lost. */
    reset();
    uint32_t target = goose_lattice_hash(coord_of(1), BUCKETS);
    int partner = -1;
    for (int s = 2; s < 200000 && partner < 0; s++) {
        if (goose_lattice_hash(coord_of(s), BUCKETS) == target) partner = s;
    }
    CHECK("found a colliding pair to test with", partner > 0);
    if (partner > 0) {
        add(0, 1);
        add(1, partner);
        CHECK("colliding key A reachable",
              goose_lattice_find(g_slots, BUCKETS, g_cells, CELLS, coord_of(1)) == 0);
        CHECK("colliding key B reachable (direct-mapped lost this one)",
              goose_lattice_find(g_slots, BUCKETS, g_cells, CELLS, coord_of(partner)) == 1);

        /* Removing the first must not strand the second behind an empty slot. */
        evict(0);
        CHECK("B survives removal of A",
              goose_lattice_find(g_slots, BUCKETS, g_cells, CELLS, coord_of(partner)) == 1);
    }

    printf("ok\n");
}

/* Differential soak: fill, then churn with eviction the way the Loom does,
 * checking the index against brute force after every operation. */
static void test_differential(void)
{
    printf("[lat-diff]");
    reset();

    int mismatches = 0;
    unsigned rng = 12345u;

    /* Phase 1: fill to capacity. */
    for (int i = 0; i < CELLS; i++) {
        CHECK_SILENT(add(i, i * 7 + 1));
        if (goose_lattice_find(g_slots, BUCKETS, g_cells, CELLS, g_cells[i].coord)
            != reference_find(g_cells[i].coord)) mismatches++;
    }
    CHECK("fill to capacity agrees with brute force", mismatches == 0);

    /* Every live key must be findable once full — this is where the old table
     * silently lost ~21% of cells. */
    int lost = 0;
    for (int i = 0; i < CELLS; i++) {
        if (goose_lattice_find(g_slots, BUCKETS, g_cells, CELLS, g_cells[i].coord) != (int16_t)i) lost++;
    }
    CHECK("all 256 cells reachable through the index when full", lost == 0);

    /* Phase 2: evict-and-replace churn, as goose_fabric_alloc_cell does. */
    mismatches = 0;
    for (int step = 0; step < 20000; step++) {
        rng = rng * 1103515245u + 12345u;
        int slot = (int)((rng >> 16) % CELLS);
        evict(slot);
        /* Retry until a free coordinate is found, as the substrate would. */
        for (int attempt = 0; attempt < 8; attempt++) {
            rng = rng * 1103515245u + 12345u;
            if (add(slot, (int)((rng >> 8) % 100000) + 1000)) break;
        }

        /* Spot-check a few keys against the oracle each step. */
        for (int k = 0; k < 3; k++) {
            rng = rng * 1103515245u + 12345u;
            int probe = (int)((rng >> 16) % CELLS);
            reflex_tryte9_t c = g_live[probe] ? g_cells[probe].coord : coord_of(999999);
            if (goose_lattice_find(g_slots, BUCKETS, g_cells, CELLS, c) != reference_find(c)) mismatches++;
        }
    }
    CHECK("20000 evict/insert cycles agree with brute force", mismatches == 0);

    /* Full sweep at the end: nothing stranded by the shifting. */
    lost = 0;
    for (int i = 0; i < CELLS; i++) {
        if (!g_live[i]) continue;
        if (goose_lattice_find(g_slots, BUCKETS, g_cells, CELLS, g_cells[i].coord) != (int16_t)i) lost++;
    }
    CHECK("no cell stranded after sustained churn", lost == 0);

    /* Absent keys must miss, and must miss cheaply — the allocation case. */
    int false_hits = 0;
    for (int s = 500000; s < 500500; s++) {
        if (goose_lattice_find(g_slots, BUCKETS, g_cells, CELLS, coord_of(s)) != GOOSE_LATTICE_EMPTY) {
            if (reference_find(coord_of(s)) == GOOSE_LATTICE_EMPTY) false_hits++;
        }
    }
    CHECK("absent keys report absent", false_hits == 0);

    /* Emptying the table must leave every bucket empty, not a field of
     * half-closed runs. */
    for (int i = 0; i < CELLS; i++) if (g_live[i]) evict(i);
    int occupied = 0;
    for (int b = 0; b < BUCKETS; b++) if (g_slots[b] != GOOSE_LATTICE_EMPTY) occupied++;
    CHECK("table fully drains after removing everything", occupied == 0);

    printf(" ok\n");
}

int test_lattice(void)
{
    test_basic();
    test_differential();
    return s_fail;
}

int test_lattice_passed(void) { return s_pass; }
