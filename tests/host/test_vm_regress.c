/**
 * @file test_vm_regress.c
 * @brief Regression tests for VM defects found on 2026-08-12.
 *
 * These compile the real vm/cache.c and vm/loader.c, not reimplementations.
 * Every case below corresponds to a defect that shipped and was fixed by
 * hardware observation alone — watching serial output once. That is exactly
 * the kind of evidence that rots, and three times in that session a hardware
 * "pass" turned out to be confounded by whatever state happened to be on the
 * bench. A test does not depend on the bench.
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "reflex_types.h"
#include "reflex_ternary.h"
#include "reflex_vm.h"
#include "reflex_vm_task.h"
#include "reflex_hal.h"
#include "reflex_cache.h"
#include "reflex_vm_loader.h"

static int s_pass = 0, s_fail = 0;

#define CHECK(name, expr) do { \
    if (expr) { s_pass++; } \
    else { printf("  FAIL: %s\n", name); s_fail++; } \
} while (0)

/* --- Backing store for the cache under test ------------------------------
 *
 * vm/cache.c reaches memory through reflex_vm_mem_get_raw/set_raw. This file
 * used to define those itself, along with inert reflex_vm_reset/unload, so the
 * suite could link without the interpreter. That worked, but it meant the
 * interpreter's run loop had no host coverage at all.
 *
 * The real accessors translate through the MMU, and mapping one region over
 * this same array reproduces exactly what the stubs did: index N of the region
 * is s_mem[N], so every assertion below still reads the bytes the cache wrote.
 * The stubs are gone and vm/interpreter.c is linked instead. */
#define TEST_MEM_WORDS 64
static reflex_word18_t s_mem[TEST_MEM_WORDS];

/* Map s_mem as the VM's private memory at address 0. */
static void bind_test_memory(reflex_vm_state_t *vm) {
    reflex_vm_mmu_init(&vm->mmu);
    reflex_vm_mmu_add_region(&vm->mmu, s_mem, TEST_MEM_WORDS, REFLEX_MEM_PRIVATE, 0);
}

static reflex_word18_t w(int32_t v)
{
    reflex_word18_t out;
    reflex_word18_from_int32(v, &out);
    return out;
}

static int32_t as_int(const reflex_word18_t *v)
{
    int32_t out = 0;
    reflex_word18_to_int32(v, &out);
    return out;
}

/* --- Cache -----------------------------------------------------------------
 *
 * Regression: TINV called the discarding reflex_cache_invalidate directly, so
 * a program that stored to an address and then invalidated it lost the store —
 * the line sat dirty, invalidate dropped it, and the next load re-fetched the
 * stale value from memory. docs/vm/cache.md calls TINV a "re-fetch from host
 * RAM", which only means anything if host RAM holds the program's own most
 * recent write. */
static void test_cache(void)
{
    printf("[cache]   ");
    reflex_cache_t cache;
    reflex_vm_state_t vm;

    memset(&vm, 0, sizeof(vm));
    bind_test_memory(&vm);
    memset(s_mem, 0, sizeof(s_mem));
    reflex_cache_init(&cache);
    vm.cache = (struct reflex_cache *)&cache;

    /* A committed store must survive an invalidate. This is the defect. */
    reflex_word18_t val = w(7);
    CHECK("store accepted", reflex_cache_store(&vm, 3, &val) == REFLEX_OK);
    CHECK("flush_invalidate ok", reflex_cache_flush_invalidate(&vm, 3) == REFLEX_OK);
    CHECK("dirty line was written back, not dropped", as_int(&s_mem[3]) == 7);

    reflex_word18_t back;
    CHECK("reload after invalidate", reflex_cache_load(&vm, 3, &back) == REFLEX_OK);
    CHECK("reload sees the stored value, not the stale one", as_int(&back) == 7);

    /* The discarding variant still discards — reflex_vm_host_write depends on
     * that, having already written newer data straight to memory. Using
     * flush-invalidate there would clobber the host's value with the stale
     * cached copy. */
    reflex_cache_init(&cache);
    memset(s_mem, 0, sizeof(s_mem));
    reflex_word18_t v2 = w(11);
    reflex_cache_store(&vm, 5, &v2);                 /* dirty, memory still 0 */
    CHECK("plain invalidate ok", reflex_cache_invalidate(&vm, 5) == REFLEX_OK);
    CHECK("plain invalidate discards without write-back", as_int(&s_mem[5]) == 0);

    /* host_write must land in memory and not be shadowed by a stale line. */
    reflex_cache_init(&cache);
    memset(s_mem, 0, sizeof(s_mem));
    reflex_word18_t v3 = w(4);
    reflex_cache_store(&vm, 9, &v3);                 /* cache holds 4, dirty */
    reflex_word18_t host = w(9);
    CHECK("host_write ok", reflex_vm_host_write(&vm, 9, &host) == REFLEX_OK);
    CHECK("host value reached memory", as_int(&s_mem[9]) == 9);
    reflex_cache_load(&vm, 9, &back);
    CHECK("load after host_write sees the host value", as_int(&back) == 9);

    /* Eviction on a set collision must write back the displaced dirty line. */
    reflex_cache_init(&cache);
    memset(s_mem, 0, sizeof(s_mem));
    reflex_word18_t a = w(1);
    reflex_cache_store(&vm, 0, &a);
    reflex_word18_t b = w(2);
    reflex_cache_store(&vm, REFLEX_CACHE_SET_COUNT, &b);   /* same set, different tag */
    CHECK("displaced dirty line was written back", as_int(&s_mem[0]) == 1);

    /* flush_all must not lose anything either. */
    reflex_cache_init(&cache);
    memset(s_mem, 0, sizeof(s_mem));
    reflex_word18_t c = w(5);
    reflex_cache_store(&vm, 2, &c);
    CHECK("flush_all ok", reflex_cache_flush_all(&vm) == REFLEX_OK);
    CHECK("flush_all wrote the dirty line back", as_int(&s_mem[2]) == 5);

    printf("ok\n");
}

