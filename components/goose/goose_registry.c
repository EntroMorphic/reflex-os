/**
 * @file goose_registry.c
 * @brief Open-addressed name and coordinate indexes. See goose_registry.h.
 */

#include "goose_registry.h"
#include "goose_lattice.h"   /* goose_coord_equal */

#include <stdio.h>
#include <string.h>

/* Both indexes share one probe implementation, selected by key kind. The
 * alternative was two near-identical copies of backward-shift deletion in the
 * same file, and that is the part worth having exactly once. */
typedef enum { KEY_NAME, KEY_COORD } key_kind_t;

typedef struct {
    key_kind_t kind;
    const char *name;        /* KEY_NAME  */
    reflex_tryte9_t coord;   /* KEY_COORD */
} key_t;

uint32_t goose_registry_name_hash(const char *name, size_t bucket_count)
{
    /* FNV-1a. Registry names share long prefixes ("shadow.gpio.out.*"), so the
     * hash has to mix every byte rather than favour the leading ones. */
    if (!name || bucket_count == 0) return 0;
    uint32_t h = 2166136261u;
    for (const unsigned char *p = (const unsigned char *)name; *p; p++) {
        h ^= (uint32_t)*p;
        h *= 16777619u;
    }
    return (uint32_t)(h % bucket_count);
}

static uint32_t key_hash(const key_t *k, size_t bucket_count)
{
    return (k->kind == KEY_NAME) ? goose_registry_name_hash(k->name, bucket_count)
                                 : goose_lattice_hash(k->coord, bucket_count);
}

/* Does entry @p idx hold this key? Entries at or beyond count are dead. */
static bool entry_matches(const goose_registry_t *reg, int16_t idx, const key_t *k)
{
    if (idx < 0 || (size_t)idx >= reg->count) return false;
    const goose_registry_entry_t *e = &reg->entries[idx];
    if (k->kind == KEY_NAME) {
        return k->name && strncmp(e->name, k->name, GOOSE_REGISTRY_NAME_MAX) == 0;
    }
    return goose_coord_equal(e->coord, k->coord);
}

/* The key entry @p idx is currently stored under, for the given index. */
static key_t key_of(const goose_registry_t *reg, int16_t idx, key_kind_t kind)
{
    key_t k = {.kind = kind, .name = NULL, .coord = {{0}}};
    if (kind == KEY_NAME) k.name = reg->entries[idx].name;
    else                  k.coord = reg->entries[idx].coord;
    return k;
}

static int16_t idx_find(const goose_registry_t *reg, const int16_t *slots, const key_t *k)
{
    uint32_t b = key_hash(k, reg->buckets);
    for (size_t probe = 0; probe < reg->buckets; probe++) {
        /* An empty bucket ends the run — insertion never skips one and removal
         * closes the gap behind itself. This is what makes a miss cheap. */
        if (slots[b] == GOOSE_REGISTRY_EMPTY) return GOOSE_REGISTRY_EMPTY;
        if (entry_matches(reg, slots[b], k)) return slots[b];
        b++; if (b == reg->buckets) b = 0;
    }
    return GOOSE_REGISTRY_EMPTY;
}

static void idx_insert(goose_registry_t *reg, int16_t *slots, const key_t *k, int16_t value)
{
    uint32_t b = key_hash(k, reg->buckets);
    for (size_t probe = 0; probe < reg->buckets; probe++) {
        if (slots[b] == GOOSE_REGISTRY_EMPTY) { slots[b] = value; return; }
        if (entry_matches(reg, slots[b], k)) { slots[b] = value; return; }
        b++; if (b == reg->buckets) b = 0;
    }
    /* Unreachable while buckets > capacity, which init enforces by contract. */
}

/* Remove @p k's mapping and close the run behind it. Must be called while the
 * entry still holds the key, or there is nothing to find. */
