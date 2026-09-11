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

#ifdef REFLEX_HEAP_SHIM

#include "reflex_heap.h"
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <sys/reent.h>

/* Sized from measurement, not from caution.
 *
 * It is .bss, so the linker reserves it and there is no question of
 * overlapping a region ESP-IDF also believes it owns — which is the failure
 * mode of computing the extent from _heap_start by hand. Whatever this takes,
 * ESP-IDF's heap gives up, so total capacity does not change; the only
 * question is where the boundary sits.
 *
 * 192 KiB leaves ESP-IDF around 170 KiB, of which measurement says roughly
 * 4 KiB is ever used — the unintercepted paths take very little. That looks
 * like 166 KiB sitting idle, and the obvious improvement is to claim more of
 * it so the figure the shell reports is closer to the machine's true free
 * memory.
 *
 * 288 KiB was tried and the board panics: `Stack protection fault` during
 * startup, with ESP-IDF's residual heap confirmed down to 73 KiB, so the image
 * was right and the size was wrong. The memory is not free for the taking —
 * something in startup needs room this array would occupy. 192 KiB is the size
 * that has been run against the full hardware suite, so 192 KiB is the size
 * that ships. The idle residual is a real cost, recorded rather than guessed
 * away, and the boundary between the two is somewhere that has not been
 * measured. */
#ifndef REFLEX_HEAP_REGION_BYTES
#define REFLEX_HEAP_REGION_BYTES (192 * 1024)
#endif

static uint8_t s_region[REFLEX_HEAP_REGION_BYTES] __attribute__((aligned(8)));

/* Allocation starts long before app_main, so the region is brought up on first
 * use rather than from an init call nobody would reach in time.
 *
 * Double-checked, with the second check under interrupts. The bare
 * check-then-act this started as is a race however unlikely the interleaving:
 * reflex_heap_init resets the region wholesale, so a second init racing the
 * first would discard every allocation already handed out and leave their
 * owners pointing into free space. The fast path still costs one load. */
static void ensure_ready(void) {
    if (reflex_heap_ready()) return;
    uint32_t saved;
    __asm__ volatile("csrrci %0, mstatus, 0x8" : "=r"(saved));
    if (!reflex_heap_ready()) reflex_heap_init(s_region, sizeof(s_region));
    if (saved & 0x8u) __asm__ volatile("csrsi mstatus, 0x8");
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

/* What this region can actually satisfy, stated as a positive mask.
 *
 * The first version of this listed the two capabilities it thought the region
 * could not serve and passed those through. That is the wrong shape and it was
 * also wrong on the facts: MALLOC_CAP_RTCRAM is bit 15, not bit 4, and bit 4 is
 * MALLOC_CAP_PID2. A request for RTC fast memory would have been served out of
 * DRAM. Enumerating what is *not* supported means every capability nobody
 * thought of is silently claimed; enumerating what *is* supported means every
 * capability nobody thought of is passed through, which is the safe direction.
 *
 * The values match esp_heap_caps.h, and the set matches what ESP-IDF's own C6
 * memory layout declares for SOC_MEMORY_TYPE_RAM — the region this .bss array
 * sits inside. EXEC is deliberately excluded even though that layout can carry
 * it: it is conditional there, and executing out of the application's .bss is
 * not something to infer. Constants are named here rather than included
 * because esp_heap_caps.h is the dependency being removed. */
#define REFLEX_CAP_EXEC     (1u << 0)
#define REFLEX_CAP_32BIT    (1u << 1)
#define REFLEX_CAP_8BIT     (1u << 2)
#define REFLEX_CAP_DMA      (1u << 3)
#define REFLEX_CAP_INTERNAL (1u << 11)
#define REFLEX_CAP_DEFAULT  (1u << 12)

#define REFLEX_CAPS_SERVEABLE                                                  \
    (REFLEX_CAP_DEFAULT | REFLEX_CAP_INTERNAL | REFLEX_CAP_8BIT |              \
     REFLEX_CAP_32BIT | REFLEX_CAP_DMA)

static inline bool ours_to_serve(uint32_t caps) {
    return (caps & ~(uint32_t)REFLEX_CAPS_SERVEABLE) == 0;
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

    /* From here p is ours or NULL, and handing it to __real_heap_caps_realloc
     * would be handing ESP-IDF a pointer into a region it does not manage.
     * The first version of this did exactly that whenever the caller asked for
     * a capability this region cannot serve — a pointer ours, a realloc
     * theirs. It has to be a move: allocate there, copy, release here. */
    if (ours_to_serve(caps)) {
        ensure_ready();
        void *q = reflex_heap_realloc(p, n);
        if (q || n == 0) return q;
        /* fall through: our region is full, move it to theirs */
    }

    if (n == 0) {
        if (p) reflex_heap_free(p);
        return NULL;
    }
    void *fresh = __real_heap_caps_malloc(n, caps);
    if (fresh && p) {
        size_t old = reflex_heap_block_size(p);
        memcpy(fresh, p, old < n ? old : n);
        reflex_heap_free(p);
    }
    return fresh;
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

/* Two low-water marks added together, which is not the low-water mark of the
 * sum: the two pools reach their minima at different moments, so
 * min(a) + min(b) <= min(a + b). The answer is therefore an underestimate and
 * never an overestimate, which is the safe direction for the only thing this
 * figure is used for — noticing that memory is getting tight. Said plainly
 * here because "add the two" looks like it ought to be exact and is not. */
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

#endif /* REFLEX_HEAP_SHIM */
