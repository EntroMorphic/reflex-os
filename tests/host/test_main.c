/**
 * @file test_main.c
 * @brief Reflex OS host-side test runner.
 *
 * Runs without hardware. Tests: ternary math, crypto, task API, queues.
 */

#include <stdio.h>
#include <string.h>
#include <assert.h>
#include "reflex_types.h"
#include "reflex_hal.h"
#include "reflex_task.h"
#include "reflex_crypto.h"

static int s_passed = 0;
static int s_failed = 0;

#define TEST(name, expr) do { \
    if (expr) { s_passed++; } \
    else { printf("  FAIL: %s\n", name); s_failed++; } \
} while(0)

/* --- Ternary tests (existing) --- */
extern reflex_err_t reflex_ternary_self_check(void);

static void test_ternary(void) {
    printf("[ternary] ");
    TEST("self_check", reflex_ternary_self_check() == REFLEX_OK);
    printf("ok\n");
}

/* --- Crypto tests --- */

/* Compare a computed MAC against a hex-encoded expected value. */
static int hmac_matches(const uint8_t *key, size_t key_len,
                        const void *data, size_t data_len,
                        const char *expect_hex) {
    uint8_t out[32];
    if (reflex_hmac_sha256(key, key_len, (const uint8_t *)data, data_len, out) != REFLEX_OK)
        return 0;
    for (int i = 0; i < 32; i++) {
        unsigned byte;
        if (sscanf(expect_hex + (i * 2), "%2x", &byte) != 1) return 0;
        if (out[i] != (uint8_t)byte) return 0;
    }
    return 1;
}

static void test_crypto(void) {
    printf("[crypto]  ");

    /* Known-answer tests from RFC 4231.
     *
     * This suite previously asserted only that the MAC was deterministic and
     * that it changed when the key or message changed. Every one of those
     * holds for an implementation that computes the wrong digest — and because
     * both sides of the mesh run this same code, a wrong-but-consistent
     * SHA-256 would have authenticated every arc between two boards happily
     * while being incompatible with every other HMAC-SHA-256 on earth. The
     * Aura MAC is the only thing standing between the substrate and an
     * attacker-supplied arc, so it is checked against the standard's vectors
     * rather than against itself.
     *
     * Case 6 is the one that matters structurally: a 131-byte key exercises
     * the key_len > 64 branch, which folds the key through SHA-256 first and
     * which nothing here had ever executed. */
    uint8_t key20[20], key131[131], data50[50];
    memset(key20, 0x0b, sizeof(key20));
    memset(key131, 0xaa, sizeof(key131));
    memset(data50, 0xdd, sizeof(data50));

    TEST("rfc4231_case1",
         hmac_matches(key20, 20, "Hi There", 8,
                      "b0344c61d8db38535ca8afceaf0bf12b881dc200c9833da726e9376c2e32cff7"));

    TEST("rfc4231_case2",
         hmac_matches((const uint8_t *)"Jefe", 4,
                      "what do ya want for nothing?", 28,
                      "5bdcc146bf60754e6a042426089575c75a003f089d2739839dec58b964ec3843"));

    memset(key20, 0xaa, sizeof(key20));
    TEST("rfc4231_case3",
         hmac_matches(key20, 20, data50, 50,
                      "773ea91e36800e46854db8ebd09181a72959098b3ef8c122d9635514ced565fe"));

    /* Key longer than the 64-byte block: hashed down before use. */
    TEST("rfc4231_case6_long_key",
         hmac_matches(key131, 131,
                      "Test Using Larger Than Block-Size Key - Hash Key First", 54,
                      "60e431591ee0b67f0d8a26aacbf5b77f8e0bc6213728c5140546040f0ee37f54"));

    /* Long key *and* a message spanning several blocks. */
    TEST("rfc4231_case7_long_key_and_data",
         hmac_matches(key131, 131,
                      "This is a test using a larger than block-size key and a "
                      "larger than block-size data. The key needs to be hashed "
                      "before being used by the HMAC algorithm.", 152,
                      "9b09ffa71b942fcb27635fbcd5b0e944bfdc63644f0713938a7f51535c3a35e2"));

    /* The 16-byte width the Aura key actually uses. Cross-checked against
     * OpenSSL rather than taken from the RFC, which has no 16-byte case. */
    uint8_t key16[16];
    for (int i = 0; i < 16; i++) key16[i] = (uint8_t)i;
    TEST("aura_width_16_byte_key",
         hmac_matches(key16, 16, "reflex", 6,
                      "dfd7087e2bb7752783e5c2780733a453076e0d19a21e50002b44dc042c4e7a79"));

    /* Behavioural properties, kept: they catch a stuck or key-ignoring MAC
     * that somehow still matched a vector. */
    uint8_t out[32], out2[32];
    reflex_hmac_sha256((const uint8_t *)"key", 3, (const uint8_t *)"message", 7, out);
    reflex_hmac_sha256((const uint8_t *)"key", 3, (const uint8_t *)"other", 5, out2);
    TEST("different_input", memcmp(out, out2, 32) != 0);
    reflex_hmac_sha256((const uint8_t *)"key2", 4, (const uint8_t *)"message", 7, out2);
    TEST("different_key", memcmp(out, out2, 32) != 0);

    printf("ok\n");
}

