/**
 * @file goose_lattice.c
 * @brief Open-addressed coordinate index. See goose_lattice.h for rationale.
 */

#include "goose_lattice.h"

bool goose_coord_equal(reflex_tryte9_t a, reflex_tryte9_t b)
{
    /* Moved here from goose_runtime.c. It is pure coordinate logic, it is the
     * inner loop of every index probe, and keeping it beside the index lets the
     * host suite link the index on its own. */
    for (int i = 0; i < 9; i++) { if (a.trits[i] != b.trits[i]) return false; }
    return true;
}

uint32_t goose_lattice_hash(reflex_tryte9_t coord, size_t bucket_count)
{
    /* Same mixing as the original direct-mapped table: base-3 positional over
     * all nine trits, each shifted into 0..2. Kept identical so the change is
     * confined to collision handling and not to bucket distribution. */
    uint32_t hash = 0;
    for (int i = 0; i < 9; i++) {
        hash = (hash * 3u) + (uint32_t)(coord.trits[i] + 1);
    }
    return (uint32_t)(hash % bucket_count);
}

void goose_lattice_clear(int16_t *slots, size_t bucket_count)
{
    if (!slots) return;
    for (size_t i = 0; i < bucket_count; i++) slots[i] = GOOSE_LATTICE_EMPTY;
}

/* Is the bucket entry valid and does it name a cell holding `coord`? */
static bool slot_matches(const int16_t *slots, size_t b,
                         const goose_cell_t *cells, size_t cell_count,
                         reflex_tryte9_t coord)
{
    int16_t idx = slots[b];
    if (idx == GOOSE_LATTICE_EMPTY) return false;
    if (idx < 0 || (size_t)idx >= cell_count) return false;
    return goose_coord_equal(cells[idx].coord, coord);
}

int16_t goose_lattice_find(const int16_t *slots, size_t bucket_count,
                           const goose_cell_t *cells, size_t cell_count,
                           reflex_tryte9_t coord)
{
    if (!slots || !cells || bucket_count == 0) return GOOSE_LATTICE_EMPTY;

    uint32_t b = goose_lattice_hash(coord, bucket_count);
    for (size_t probe = 0; probe < bucket_count; probe++) {
        /* An empty bucket ends the run: nothing beyond it can belong to this
         * key, because insertion never skips an empty bucket and removal
         * closes the gap behind itself. This is what makes a miss cheap. */
        if (slots[b] == GOOSE_LATTICE_EMPTY) return GOOSE_LATTICE_EMPTY;
        if (slot_matches(slots, b, cells, cell_count, coord)) return slots[b];
        b++; if (b == bucket_count) b = 0;
    }
    return GOOSE_LATTICE_EMPTY;
}

bool goose_lattice_insert(int16_t *slots, size_t bucket_count,
                          const goose_cell_t *cells, size_t cell_count,
                          reflex_tryte9_t coord, int16_t cell_idx)
{
    if (!slots || !cells || bucket_count == 0) return false;

    uint32_t b = goose_lattice_hash(coord, bucket_count);
    for (size_t probe = 0; probe < bucket_count; probe++) {
        if (slots[b] == GOOSE_LATTICE_EMPTY) { slots[b] = cell_idx; return true; }
        /* Re-mapping a coordinate that is already indexed: overwrite in place
         * rather than adding a second bucket for the same key. */
        if (slot_matches(slots, b, cells, cell_count, coord)) { slots[b] = cell_idx; return true; }
        b++; if (b == bucket_count) b = 0;
    }
    return false;   /* table full */
}

void goose_lattice_remove(int16_t *slots, size_t bucket_count,
                          const goose_cell_t *cells, size_t cell_count,
                          reflex_tryte9_t coord)
{
    if (!slots || !cells || bucket_count == 0) return;

    /* Locate the bucket holding this key. */
    uint32_t hole = goose_lattice_hash(coord, bucket_count);
    size_t probe = 0;
    for (; probe < bucket_count; probe++) {
        if (slots[hole] == GOOSE_LATTICE_EMPTY) return;   /* not present */
        if (slot_matches(slots, hole, cells, cell_count, coord)) break;
        hole++; if (hole == bucket_count) hole = 0;
    }
    if (probe == bucket_count) return;

    slots[hole] = GOOSE_LATTICE_EMPTY;

    /* Backward-shift deletion.
     *
     * Emptying a bucket mid-run would strand every later entry whose natural
     * bucket is at or before the hole, because find() stops at the first empty
     * bucket. Walk forward and pull back any entry that probed past the hole to
     * reach its current position, repeating from each vacated slot.
     *
     * Tombstones would be the alternative, but eviction churn here is
     * continuous — the Loom evicts on every allocation once saturated — so they
     * would accumulate until the table needed rehashing. */
    uint32_t j = hole;
    for (size_t step = 0; step < bucket_count; step++) {
        j++; if (j == bucket_count) j = 0;
        if (slots[j] == GOOSE_LATTICE_EMPTY) break;

        int16_t idx = slots[j];
        if (idx < 0 || (size_t)idx >= cell_count) {
            /* Stale entry naming a cell that no longer exists. Drop it: leaving
             * it would keep a run alive around a key nothing can match. */
            slots[j] = GOOSE_LATTICE_EMPTY;
            continue;
        }

        uint32_t home = goose_lattice_hash(cells[idx].coord, bucket_count);

        /* Does `home` lie cyclically outside the open interval (hole, j]? If so
         * this entry can move back into the hole without breaking its own run. */
        bool movable;
        if (hole <= j) movable = (home <= hole) || (home > j);
        else           movable = (home <= hole) && (home > j);

        if (movable) {
            slots[hole] = slots[j];
            slots[j] = GOOSE_LATTICE_EMPTY;
            hole = j;
        }
    }
}
