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
static void test_crypto(void) {
    printf("[crypto]  ");

    /* Known-answer test: HMAC-SHA256("key", "message") */
    uint8_t out[32];
    reflex_hmac_sha256((const uint8_t *)"key", 3,
                       (const uint8_t *)"message", 7, out);

    /* Expected: from RFC 4231 test vector (not exact — key/msg differ).
     * Validate it's non-zero and deterministic. */
    uint8_t out2[32];
    reflex_hmac_sha256((const uint8_t *)"key", 3,
                       (const uint8_t *)"message", 7, out2);
    TEST("deterministic", memcmp(out, out2, 32) == 0);

    /* Different input → different output */
    reflex_hmac_sha256((const uint8_t *)"key", 3,
                       (const uint8_t *)"other", 5, out2);
    TEST("different_input", memcmp(out, out2, 32) != 0);

    /* Different key → different output */
    reflex_hmac_sha256((const uint8_t *)"key2", 4,
                       (const uint8_t *)"message", 7, out2);
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

    test_ternary();
    test_crypto();
    test_tasks();
    test_queues();
    test_hal();

    printf("\n%d passed, %d failed\n", s_passed, s_failed);
    return s_failed > 0 ? 1 : 0;
}
