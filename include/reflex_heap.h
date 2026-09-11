/* Reflex's own allocator.
 *
 * The reason this exists is not performance and not features. It is that
 * "how much heap is left" is a question Reflex answers in two places — the
 * metabolic circuit breaker and the shell — and neither can be answered
 * honestly by code that does not own the allocator. Consolidating those two
 * calls behind a Reflex-named function would move the dependency rather than
 * end it, which is the move this tree has refused three times for Tier C and
 * refused again for the heap. So: own the allocator, and the reporting
 * follows.
 *
 * Single-threaded with respect to itself: every public entry point takes the
 * heap's lock, which on a single-core part is interrupts-off.
 */
#ifndef REFLEX_HEAP_H
#define REFLEX_HEAP_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

/** Statistics, sampled atomically with respect to allocation. */
typedef struct {
    size_t total_bytes;    /**< payload capacity of the region            */
    size_t free_bytes;     /**< sum of free payloads right now            */
    size_t min_free_bytes; /**< low-water mark of free_bytes since init   */
    size_t largest_free;   /**< biggest single allocation that would fit  */
    uint32_t blocks_total; /**< blocks in the region, used and free       */
    uint32_t blocks_free;  /**< of which free                             */
} reflex_heap_stats_t;

/** Hand the allocator a region. Call once, before any allocation.
 *  @p base need not be aligned; the allocator aligns internally. */
void reflex_heap_init(void *base, size_t size);

/** True once a region has been given. */
bool reflex_heap_ready(void);

/** Allocate @p size bytes, or NULL. Zero size returns NULL. */
void *reflex_heap_alloc(size_t size);

/** Allocate @p size bytes aligned to @p align, which must be a power of two. */
void *reflex_heap_alloc_aligned(size_t align, size_t size);

/** Free a pointer from this allocator. NULL is a no-op. */
void reflex_heap_free(void *ptr);

/** Resize. Semantics of realloc(), including NULL and zero cases. */
void *reflex_heap_realloc(void *ptr, size_t size);

/** Payload size of an allocation, for realloc-style copying. 0 if not ours. */
size_t reflex_heap_block_size(const void *ptr);

/** True if @p ptr lies inside this allocator's region.
 *
 * The reason this is public: when Reflex's allocator is spliced in underneath
 * code that was already running, some memory predates it or comes from a path
 * that was not intercepted. A free() that assumes every pointer is its own
 * will read a header that is not there. Range-checking first, and handing
 * anything foreign back to the allocator it came from, is what makes partial
 * interception safe rather than merely likely to work. */
bool reflex_heap_owns(const void *ptr);

/** Free bytes right now. */
size_t reflex_heap_free_bytes(void);

/** Low-water mark of free bytes since init. */
size_t reflex_heap_min_free_bytes(void);

/** Fill @p out with a consistent snapshot. */
void reflex_heap_get_stats(reflex_heap_stats_t *out);

/** Walk every block and verify the structure. Returns true if intact.
 *  Exists so corruption is caught where it happens rather than three
 *  allocations later. */
bool reflex_heap_check(void);

#endif /* REFLEX_HEAP_H */
