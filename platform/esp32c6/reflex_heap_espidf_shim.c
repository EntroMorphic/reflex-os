/* Reflex's allocator, spliced in underneath ESP-IDF's on the 802.15.4 path.
 *
 * `--wrap` on the allocation entry points, so every allocation in the image —
 * Reflex's, FreeRTOS's task stacks, newlib's, the PHY blob's — is served from
 * Reflex's region. That is what makes "how much heap is left" a question
 * Reflex can answer about its own bookkeeping instead of asking
 * heap_caps_get_free_size, which is the whole point of the exercise.
 *
 * The safety property that makes this tractable: **every free range-checks.**
 *
 * Interception can never be complete. Some memory is allocated before this
 * shim's region exists, and ESP-IDF has internal entry points
 * (heap_caps_malloc_base and friends) that callers inside the component reach
 * without going through a wrapped symbol. A free() that assumed every pointer
 * carried a Reflex block header would read sixteen bytes that are not there
 * and corrupt whatever is. So every free and every realloc asks
 * reflex_heap_owns() first and hands anything foreign back to __real_. Mixed
 * ownership is then correct rather than merely lucky, and a missed entry point
 * costs a little memory instead of the system.
 *
 * Capabilities are deliberately coarse. The C6 has one unified SRAM and no
 * PSRAM, so DEFAULT, INTERNAL, DMA, 8BIT and 32BIT are all the same memory and
 * all served from the region. RTCRAM is not: ESP-IDF reports that region as
 * 0 KiB on this target, and a request for it is passed through rather than
 * guessed at.
 */

#ifdef REFLEX_OWN_HEAP

#include "reflex_heap.h"
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <sys/reent.h>

/* Sized to leave ESP-IDF's own heap enough to service anything this shim does
 * not intercept. It is .bss, so the linker reserves it and there is no
 * question of overlapping a region ESP-IDF also believes it owns — which is
 * the failure mode of computing the extent from _heap_start by hand. */
#ifndef REFLEX_HEAP_REGION_BYTES
#define REFLEX_HEAP_REGION_BYTES (192 * 1024)
#endif

static uint8_t s_region[REFLEX_HEAP_REGION_BYTES] __attribute__((aligned(8)));

/* Allocation starts long before app_main, so the region is brought up on first
 * use rather than from an init call nobody would reach in time. Single core,
 * and the first allocation happens in startup before any scheduler exists. */
static inline void ensure_ready(void) {
    if (!reflex_heap_ready()) reflex_heap_init(s_region, sizeof(s_region));
}

/* ---- ESP-IDF's real entry points ---- */
extern void *__real_malloc(size_t n);
extern void __real_free(void *p);
extern void *__real_calloc(size_t c, size_t n);
extern void *__real_realloc(void *p, size_t n);
extern void *__real_heap_caps_malloc(size_t n, uint32_t caps);
extern void *__real_heap_caps_calloc(size_t c, size_t n, uint32_t caps);
extern void *__real_heap_caps_realloc(void *p, size_t n, uint32_t caps);
extern void *__real_heap_caps_aligned_alloc(size_t a, size_t n, uint32_t caps);
extern void __real_heap_caps_free(void *p);
extern size_t __real_heap_caps_get_free_size(uint32_t caps);
extern size_t __real_heap_caps_get_minimum_free_size(uint32_t caps);
extern size_t __real_heap_caps_get_largest_free_block(uint32_t caps);

/* MALLOC_CAP_RTCRAM, the one capability this region cannot satisfy. Named here
 * rather than included, because esp_heap_caps.h is the dependency being
 * removed and taking it back for one constant would be absurd. */
#define REFLEX_MALLOC_CAP_RTCRAM (1 << 4)
#define REFLEX_MALLOC_CAP_EXEC   (1 << 0)

static inline bool ours_to_serve(uint32_t caps) {
    return (caps & (REFLEX_MALLOC_CAP_RTCRAM | REFLEX_MALLOC_CAP_EXEC)) == 0;
}

/* ---- the malloc family ---- */

void *__wrap_malloc(size_t n);
void *__wrap_malloc(size_t n) {
    ensure_ready();
    void *p = reflex_heap_alloc(n);
    return p ? p : __real_malloc(n);
}

void __wrap_free(void *p);
void __wrap_free(void *p) {
    if (!p) return;
    if (reflex_heap_owns(p)) { reflex_heap_free(p); return; }
    __real_free(p);
}

void *__wrap_calloc(size_t c, size_t n);
void *__wrap_calloc(size_t c, size_t n) {
    ensure_ready();
    if (c && n > (size_t)-1 / c) return NULL;
    size_t total = c * n;
    void *p = reflex_heap_alloc(total);
    if (!p) return __real_calloc(c, n);
    memset(p, 0, total);
    return p;
}