static void idx_remove(goose_registry_t *reg, int16_t *slots, const key_t *k, key_kind_t kind)
{
    uint32_t hole = key_hash(k, reg->buckets);
    size_t probe = 0;
    for (; probe < reg->buckets; probe++) {
        if (slots[hole] == GOOSE_REGISTRY_EMPTY) return;   /* not present */
        if (entry_matches(reg, slots[hole], k)) break;
        hole++; if (hole == reg->buckets) hole = 0;
    }
    if (probe == reg->buckets) return;

    slots[hole] = GOOSE_REGISTRY_EMPTY;

    /* Backward-shift deletion. Emptying a bucket mid-run would strand every
     * later entry whose home bucket lies at or before the hole, because a probe
     * stops at the first empty bucket. Pull such entries back, repeating from
     * each newly vacated slot.
     *
     * Tombstones are the alternative and are wrong here for the same reason
     * they are wrong in the lattice: eviction is continuous once the fabric
     * saturates, so they would accumulate until the table needed rehashing. */
    uint32_t j = hole;
    for (size_t step = 0; step < reg->buckets; step++) {
        j++; if (j == reg->buckets) j = 0;
        if (slots[j] == GOOSE_REGISTRY_EMPTY) break;

        int16_t idx = slots[j];
        if (idx < 0 || (size_t)idx >= reg->count) {
            /* Names an entry that no longer exists. Drop it rather than let it
             * hold a run open around a key nothing can match. */
            slots[j] = GOOSE_REGISTRY_EMPTY;
            continue;
        }

        key_t jk = key_of(reg, idx, kind);
        uint32_t home = key_hash(&jk, reg->buckets);

        /* Movable if `home` lies cyclically outside the open interval (hole, j],
         * meaning it can sit in the hole without breaking its own run. */
        bool movable;
        if (hole <= j) movable = (home <= hole) || (home > j);
        else           movable = (home <= hole) && (home > j);

        if (movable) {
            slots[hole] = slots[j];
            slots[j] = GOOSE_REGISTRY_EMPTY;
            hole = j;
        }
    }
}

void goose_registry_init(goose_registry_t *reg,
                         goose_registry_entry_t *entries, size_t capacity,
                         int16_t *by_name, int16_t *by_coord, size_t buckets)
{
    if (!reg) return;
    reg->entries = entries;
    reg->capacity = capacity;
    reg->count = 0;
    reg->by_name = by_name;
    reg->by_coord = by_coord;
    reg->buckets = buckets;
    for (size_t i = 0; i < buckets; i++) {
        by_name[i] = GOOSE_REGISTRY_EMPTY;
        by_coord[i] = GOOSE_REGISTRY_EMPTY;
    }
}

void goose_registry_reindex(goose_registry_t *reg)
{
    if (!reg || !reg->entries) return;
    for (size_t i = 0; i < reg->buckets; i++) {
        reg->by_name[i] = GOOSE_REGISTRY_EMPTY;
        reg->by_coord[i] = GOOSE_REGISTRY_EMPTY;
    }
    for (size_t i = 0; i < reg->count; i++) {
        key_t nk = key_of(reg, (int16_t)i, KEY_NAME);
        key_t ck = key_of(reg, (int16_t)i, KEY_COORD);
        idx_insert(reg, reg->by_name, &nk, (int16_t)i);
        idx_insert(reg, reg->by_coord, &ck, (int16_t)i);
    }
}

int16_t goose_registry_find_name(const goose_registry_t *reg, const char *name)
{
    if (!reg || !reg->entries || !name) return GOOSE_REGISTRY_EMPTY;
    key_t k = {.kind = KEY_NAME, .name = name, .coord = {{0}}};
    return idx_find(reg, reg->by_name, &k);
}

int16_t goose_registry_find_coord(const goose_registry_t *reg, reflex_tryte9_t coord)
{
    if (!reg || !reg->entries) return GOOSE_REGISTRY_EMPTY;
    key_t k = {.kind = KEY_COORD, .name = NULL, .coord = coord};
    return idx_find(reg, reg->by_coord, &k);
}

