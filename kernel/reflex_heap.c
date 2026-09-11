/* Reflex's own allocator. See include/reflex_heap.h for why it exists.
 *
 * Structure: one physically-ordered doubly-linked list of every block in the
 * region, used and free alike, with first-fit search and immediate coalescing
 * on free. That is not the fastest arrangement available — a segregated free
 * list would be — and it is chosen deliberately, because every block is
 * reachable by walking `next` from the first, which makes reflex_heap_check()
 * a complete structural audit rather than a spot check. An allocator that
 * cannot be audited is not one to put underneath a radio blob.
 *
 * Sixteen bytes of header per block on a 32-bit target. That is real overhead
 * and it buys backward coalescing without a footer and the audit above.
 */

#include "reflex_heap.h"
#include <string.h>

#define ALIGN       8u
#define ALIGN_UP(n) (((n) + (ALIGN - 1u)) & ~(size_t)(ALIGN - 1u))

typedef struct block {
    struct block *prev; /* physically previous block, NULL for the first */
    struct block *next; /* physically next block, NULL for the last      */
    size_t size;        /* payload bytes, always a multiple of ALIGN     */
    uint32_t used;      /* 0 free, 1 in use                              */
} block_t;

#define HDR         ALIGN_UP(sizeof(block_t))
#define MIN_PAYLOAD ALIGN
/* Splitting is only worth it if the remainder can hold a header and a
 * minimum payload; otherwise the tail stays with the allocation. */
#define SPLIT_MIN   (HDR + MIN_PAYLOAD)

static block_t *s_first;
static size_t s_total;
static size_t s_free;
static size_t s_min_free;
static bool s_ready;

/* ---- the lock ----
 *
 * On the C6 this is interrupts-off, which is what a spinlock degenerates to on
 * a single-core part. On the host it is nothing: the host suite is
 * single-threaded, and pretending otherwise would only hide races it cannot
 * have. */
#if defined(REFLEX_HOST_BUILD) || !defined(__riscv)
typedef int heap_lock_t;
static inline heap_lock_t heap_lock(void) { return 0; }
static inline void heap_unlock(heap_lock_t s) { (void)s; }
#else
typedef uint32_t heap_lock_t;
static inline heap_lock_t heap_lock(void) {
    uint32_t saved;
    __asm__ volatile("csrrci %0, mstatus, 0x8" : "=r"(saved));
    return saved;
}
static inline void heap_unlock(heap_lock_t s) {
    if (s & 0x8u) __asm__ volatile("csrsi mstatus, 0x8");
}
#endif

static inline void *payload_of(block_t *b) { return (uint8_t *)b + HDR; }

static inline block_t *block_of(const void *p) {
    return (block_t *)(void *)((uint8_t *)p - HDR);
}

/* Rounding up overflows for sizes near SIZE_MAX, and a wrapped size is worse
 * than a refusal: ALIGN_UP((size_t)-1) is 0, which then clamps to MIN_PAYLOAD,
 * so a caller asking for everything would be handed eight bytes and told it
 * succeeded. Anything that cannot be rounded and headered is refused. */
static inline bool size_is_sane(size_t size) {
    return size <= (size_t)-1 - (ALIGN - 1u) - HDR;
}

static void note_free_change(void) {
    if (s_free < s_min_free) s_min_free = s_free;
}

void reflex_heap_init(void *base, size_t size) {
    s_first = NULL;
    s_total = s_free = s_min_free = 0;
    s_ready = false;
    if (!base || size < HDR + MIN_PAYLOAD) return;

    uintptr_t raw = (uintptr_t)base;
    uintptr_t start = (raw + (ALIGN - 1u)) & ~(uintptr_t)(ALIGN - 1u);
    size_t lost = (size_t)(start - raw);
    if (size <= lost + HDR + MIN_PAYLOAD) return;
    size_t usable = (size - lost) & ~(size_t)(ALIGN - 1u);

    block_t *b = (block_t *)(void *)start;
    b->prev = NULL;
    b->next = NULL;
    b->size = usable - HDR;
    b->used = 0;

    s_first = b;
    s_total = b->size;
    s_free = b->size;
    s_min_free = b->size;
    s_ready = true;
}

