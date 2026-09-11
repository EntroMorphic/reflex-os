/**
 * @file test_heap.c
 * @brief Tests for Reflex's own allocator.
 *
 * An allocator fails on sequences, not on single calls. The interesting bugs
 * are a split that loses the remainder, a coalesce that runs in one direction
 * only, an accounting error that drifts a few bytes per cycle, and alignment
 * that holds for the first block and not the fourth. None of those show up in
 * a handful of hand-picked allocations.
 *
 * So the centre of this file is randomised: thousands of allocate/free/realloc
 * operations against a reference model that tracks only what the caller can
 * observe — which pointers are live, how big each one is, and what bytes are in
 * them — with the allocator's own structural audit run after every single
 * operation. reflex_heap_check() walks the whole region and asserts that blocks
 * tile it exactly, that no two free blocks are adjacent, and that the free
 * counter equals the free space actually present. A drift of one header shows
 * up immediately rather than as an out-of-memory ten thousand operations later.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "reflex_heap.h"

static int s_pass = 0, s_fail = 0;

#define CHECK(name, expr)                                                                          \
    do {                                                                                           \
        if (expr) {                                                                                \
            s_pass++;                                                                              \
        } else {                                                                                   \
            printf("  FAIL: %s\n", name);                                                          \
            s_fail++;                                                                              \
        }                                                                                          \
    } while (0)

#define REGION 65536
static uint8_t s_region[REGION] __attribute__((aligned(8)));

static void fresh(void) { reflex_heap_init(s_region, sizeof(s_region)); }

/* ---- basics ---- */

static void test_init(void) {
    fresh();
    CHECK("ready after init", reflex_heap_ready());
    CHECK("intact after init", reflex_heap_check());
    reflex_heap_stats_t st;
    reflex_heap_get_stats(&st);
    CHECK("one block after init", st.blocks_total == 1);
    CHECK("that block is free", st.blocks_free == 1);
    CHECK("free equals total", st.free_bytes == st.total_bytes);
    CHECK("total is most of the region", st.total_bytes > REGION - 64);

    /* A region too small to hold one block must refuse rather than pretend. */
    reflex_heap_init(s_region, 4);
    CHECK("tiny region refused", !reflex_heap_ready());
    CHECK("tiny region allocates nothing", reflex_heap_alloc(8) == NULL);
    reflex_heap_init(NULL, REGION);
    CHECK("NULL region refused", !reflex_heap_ready());
    fresh();
}

static void test_alloc_free_roundtrip(void) {
    fresh();
    size_t before = reflex_heap_free_bytes();
    void *p = reflex_heap_alloc(100);
    CHECK("alloc returns memory", p != NULL);
    CHECK("alloc is aligned", ((uintptr_t)p & 7u) == 0);
    CHECK("free went down", reflex_heap_free_bytes() < before);
    CHECK("intact while held", reflex_heap_check());
    reflex_heap_free(p);
    CHECK("intact after free", reflex_heap_check());
    CHECK("free restored exactly", reflex_heap_free_bytes() == before);

    CHECK("zero size allocates nothing", reflex_heap_alloc(0) == NULL);
    reflex_heap_free(NULL); /* must not crash */
    CHECK("free(NULL) survives", reflex_heap_check());
}

static void test_exhaustion(void) {
    fresh();
    void *held[512];
    int n = 0;
    while (n < 512) {
        void *p = reflex_heap_alloc(256);
        if (!p) break;
        held[n++] = p;
    }
    CHECK("exhaustion reached", n > 0 && n < 512);
    CHECK("intact when full", reflex_heap_check());
    CHECK("full heap returns NULL", reflex_heap_alloc(256) == NULL);
    /* An enormous request must fail rather than wrap around. */
    CHECK("huge request refused", reflex_heap_alloc((size_t)-1) == NULL);
    for (int i = 0; i < n; i++) reflex_heap_free(held[i]);
    CHECK("intact after mass free", reflex_heap_check());
    reflex_heap_stats_t st;
    reflex_heap_get_stats(&st);
    CHECK("all coalesced back to one block", st.blocks_total == 1);
    CHECK("free restored to total", st.free_bytes == st.total_bytes);
}

static void test_coalescing_both_directions(void) {
    fresh();
    /* Three neighbours: freeing the middle one last must merge all three,
     * which only happens if coalescing runs backward as well as forward. */
    void *a = reflex_heap_alloc(128);
    void *b = reflex_heap_alloc(128);
    void *c = reflex_heap_alloc(128);
    void *guard = reflex_heap_alloc(128);
    CHECK("three neighbours allocated", a && b && c && guard);
    reflex_heap_free(a);
    reflex_heap_free(c);
    CHECK("intact with a hole either side", reflex_heap_check());
    reflex_heap_free(b);
    CHECK("intact after middle freed", reflex_heap_check());
    reflex_heap_stats_t st;
    reflex_heap_get_stats(&st);
    /* a+b+c are now one free block; guard and the tail remain. */
    CHECK("middle merged with both neighbours", st.blocks_free == 2);
    reflex_heap_free(guard);
    reflex_heap_get_stats(&st);
    CHECK("everything merged", st.blocks_total == 1);
}