/* --- Loader ----------------------------------------------------------------
 *
 * Regression: reflex_vm_loader_target_in_range declared int16_t while
 * reflex_vm_instruction_t::imm is int32_t, so every branch-target check
 * silently truncated. The decoder emits a 17-bit signed immediate, and
 * -65536 is the value that matters: its low 16 bits are zero, so it passed
 * validation as target 0. */
static void test_loader_branch_target(void)
{
    printf("[loader]  ");

    static const reflex_vm_instruction_t ok_prog[] = {
        {.opcode = REFLEX_VM_OPCODE_TLDI, .dst = 0, .imm = 1},
        {.opcode = REFLEX_VM_OPCODE_THALT},
    };
    reflex_vm_image_t img = {
        .magic = REFLEX_VM_IMAGE_MAGIC, .version = 1, .entry_ip = 0,
        .instructions = ok_prog, .instruction_count = 2,
        .private_memory = NULL, .private_memory_count = 0,
    };
    CHECK("valid image accepted", reflex_vm_validate_image(&img) == REFLEX_OK);

    /* The truncation case. -65536 & 0xFFFF == 0, so a 16-bit check reads it
     * as branch-to-0 and lets the image through. */
    static const reflex_vm_instruction_t trunc_prog[] = {
        {.opcode = REFLEX_VM_OPCODE_TBRZERO, .src_a = 0, .imm = -65536},
        {.opcode = REFLEX_VM_OPCODE_THALT},
    };
    reflex_vm_image_t trunc = img;
    trunc.instructions = trunc_prog;
    trunc.instruction_count = 2;
    CHECK("branch target -65536 rejected (not truncated to 0)",
          reflex_vm_validate_image(&trunc) != REFLEX_OK);

    /* Same shape for a large positive target beyond the program. */
    static const reflex_vm_instruction_t far_prog[] = {
        {.opcode = REFLEX_VM_OPCODE_TJMP, .imm = 65536},
        {.opcode = REFLEX_VM_OPCODE_THALT},
    };
    reflex_vm_image_t far = img;
    far.instructions = far_prog;
    far.instruction_count = 2;
    CHECK("branch target beyond program rejected",
          reflex_vm_validate_image(&far) != REFLEX_OK);

    /* An ordinary in-range backward branch must still be accepted, so the
     * check is not simply rejecting everything. */
    static const reflex_vm_instruction_t back_prog[] = {
        {.opcode = REFLEX_VM_OPCODE_TNOP},
        {.opcode = REFLEX_VM_OPCODE_TBRZERO, .src_a = 0, .imm = 0},
        {.opcode = REFLEX_VM_OPCODE_THALT},
    };
    reflex_vm_image_t back = img;
    back.instructions = back_prog;
    back.instruction_count = 3;
    CHECK("in-range branch still accepted", reflex_vm_validate_image(&back) == REFLEX_OK);

    /* Same defect class, two checks away: reflex_vm_loader_syscall_valid
     * declared int16_t, so the selector was validated at 16 bits and
     * dispatched at 17. 0x10003 truncates to 3 (SYSCALL_DELAY) and passes,
     * while the interpreter would dispatch 65539. */
    static const reflex_vm_instruction_t sys_trunc_prog[] = {
        {.opcode = REFLEX_VM_OPCODE_TSYS, .dst = 0, .src_a = 0, .src_b = 0, .imm = 0x10003},
        {.opcode = REFLEX_VM_OPCODE_THALT},
    };
    reflex_vm_image_t sys_trunc = img;
    sys_trunc.instructions = sys_trunc_prog;
    sys_trunc.instruction_count = 2;
    CHECK("syscall selector 0x10003 rejected (not truncated to DELAY)",
          reflex_vm_validate_image(&sys_trunc) != REFLEX_OK);

    /* The negative counterpart: -65536 has zero low 16 bits, so a 16-bit
     * check reads it as SYSCALL_LOG. */
    static const reflex_vm_instruction_t sys_neg_prog[] = {
        {.opcode = REFLEX_VM_OPCODE_TSYS, .dst = 0, .src_a = 0, .src_b = 0, .imm = -65536},
        {.opcode = REFLEX_VM_OPCODE_THALT},
    };
    reflex_vm_image_t sys_neg = img;
    sys_neg.instructions = sys_neg_prog;
    sys_neg.instruction_count = 2;
    CHECK("syscall selector -65536 rejected (not truncated to LOG)",
          reflex_vm_validate_image(&sys_neg) != REFLEX_OK);

    /* And every real selector must still load, so the check is not simply
     * rejecting everything. */
    static const reflex_vm_instruction_t sys_ok_prog[] = {
        {.opcode = REFLEX_VM_OPCODE_TSYS,
         .dst = 0,
         .src_a = 0,
         .src_b = 0,
         .imm = REFLEX_VM_SYSCALL_DELAY},
        {.opcode = REFLEX_VM_OPCODE_THALT},
    };
    reflex_vm_image_t sys_ok = img;
    sys_ok.instructions = sys_ok_prog;
    sys_ok.instruction_count = 2;
    CHECK("in-range syscall selector still accepted",
          reflex_vm_validate_image(&sys_ok) == REFLEX_OK);

    printf("ok\n");
}