/* --- Task API tests --- */
static void test_tasks(void) {
    printf("[tasks]   ");

    reflex_task_handle_t h = NULL;
    reflex_err_t rc = reflex_task_create(NULL, "test1", 1024, NULL, 5, &h);
    /* NULL fn should still create in mock (mock doesn't validate fn) */
    TEST("create", rc == REFLEX_OK && h != NULL);

    TEST("get_priority", reflex_task_get_priority(h) == 5);

    reflex_task_set_priority(h, 12);
    TEST("set_priority", reflex_task_get_priority(h) == 12);

    reflex_task_handle_t found = reflex_task_get_by_name("test1");
    TEST("get_by_name", found == h);

    TEST("get_by_name_miss", reflex_task_get_by_name("nonexistent") == NULL);

    printf("ok\n");
}

/* --- Queue tests --- */
static void test_queues(void) {
    printf("[queues]  ");

    reflex_queue_handle_t q = reflex_queue_create(4, sizeof(int));
    TEST("create", q != NULL);

    int val = 42;
    TEST("send", reflex_queue_send(q, &val, 0) == REFLEX_OK);

    val = 99;
    TEST("send2", reflex_queue_send(q, &val, 0) == REFLEX_OK);

    int recv = 0;
    TEST("recv", reflex_queue_recv(q, &recv, 0) == REFLEX_OK);
    TEST("recv_val", recv == 42);

    TEST("recv2", reflex_queue_recv(q, &recv, 0) == REFLEX_OK);
    TEST("recv2_val", recv == 99);

    TEST("recv_empty", reflex_queue_recv(q, &recv, 0) == REFLEX_ERR_NOT_FOUND);

    /* Fill to capacity */
    for (int i = 0; i < 4; i++) reflex_queue_send(q, &i, 0);
    TEST("send_full", reflex_queue_send(q, &val, 0) == REFLEX_ERR_TIMEOUT);

    printf("ok\n");
}

/* --- HAL mock tests --- */
static void test_hal(void) {
    printf("[hal]     ");

    TEST("time_starts_zero", reflex_hal_time_us() >= 0);

    reflex_hal_delay_us(1000);
    uint64_t t = reflex_hal_time_us();
    TEST("delay_advances_time", t >= 1000);

    TEST("gpio_init", reflex_hal_gpio_init_output(5) == REFLEX_OK);
    TEST("gpio_set", reflex_hal_gpio_set_level(5, 1) == REFLEX_OK);
    TEST("gpio_get", reflex_hal_gpio_get_level(5) == 1);
    TEST("gpio_bounds", reflex_hal_gpio_init_output(99) == REFLEX_ERR_INVALID_ARG);

    uint8_t mac[6] = {0};
    TEST("mac_read", reflex_hal_mac_read(mac) == REFLEX_OK);
    TEST("mac_nonzero", mac[0] != 0);

    printf("ok\n");
}

int main(void) {
    printf("Reflex OS Host Tests\n");
    printf("=====================\n\n");

    extern int test_scheduler(void);
    extern int test_kv(void);
    extern int test_integration(void);
    extern int test_policy(void);
    extern int test_policy_passed(void);
    extern int test_vm_regress(void);
    extern int test_vm_regress_passed(void);
    extern int test_log_format(void);
    extern int test_log_format_passed(void);
    extern int test_lattice(void);
    extern int test_lattice_passed(void);
    extern int test_registry(void);
    extern int test_registry_passed(void);
    extern int test_shell_policy(void);
    extern int test_shell_policy_passed(void);
    extern int test_shell_parse(void);
    extern int test_shell_parse_passed(void);
    extern int test_shell_outcome(void);
    extern int test_shell_outcome_passed(void);
    extern int test_reflex_queue(void);
    extern int test_reflex_queue_passed(void);

    test_ternary();
    test_crypto();
    test_tasks();
    test_queues();
    test_hal();
    s_failed += test_scheduler();
    s_failed += test_kv();
    s_failed += test_integration();
    s_failed += test_policy();
    s_passed += test_policy_passed();
    s_failed += test_vm_regress();
    s_passed += test_vm_regress_passed();
    s_failed += test_log_format();
    s_passed += test_log_format_passed();
    s_failed += test_lattice();
    s_passed += test_lattice_passed();
    s_failed += test_registry();
    s_passed += test_registry_passed();
    s_failed += test_shell_policy();
    s_passed += test_shell_policy_passed();
    s_failed += test_shell_parse();
    s_passed += test_shell_parse_passed();
    s_failed += test_shell_outcome();
    s_passed += test_shell_outcome_passed();
    s_failed += test_reflex_queue();
    s_passed += test_reflex_queue_passed();

    printf("\n%d passed, %d failed\n", s_passed, s_failed);
    return s_failed > 0 ? 1 : 0;
}