bool reflex_heap_ready(void) { return s_ready; }

/* Split @p b so it holds exactly @p want payload bytes, returning the tail to
 * the free pool. Caller holds the lock and has checked b->size >= want. */
static void split(block_t *b, size_t want) {
    size_t rest = b->size - want;
    if (rest < SPLIT_MIN) return;

    block_t *tail = (block_t *)(void *)((uint8_t *)payload_of(b) + want);
    tail->size = rest - HDR;
    tail->used = 0;
    tail->prev = b;
    tail->next = b->next;
    if (b->next) b->next->prev = tail;
    b->next = tail;
    b->size = want;
    /* The header carved out of free space is no longer available. */
    s_free -= HDR;
}

/* Merge @p b with its next neighbour if that neighbour is free. */
static void coalesce_forward(block_t *b) {
    block_t *n = b->next;
    if (!n || n->used) return;
    b->size += HDR + n->size;
    b->next = n->next;
    if (n->next) n->next->prev = b;
    s_free += HDR;
}

static void *alloc_locked(size_t align, size_t size) {
    if (!s_ready || size == 0 || !size_is_sane(size)) return NULL;
    size_t want = ALIGN_UP(size);
    if (want < MIN_PAYLOAD) want = MIN_PAYLOAD;
    if (align < ALIGN) align = ALIGN;

    for (block_t *b = s_first; b; b = b->next) {
        if (b->used) continue;

        uintptr_t pay = (uintptr_t)payload_of(b);
        uintptr_t aligned = (pay + (align - 1u)) & ~(uintptr_t)(align - 1u);
        size_t gap = (size_t)(aligned - pay);

        /* A gap has to become a block of its own, so it must be big enough to
         * be one. If the nearest aligned address leaves too small a gap, step
         * to the next one rather than abandoning the block — refusing here is
         * what made a 32-byte-aligned request fail outright on a wholly empty
         * heap, because the first payload happened to sit 16 bytes below the
         * boundary. */
        while (gap != 0 && gap < SPLIT_MIN) {
            aligned += align;
            gap = (size_t)(aligned - pay);
        }

        if (gap == 0) {
            if (b->size < want) continue;
        } else {
            if (b->size < gap + want) continue;
            split(b, gap - HDR);
            b = b->next; /* the tail, whose payload is `aligned` */
            /* If the split was declined the tail is not ours — it is whatever
             * physically followed, quite possibly in use. Checking `used` here
             * is what stops this handing out an allocated block. */
            if (!b || b->used || b->size < want) continue;
        }

        split(b, want);
        b->used = 1;
        s_free -= b->size;
        note_free_change();
        return payload_of(b);
    }
    return NULL;
}

void *reflex_heap_alloc(size_t size) {
    heap_lock_t s = heap_lock();
    void *p = alloc_locked(ALIGN, size);
    heap_unlock(s);
    return p;
}

void *reflex_heap_alloc_aligned(size_t align, size_t size) {
    if (align == 0 || (align & (align - 1u)) != 0) return NULL;
    heap_lock_t s = heap_lock();
    void *p = alloc_locked(align, size);
    heap_unlock(s);
    return p;
}

static void free_locked(void *ptr) {
    block_t *b = block_of(ptr);
    if (!b->used) return; /* double free: refuse rather than corrupt */
    b->used = 0;
    s_free += b->size;
    coalesce_forward(b);
    if (b->prev && !b->prev->used) coalesce_forward(b->prev);
}

void reflex_heap_free(void *ptr) {
    if (!ptr || !s_ready) return;
    heap_lock_t s = heap_lock();
    free_locked(ptr);
    heap_unlock(s);
}

size_t reflex_heap_block_size(const void *ptr) {
    if (!ptr || !s_ready) return 0;
    heap_lock_t s = heap_lock();
    size_t n = block_of(ptr)->size;
    heap_unlock(s);
    return n;
}