/* --- word18 conversion -----------------------------------------------------
 *
 * Regression: reflex_word18_from_int32 wrote its trits straight into *out and
 * only checked for overflow afterwards, so an out-of-range value returned an
 * error *and* left the destination holding the truncated low 18 trits. The
 * interpreter's TLDI passes a live register as that destination. */
static void test_word18_no_clobber(void)
{
    printf("[word18]  ");

    reflex_word18_t out;
    CHECK("in-range converts", reflex_word18_from_int32(42, &out) == REFLEX_OK);
    CHECK("in-range value correct", as_int(&out) == 42);

    /* (3^18 - 1) / 2 == 193710244 is the largest representable magnitude. */
    CHECK("max magnitude accepted", reflex_word18_from_int32(193710244, &out) == REFLEX_OK);

    reflex_word18_t guard = w(1234);
    reflex_word18_t before = guard;
    CHECK("overflow rejected", reflex_word18_from_int32(193710245, &guard) != REFLEX_OK);
    CHECK("destination untouched on overflow",
          memcmp(&guard, &before, sizeof(guard)) == 0);

    reflex_word18_t guard2 = w(-77);
    reflex_word18_t before2 = guard2;
    CHECK("negative overflow rejected", reflex_word18_from_int32(-193710245, &guard2) != REFLEX_OK);
    CHECK("destination untouched on negative overflow",
          memcmp(&guard2, &before2, sizeof(guard2)) == 0);

    /* Round-trip across the range, including the sign boundary. */
    const int32_t samples[] = {0, 1, -1, 2, -2, 3, -3, 1000, -1000, 193710244, -193710244};
    int round_ok = 1;
    for (size_t i = 0; i < sizeof(samples) / sizeof(samples[0]); i++) {
        reflex_word18_t t;
        if (reflex_word18_from_int32(samples[i], &t) != REFLEX_OK || as_int(&t) != samples[i]) {
            round_ok = 0;
        }
    }
    CHECK("round-trip across the representable range", round_ok);

    printf("ok\n");
}

/* --- VM task service init --------------------------------------------------
 *
 * Regression: reflex_service_register calls the init hook synchronously, and
 * reflex_vm_task_service_init called reflex_vm_task_runtime_init, whose whole
 * body is memset(runtime, 0, sizeof *runtime). main.c installed the system
 * VM's soft cache and *then* registered the service, so the pointer was zeroed
 * before the VM ever ran: the system VM executed in direct-MMU mode with every
 * TLD/TST/TFLUSH/TINV cache semantic skipped and no diagnostic. A NULL cache is
 * a supported mode, so nothing was unsafe — it was a shipped feature that was
 * never once active. */