static void test_alignment(void) {
    fresh();
    size_t aligns[] = { 8, 16, 32, 64, 128, 256 };
    void *held[6];
    for (int i = 0; i < 6; i++) {
        held[i] = reflex_heap_alloc_aligned(aligns[i], 100);
        CHECK("aligned alloc succeeded", held[i] != NULL);
        if (held[i]) {
            CHECK("alignment honoured", ((uintptr_t)held[i] & (aligns[i] - 1u)) == 0);
        }
        CHECK("intact after aligned alloc", reflex_heap_check());
    }
    for (int i = 0; i < 6; i++) reflex_heap_free(held[i]);
    CHECK("intact after aligned frees", reflex_heap_check());
    CHECK("non-power-of-two refused", reflex_heap_alloc_aligned(24, 16) == NULL);
    CHECK("zero alignment refused", reflex_heap_alloc_aligned(0, 16) == NULL);
}

static void test_realloc(void) {
    fresh();
    char *p = reflex_heap_alloc(64);
    CHECK("realloc seed allocated", p != NULL);
    memset(p, 0xAB, 64);

    char *grown = reflex_heap_realloc(p, 256);
    CHECK("grow succeeded", grown != NULL);
    CHECK("grow preserved contents", grown && grown[0] == (char)0xAB && grown[63] == (char)0xAB);
    CHECK("intact after grow", reflex_heap_check());

    char *shrunk = reflex_heap_realloc(grown, 32);
    CHECK("shrink succeeded", shrunk != NULL);
    CHECK("shrink preserved contents", shrunk && shrunk[0] == (char)0xAB);
    CHECK("intact after shrink", reflex_heap_check());

    CHECK("realloc(NULL) allocates", reflex_heap_realloc(NULL, 32) != NULL);
    CHECK("realloc to zero frees", reflex_heap_realloc(shrunk, 0) == NULL);
    CHECK("intact after realloc-to-zero", reflex_heap_check());

    /* Growing into a free neighbour must not move the pointer, because code
     * that assumes it can is exactly the code this would break. */
    fresh();
    void *x = reflex_heap_alloc(64);
    void *y = reflex_heap_alloc(64);
    reflex_heap_free(y);
    void *xg = reflex_heap_realloc(x, 120);
    CHECK("grow into free neighbour stays put", xg == x);
    CHECK("intact after in-place grow", reflex_heap_check());
}

static void test_min_free_tracking(void) {
    fresh();
    size_t total = reflex_heap_free_bytes();
    void *p = reflex_heap_alloc(4096);
    size_t low = reflex_heap_free_bytes();
    reflex_heap_free(p);
    CHECK("free recovered", reflex_heap_free_bytes() == total);
    CHECK("min free remembers the dip", reflex_heap_min_free_bytes() <= low);
    CHECK("min free is not the current value", reflex_heap_min_free_bytes() < total);
}

static void test_double_free_refused(void) {
    fresh();
    void *p = reflex_heap_alloc(64);
    size_t after_alloc = reflex_heap_free_bytes();
    reflex_heap_free(p);
    size_t after_free = reflex_heap_free_bytes();
    reflex_heap_free(p); /* second time: must be refused, not double-counted */
    CHECK("double free does not inflate free bytes", reflex_heap_free_bytes() == after_free);
    CHECK("intact after double free", reflex_heap_check());
    CHECK("accounting sane", after_free > after_alloc);
}

/* ---- the differential test ---- */

#define LIVE_MAX 128
typedef struct {
    void *ptr;
    size_t size;
    uint8_t tag;
} live_t;

static uint32_t s_rng = 12345;
static uint32_t rnd(void) {
    s_rng = s_rng * 1103515245u + 12345u;
    return (s_rng >> 8);
}

static void test_differential(void) {
    fresh();
    live_t live[LIVE_MAX];
    int n = 0;
    int bad_content = 0, bad_check = 0;

    for (int step = 0; step < 20000; step++) {
        uint32_t r = rnd() % 100;

        if (r < 45 && n < LIVE_MAX) {
            size_t sz = 1 + (rnd() % 512);
            void *p = reflex_heap_alloc(sz);
            if (p) {
                uint8_t tag = (uint8_t)(rnd() & 0xFF);
                memset(p, tag, sz);
                live[n].ptr = p;
                live[n].size = sz;
                live[n].tag = tag;
                n++;
            }
        } else if (r < 75 && n > 0) {
            int i = (int)(rnd() % (uint32_t)n);
            /* Verify before releasing: a block whose bytes changed while it
             * was held means some other allocation overlapped it. */
            uint8_t *q = live[i].ptr;
            for (size_t k = 0; k < live[i].size; k++) {
                if (q[k] != live[i].tag) { bad_content++; break; }
            }
            reflex_heap_free(live[i].ptr);
            live[i] = live[n - 1];
            n--;
        } else if (n > 0) {
            int i = (int)(rnd() % (uint32_t)n);
            size_t nsz = 1 + (rnd() % 512);
            uint8_t *q = reflex_heap_realloc(live[i].ptr, nsz);
            if (q) {
                size_t kept = live[i].size < nsz ? live[i].size : nsz;
                for (size_t k = 0; k < kept; k++) {
                    if (q[k] != live[i].tag) { bad_content++; break; }
                }
                memset(q, live[i].tag, nsz);
                live[i].ptr = q;
                live[i].size = nsz;
            }
        }

        if (!reflex_heap_check()) { bad_check++; break; }
    }

    CHECK("differential: no block was overwritten while held", bad_content == 0);
    CHECK("differential: structure intact at every step", bad_check == 0);

    for (int i = 0; i < n; i++) reflex_heap_free(live[i].ptr);
    CHECK("differential: intact after draining", reflex_heap_check());
    reflex_heap_stats_t st;
    reflex_heap_get_stats(&st);
    CHECK("differential: everything coalesced back", st.blocks_total == 1);
    CHECK("differential: no bytes lost", st.free_bytes == st.total_bytes);
}