void *__wrap_realloc(void *p, size_t n);
void *__wrap_realloc(void *p, size_t n) {
    ensure_ready();
    if (p && !reflex_heap_owns(p)) return __real_realloc(p, n);
    void *q = reflex_heap_realloc(p, n);
    if (q || n == 0) return q;
    /* Reflex's region is full. Move the allocation to ESP-IDF's heap rather
     * than failing, and copy what was there. */
    void *fallback = __real_malloc(n);
    if (fallback && p) {
        size_t old = reflex_heap_block_size(p);
        memcpy(fallback, p, old < n ? old : n);
        reflex_heap_free(p);
    }
    return fallback;
}

/* newlib reaches the allocator through the reentrant forms. */
void *__wrap__malloc_r(struct _reent *r, size_t n);
void *__wrap__malloc_r(struct _reent *r, size_t n) { (void)r; return __wrap_malloc(n); }

void __wrap__free_r(struct _reent *r, void *p);
void __wrap__free_r(struct _reent *r, void *p) { (void)r; __wrap_free(p); }

void *__wrap__calloc_r(struct _reent *r, size_t c, size_t n);
void *__wrap__calloc_r(struct _reent *r, size_t c, size_t n) { (void)r; return __wrap_calloc(c, n); }

void *__wrap__realloc_r(struct _reent *r, void *p, size_t n);
void *__wrap__realloc_r(struct _reent *r, void *p, size_t n) { (void)r; return __wrap_realloc(p, n); }

/* ---- the capability family ---- */

void *__wrap_heap_caps_malloc(size_t n, uint32_t caps);
void *__wrap_heap_caps_malloc(size_t n, uint32_t caps) {
    if (!ours_to_serve(caps)) return __real_heap_caps_malloc(n, caps);
    ensure_ready();
    void *p = reflex_heap_alloc(n);
    return p ? p : __real_heap_caps_malloc(n, caps);
}

void *__wrap_heap_caps_calloc(size_t c, size_t n, uint32_t caps);
void *__wrap_heap_caps_calloc(size_t c, size_t n, uint32_t caps) {
    if (!ours_to_serve(caps)) return __real_heap_caps_calloc(c, n, caps);
    ensure_ready();
    if (c && n > (size_t)-1 / c) return NULL;
    size_t total = c * n;
    void *p = reflex_heap_alloc(total);
    if (!p) return __real_heap_caps_calloc(c, n, caps);
    memset(p, 0, total);
    return p;
}

void *__wrap_heap_caps_aligned_alloc(size_t a, size_t n, uint32_t caps);
void *__wrap_heap_caps_aligned_alloc(size_t a, size_t n, uint32_t caps) {
    if (!ours_to_serve(caps)) return __real_heap_caps_aligned_alloc(a, n, caps);
    ensure_ready();
    void *p = reflex_heap_alloc_aligned(a, n);
    return p ? p : __real_heap_caps_aligned_alloc(a, n, caps);
}

void *__wrap_heap_caps_realloc(void *p, size_t n, uint32_t caps);
void *__wrap_heap_caps_realloc(void *p, size_t n, uint32_t caps) {
    if (p && !reflex_heap_owns(p)) return __real_heap_caps_realloc(p, n, caps);
    if (!ours_to_serve(caps)) return __real_heap_caps_realloc(p, n, caps);
    ensure_ready();
    return reflex_heap_realloc(p, n);
}

void __wrap_heap_caps_free(void *p);
void __wrap_heap_caps_free(void *p) {
    if (!p) return;
    if (reflex_heap_owns(p)) { reflex_heap_free(p); return; }
    __real_heap_caps_free(p);
}

/* Reporting. These are the three calls the whole exercise exists to be able to
 * answer from Reflex's own bookkeeping, and they report the region Reflex
 * actually serves allocations from. Anything ESP-IDF still holds is added,
 * because a caller asking "how much is left" means the machine and not the
 * bookkeeping. */
size_t __wrap_heap_caps_get_free_size(uint32_t caps);
size_t __wrap_heap_caps_get_free_size(uint32_t caps) {
    if (!ours_to_serve(caps)) return __real_heap_caps_get_free_size(caps);
    ensure_ready();
    return reflex_heap_free_bytes() + __real_heap_caps_get_free_size(caps);
}

size_t __wrap_heap_caps_get_minimum_free_size(uint32_t caps);
size_t __wrap_heap_caps_get_minimum_free_size(uint32_t caps) {
    if (!ours_to_serve(caps)) return __real_heap_caps_get_minimum_free_size(caps);
    ensure_ready();
    return reflex_heap_min_free_bytes() + __real_heap_caps_get_minimum_free_size(caps);
}

size_t __wrap_heap_caps_get_largest_free_block(uint32_t caps);
size_t __wrap_heap_caps_get_largest_free_block(uint32_t caps) {
    if (!ours_to_serve(caps)) return __real_heap_caps_get_largest_free_block(caps);
    ensure_ready();
    reflex_heap_stats_t st;
    reflex_heap_get_stats(&st);
    size_t theirs = __real_heap_caps_get_largest_free_block(caps);
    return st.largest_free > theirs ? st.largest_free : theirs;
}

#endif /* REFLEX_OWN_HEAP */