static void test_vm_task_service_init_keeps_cache(void) {
    printf("[vmtask]  ");

    static reflex_cache_t cache;
    static reflex_vm_task_runtime_t runtime;

    reflex_cache_init(&cache);
    reflex_vm_task_runtime_init(&runtime);
    runtime.vm.cache = (struct reflex_cache *)&cache;

    /* The exact order main.c uses: configure, then register. */
    extern int mock_service_init_calls;
    int before = mock_service_init_calls;
    CHECK("register_service succeeded",
          reflex_vm_task_register_service(&runtime, "test-vm") == REFLEX_OK);
    CHECK("register_service ran the init hook synchronously",
          mock_service_init_calls == before + 1);
    CHECK("cache survives service init", runtime.vm.cache == (struct reflex_cache *)&cache);

    /* Calling the hook directly must behave the same way. */
    runtime.vm.cache = (struct reflex_cache *)&cache;
    CHECK("service_init ok", reflex_vm_task_service_init(&runtime) == REFLEX_OK);
    CHECK("cache still installed after a direct init",
          runtime.vm.cache == (struct reflex_cache *)&cache);

    /* And it must still clear everything else, or it is no longer an init. */
    runtime.running = true;
    runtime.handle = (reflex_task_handle_t)0x1234;
    CHECK("service_init ok (2)", reflex_vm_task_service_init(&runtime) == REFLEX_OK);
    CHECK("init still zeroes runtime state", !runtime.running && runtime.handle == NULL);
    CHECK("cache preserved across that too", runtime.vm.cache == (struct reflex_cache *)&cache);

    /* A runtime with no cache must stay NULL rather than acquire one. */
    static reflex_vm_task_runtime_t plain;
    reflex_vm_task_runtime_init(&plain);
    CHECK("service_init ok (3)", reflex_vm_task_service_init(&plain) == REFLEX_OK);
    CHECK("no cache stays no cache", plain.vm.cache == NULL);

    printf("ok\n");
}

/* --- Interrupt priority rule ------------------------------------------------
 *
 * The C6 controller forwards an interrupt only when its priority is strictly
 * above the threshold, and the threshold is whatever the rest of the system is
 * running at. A hardcoded priority of 1 therefore works or does not depending
 * on configuration — measured on a board as pri=1 against thresh=1 with the
 * source asserting and the core never offered the interrupt.
 *
 * This rule was added during debugging and kept "on its own merits", which is
 * exactly the kind of change that should not sit untested in a hardware
 * abstraction layer. It is pure arithmetic, so it can be. */
static void test_intr_priority_rule(void) {
    printf("[intrpri] ");

    /* Strictly above the threshold, which is the whole point. */
    int above = 1;
    for (uint32_t t = 0; t < REFLEX_INTR_PRIORITY_MAX; t++) {
        if (!(reflex_intr_priority_for(t) > t)) above = 0;
    }
    CHECK("priority always exceeds a representable threshold", above);

    CHECK("threshold 0 -> 1", reflex_intr_priority_for(0) == 1);
    CHECK("threshold 1 -> 2", reflex_intr_priority_for(1) == 2);
    CHECK("threshold 6 -> 7", reflex_intr_priority_for(6) == REFLEX_INTR_PRIORITY_MAX);

    /* At and past the maximum the rule saturates rather than wrapping. A
     * wrapped value would be 0, which the controller reads as "never
     * delivered" — the failure this function exists to avoid, produced by the
     * function itself. */
    CHECK("threshold at max saturates",
          reflex_intr_priority_for(REFLEX_INTR_PRIORITY_MAX) == REFLEX_INTR_PRIORITY_MAX);
    CHECK("threshold past max saturates",
          reflex_intr_priority_for(REFLEX_INTR_PRIORITY_MAX + 5) == REFLEX_INTR_PRIORITY_MAX);
    CHECK("UINT32_MAX threshold does not wrap to 0",
          reflex_intr_priority_for(0xFFFFFFFFu) == REFLEX_INTR_PRIORITY_MAX);

    /* Never 0 and never out of range, for any input at all. */
    int in_range = 1;
    for (uint32_t t = 0; t < 64; t++) {
        uint32_t p = reflex_intr_priority_for(t);
        if (p < 1u || p > REFLEX_INTR_PRIORITY_MAX) in_range = 0;
    }
    CHECK("result always in [1, max]", in_range);

    printf("ok\n");
}

int test_vm_regress(void)
{
    test_cache();
    test_loader_branch_target();
    test_word18_no_clobber();
    test_vm_task_service_init_keeps_cache();
    test_intr_priority_rule();
    return s_fail;
}

int test_vm_regress_passed(void) { return s_pass; }