/* Allocation sizes that sit exactly on the split threshold are where
 * off-by-one errors live. */
static void test_split_boundaries(void) {
    fresh();
    size_t total = reflex_heap_free_bytes();
    for (size_t sz = 1; sz <= 64; sz++) {
        void *p = reflex_heap_alloc(sz);
        CHECK("boundary alloc succeeded", p != NULL);
        if (!reflex_heap_check()) { CHECK("boundary intact", false); break; }
        reflex_heap_free(p);
        if (reflex_heap_free_bytes() != total) {
            CHECK("boundary free restored exactly", false);
            break;
        }
    }
    CHECK("all split boundaries clean", reflex_heap_free_bytes() == total);
}

static void test_ownership(void) {
    fresh();
    void *p = reflex_heap_alloc(64);
    CHECK("owns its own pointer", reflex_heap_owns(p));
    CHECK("disowns NULL", !reflex_heap_owns(NULL));
    int stack_thing = 0;
    CHECK("disowns a stack address", !reflex_heap_owns(&stack_thing));
    static int static_thing;
    CHECK("disowns a static address", !reflex_heap_owns(&static_thing));
    CHECK("disowns just below the region", !reflex_heap_owns(s_region - 1));
    CHECK("disowns just past the region", !reflex_heap_owns(s_region + REGION));
    CHECK("owns the first byte", reflex_heap_owns(s_region));
    reflex_heap_free(p);
    CHECK("still owns the range after free", reflex_heap_owns(p));
}

/* The public entry points read a block header from below the pointer they are
 * given. Handing them something that is not theirs must be refused, not
 * dereferenced. */
static void test_foreign_pointers_refused(void) {
    fresh();
    size_t before = reflex_heap_free_bytes();
    static uint8_t elsewhere[64];
    int on_stack = 0;

    reflex_heap_free(elsewhere);
    reflex_heap_free(&on_stack);
    reflex_heap_free((void *)(s_region + REGION + 8));
    CHECK("foreign frees changed nothing", reflex_heap_free_bytes() == before);
    CHECK("intact after foreign frees", reflex_heap_check());

    CHECK("foreign block_size is zero", reflex_heap_block_size(elsewhere) == 0);
    CHECK("foreign realloc refused", reflex_heap_realloc(elsewhere, 32) == NULL);
    CHECK("intact after foreign realloc", reflex_heap_check());

    /* Inside the region but misaligned, and inside the region but pointing at
     * the first block's header rather than its payload. */
    reflex_heap_free(s_region + 3);
    reflex_heap_free(s_region);
    CHECK("misaligned and header-area frees refused", reflex_heap_free_bytes() == before);
    CHECK("intact after those", reflex_heap_check());
}

/* The differential test leans on reflex_heap_check() to notice damage. That is
 * only worth leaning on if it actually fails when the structure is damaged, so
 * damage it on purpose and watch it fail. */
static void test_audit_detects_corruption(void) {
    fresh();
    uint8_t *a = reflex_heap_alloc(64);
    void *b = reflex_heap_alloc(64);
    CHECK("corruption setup allocated", a != NULL && b != NULL);
    CHECK("intact before corruption", reflex_heap_check());

    /* Overrun a's payload into what physically follows it, which is b's
     * header. A real overflow bug looks exactly like this. */
    memset(a + 64, 0x5A, 32);
    CHECK("audit detects a trampled neighbour header", !reflex_heap_check());

    fresh(); /* the region is knowingly wrecked; start clean for later tests */
    CHECK("intact after re-init", reflex_heap_check());
}

int test_reflex_heap(void) {
    printf("[heap] ");
    test_init();
    test_alloc_free_roundtrip();
    test_exhaustion();
    test_coalescing_both_directions();
    test_alignment();
    test_realloc();
    test_min_free_tracking();
    test_double_free_refused();
    test_split_boundaries();
    test_ownership();
    test_foreign_pointers_refused();
    test_audit_detects_corruption();
    test_differential();
    if (s_fail == 0) printf("ok\n");
    return s_fail;
}

int test_reflex_heap_passed(void) { return s_pass; }
