/**
 * @file goose_registry.h
 * @brief Name/coordinate index for the goonies registry.
 *
 * The registry maps a cell name to its coordinate. It was a flat array walked
 * linearly by both keys, and every allocation paid for it twice:
 *
 *   - by coordinate, to find the evicted victim's entry. Measured on hardware
 *     at 688us — the largest single term in the Loom's peak hold. 256 entries
 *     at 76 bytes each is ~19KB, and the scan touches only the 36-byte coord
 *     inside each, so it misses cache on essentially every step.
 *   - by name, to decide register-vs-update. Measured at 100us of strcmp over
 *     names sharing long prefixes ("shadow.gpio.*").
 *
 * Both are now open-addressed indexes with linear probing and backward-shift
 * deletion, the same construction goose_lattice.c uses for the fabric.
 *
 * The probe logic here is a second copy of what goose_lattice.c contains, which
 * is duplication worth naming. Collapsing them needs a key-agnostic core taking
 * hash and match as callbacks; the lattice was validated on hardware only in
 * the previous change, and rewriting it to share code with this one would put
 * both at risk at once. Each is differential-tested against brute force
 * independently. The merge is a separate, safer change once both have run.
 *
 * The index owns no memory: the caller supplies the entry array and both bucket
 * arrays, so this unit is pure and runs on host.
 *
 * Entries stay compact (0..count-1) because goonies_get_name_by_idx and its
 * callers iterate that range. Removal therefore swaps the last entry down into
 * the hole, which moves that entry's index — so both of its bucket mappings are
 * rewritten to follow it. Getting that wrong strands a live name, which is
 * precisely what the differential test exists to catch.
 */
#ifndef GOOSE_REGISTRY_H
#define GOOSE_REGISTRY_H

#include "goose.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/** Bucket holds no entry. A probe run stops here. */
#define GOOSE_REGISTRY_EMPTY ((int16_t)-1)

/** Longest name the registry stores, including the terminator. */
#define GOOSE_REGISTRY_NAME_MAX 40

typedef struct {
    char name[GOOSE_REGISTRY_NAME_MAX];
    reflex_tryte9_t coord;
} goose_registry_entry_t;

typedef struct {
    goose_registry_entry_t *entries;
    size_t capacity;
    size_t count;
    int16_t *by_name;   /**< bucket -> entry index, keyed on name  */
    int16_t *by_coord;  /**< bucket -> entry index, keyed on coord */
    size_t buckets;     /**< length of each bucket array           */
} goose_registry_t;

/**
 * @brief Bind storage to @p reg and empty it.
 *
 * @p buckets must exceed @p capacity so a probe run always meets an empty
 * bucket; the load factor also sets probe length, so give it room.
 */
void goose_registry_init(goose_registry_t *reg,
                         goose_registry_entry_t *entries, size_t capacity,
                         int16_t *by_name, int16_t *by_coord, size_t buckets);

/** @brief Bucket for a name. Exposed for tests. */
uint32_t goose_registry_name_hash(const char *name, size_t bucket_count);

/**
 * @brief Find the entry holding @p name.
 * @return entry index, or GOOSE_REGISTRY_EMPTY if absent.
 */
int16_t goose_registry_find_name(const goose_registry_t *reg, const char *name);

/**
 * @brief Find the entry holding @p coord.
 * @return entry index, or GOOSE_REGISTRY_EMPTY if absent.
 */
int16_t goose_registry_find_coord(const goose_registry_t *reg, reflex_tryte9_t coord);

/**
 * @brief Append an entry for @p name at @p coord.
 * @return the new entry index, or GOOSE_REGISTRY_EMPTY if full.
 *
 * The caller must have established that @p name is absent — this does not
 * check, because every caller has just looked it up.
 */
int16_t goose_registry_add(goose_registry_t *reg, const char *name, reflex_tryte9_t coord);

/**
 * @brief Point an existing entry at a different coordinate.
 *
 * Re-keys the coordinate index; the name index is untouched because the name
 * does not change.
 */
void goose_registry_set_coord(goose_registry_t *reg, int16_t idx, reflex_tryte9_t coord);

/**
 * @brief Remove the entry holding @p coord, keeping entries compact.
 * @return true if an entry was removed.
 */
bool goose_registry_remove_coord(goose_registry_t *reg, reflex_tryte9_t coord);

/**
 * @brief Rebuild both indexes from the entry array.
 *
 * Not called by the firmware today: the entry array is ordinary BSS, so a warm
 * boot zeroes entries and indexes alike and the atlas re-weave repopulates both
 * through goose_registry_add. This exists for the host tests, and for any
 * future path that restores entries directly — a snapshot load, or moving the
 * entries into retained memory beside fabric_cells[].
 */
void goose_registry_reindex(goose_registry_t *reg);

#endif /* GOOSE_REGISTRY_H */
