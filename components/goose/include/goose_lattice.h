/**
 * @file goose_lattice.h
 * @brief Coordinate index for the fabric — open addressing with linear probing.
 *
 * Maps a 9-trit coordinate to its slot in `fabric_cells[]`. The index owns no
 * memory: the caller supplies the bucket array and the cell array, so this unit
 * is pure and can be exercised on host.
 *
 * It replaces a direct-mapped table that stored one cell index per bucket and
 * simply overwrote on collision. With 256 cells hashed into 503 buckets that
 * left roughly a fifth of cells unreachable through the index, findable only by
 * the linear fallback scan that followed every probe — 256 coordinate compares
 * at nine trits each, about 95us. Allocation paid it unconditionally, because a
 * coordinate that does not exist yet can never hit a direct-mapped slot, and
 * that scan was the dominant term in the loom's ~600us peak hold.
 *
 * Deletion uses backward-shift rather than tombstones. Eviction churn is
 * continuous once the Loom saturates — a 15-minute soak recorded ~8000
 * evictions — so tombstones would fill the table and force periodic rehashing.
 * Backward shift keeps the invariant that a probe run terminates at the first
 * empty bucket, which is what makes a *miss* cheap, and a miss is the case
 * allocation cares about.
 */
#ifndef GOOSE_LATTICE_H
#define GOOSE_LATTICE_H

#include "goose.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/** Bucket holds no entry. A probe run stops here. */
#define GOOSE_LATTICE_EMPTY ((int16_t)-1)

/**
 * @brief Bucket for a coordinate. Exposed for tests and for the rebuild path.
 */
uint32_t goose_lattice_hash(reflex_tryte9_t coord, size_t bucket_count);

/** @brief Mark every bucket empty. */
void goose_lattice_clear(int16_t *slots, size_t bucket_count);

/**
 * @brief Find the cell index for @p coord.
 * @return cell index, or GOOSE_LATTICE_EMPTY if absent.
 *
 * Terminates at the first empty bucket, so a miss costs the length of one
 * probe run rather than a scan of every cell.
 */
int16_t goose_lattice_find(const int16_t *slots, size_t bucket_count,
                           const goose_cell_t *cells, size_t cell_count,
                           reflex_tryte9_t coord);

/**
 * @brief Map @p coord to @p cell_idx, replacing any existing mapping for it.
 * @return false if the table is full (which cannot happen while
 *         bucket_count > cell capacity, but is reported rather than assumed).
 */
bool goose_lattice_insert(int16_t *slots, size_t bucket_count,
                          const goose_cell_t *cells, size_t cell_count,
                          reflex_tryte9_t coord, int16_t cell_idx);

/**
 * @brief Remove @p coord's mapping, closing the probe run behind it.
 *
 * Must be called while the cell still holds @p coord — eviction has to unmap
 * the victim before overwriting its coordinate, or the mapping is stranded.
 */
void goose_lattice_remove(int16_t *slots, size_t bucket_count,
                          const goose_cell_t *cells, size_t cell_count,
                          reflex_tryte9_t coord);

#endif /* GOOSE_LATTICE_H */