void *reflex_heap_realloc(void *ptr, size_t size) {
    if (!ptr) return reflex_heap_alloc(size);
    if (size == 0) { reflex_heap_free(ptr); return NULL; }
    if (!s_ready || !size_is_sane(size)) return NULL;

    heap_lock_t s = heap_lock();
    block_t *b = block_of(ptr);
    size_t want = ALIGN_UP(size);
    if (want < MIN_PAYLOAD) want = MIN_PAYLOAD;

    if (want <= b->size) {
        size_t before = b->size;
        split(b, want);
        if (b->size != before) {
            s_free += before - b->size;
            /* The tail this just carved off may sit against a block that was
             * already free. Splitting does not coalesce, so without this the
             * region ends up holding two adjacent free blocks — byte-accurate
             * but structurally wrong, and it fragments a little more on every
             * shrink. The audit caught it on the first realloc. */
            if (b->next && !b->next->used) coalesce_forward(b->next);
        }
        note_free_change();
        heap_unlock(s);
        return ptr;
    }

    /* Grow in place if the physically next block is free and big enough. */
    block_t *n = b->next;
    if (n && !n->used && b->size + HDR + n->size >= want) {
        /* Absorbing a free neighbour into a block that is in use: the
         * neighbour's payload stops being free, and its header stops existing
         * as a header. Headers were never counted in s_free, so the only
         * adjustment is the payload. Crediting HDR back here — which this did
         * at first — inflates the free total by sixteen bytes per in-place
         * grow, and the structural audit caught it on the first realloc. */
        s_free -= n->size;
        b->size += HDR + n->size;
        b->next = n->next;
        if (n->next) n->next->prev = b;
        size_t before = b->size;
        split(b, want);
        if (b->size != before) {
            s_free += before - b->size;
            if (b->next && !b->next->used) coalesce_forward(b->next);
        }
        note_free_change();
        heap_unlock(s);
        return ptr;
    }

    size_t old = b->size;
    void *fresh = alloc_locked(ALIGN, size);
    if (fresh) {
        memcpy(fresh, ptr, old < size ? old : size);
        free_locked(ptr);
    }
    heap_unlock(s);
    return fresh;
}

size_t reflex_heap_free_bytes(void) {
    heap_lock_t s = heap_lock();
    size_t n = s_free;
    heap_unlock(s);
    return n;
}

size_t reflex_heap_min_free_bytes(void) {
    heap_lock_t s = heap_lock();
    size_t n = s_min_free;
    heap_unlock(s);
    return n;
}

void reflex_heap_get_stats(reflex_heap_stats_t *out) {
    if (!out) return;
    heap_lock_t s = heap_lock();
    out->total_bytes = s_total;
    out->free_bytes = s_free;
    out->min_free_bytes = s_min_free;
    out->largest_free = 0;
    out->blocks_total = 0;
    out->blocks_free = 0;
    for (block_t *b = s_first; b; b = b->next) {
        out->blocks_total++;
        if (!b->used) {
            out->blocks_free++;
            if (b->size > out->largest_free) out->largest_free = b->size;
        }
    }
    heap_unlock(s);
}

bool reflex_heap_check(void) {
    heap_lock_t s = heap_lock();
    bool ok = true;
    size_t free_seen = 0;
    block_t *prev = NULL;

    for (block_t *b = s_first; b && ok; b = b->next) {
        if (b->prev != prev) ok = false;
        if ((b->size & (ALIGN - 1u)) != 0) ok = false;
        if (b->used > 1u) ok = false;
        if (b->next) {
            /* Blocks must tile the region exactly, with no gap and no
             * overlap: the next header begins where this payload ends. */
            uint8_t *end = (uint8_t *)payload_of(b) + b->size;
            if ((uint8_t *)b->next != end) ok = false;
            /* Two adjacent free blocks mean a coalesce was missed. */
            if (!b->used && !b->next->used) ok = false;
        }
        if (!b->used) free_seen += b->size;
        prev = b;
    }
    if (ok && free_seen != s_free) ok = false;
    heap_unlock(s);
    return ok;
}