int16_t goose_registry_add(goose_registry_t *reg, const char *name, reflex_tryte9_t coord)
{
    if (!reg || !reg->entries || !name) return GOOSE_REGISTRY_EMPTY;
    if (reg->count >= reg->capacity) return GOOSE_REGISTRY_EMPTY;
    /* Refuse rather than truncate. A truncated entry is stored under a key no
     * caller can look up, so it is never found and every subsequent attempt
     * appends another copy. See GOOSE_REGISTRY_NAME_MAX. */
    if (strlen(name) >= GOOSE_REGISTRY_NAME_MAX) return GOOSE_REGISTRY_EMPTY;

    int16_t idx = (int16_t)reg->count;
    snprintf(reg->entries[idx].name, GOOSE_REGISTRY_NAME_MAX, "%s", name);
    reg->entries[idx].coord = coord;
    reg->count++;   /* before indexing: entry_matches rejects idx >= count */

    key_t nk = key_of(reg, idx, KEY_NAME);
    key_t ck = key_of(reg, idx, KEY_COORD);
    idx_insert(reg, reg->by_name, &nk, idx);
    idx_insert(reg, reg->by_coord, &ck, idx);
    return idx;
}

void goose_registry_set_coord(goose_registry_t *reg, int16_t idx, reflex_tryte9_t coord)
{
    if (!reg || !reg->entries) return;
    if (idx < 0 || (size_t)idx >= reg->count) return;

    /* Unmap under the old coordinate first — once the entry is overwritten the
     * old mapping cannot be located, and would be left pointing at an entry
     * that no longer holds that key. */
    key_t old = key_of(reg, idx, KEY_COORD);
    if (goose_coord_equal(old.coord, coord)) return;   /* nothing to re-key */
    idx_remove(reg, reg->by_coord, &old, KEY_COORD);

    reg->entries[idx].coord = coord;
    key_t nk = key_of(reg, idx, KEY_COORD);
    idx_insert(reg, reg->by_coord, &nk, idx);
}

bool goose_registry_remove_coord(goose_registry_t *reg, reflex_tryte9_t coord)
{
    if (!reg || !reg->entries || reg->count == 0) return false;

    int16_t idx = goose_registry_find_coord(reg, coord);
    if (idx == GOOSE_REGISTRY_EMPTY) return false;

    int16_t last = (int16_t)(reg->count - 1);

    /* Unmap the victim under both keys while its entry is still intact. */
    key_t vn = key_of(reg, idx, KEY_NAME);
    key_t vc = key_of(reg, idx, KEY_COORD);
    idx_remove(reg, reg->by_name, &vn, KEY_NAME);
    idx_remove(reg, reg->by_coord, &vc, KEY_COORD);

    if (idx != last) {
        /* The last entry is about to move down into the hole, so its existing
         * mappings — which name index `last` — must be withdrawn and reissued
         * against `idx`. Leaving them would strand the moved name at an index
         * that is about to fall outside count. */
        key_t ln = key_of(reg, last, KEY_NAME);
        key_t lc = key_of(reg, last, KEY_COORD);
        idx_remove(reg, reg->by_name, &ln, KEY_NAME);
        idx_remove(reg, reg->by_coord, &lc, KEY_COORD);

        reg->entries[idx] = reg->entries[last];
        reg->count--;

        key_t mn = key_of(reg, idx, KEY_NAME);
        key_t mc = key_of(reg, idx, KEY_COORD);
        idx_insert(reg, reg->by_name, &mn, idx);
        idx_insert(reg, reg->by_coord, &mc, idx);
    } else {
        reg->count--;
    }

    memset(&reg->entries[reg->count], 0, sizeof(reg->entries[reg->count]));
    return true;
}
