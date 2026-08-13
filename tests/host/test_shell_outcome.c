/**
 * @file test_shell_outcome.c
 * @brief Tests for the shell's ternary command outcome.
 *
 * The marker exists so the Python SDK stops reconstructing outcomes from
 * English. Its whole value rests on one invariant: **a reason implies its
 * trit**, so `#R:+1,denied` cannot be emitted. If a reason could carry the
 * wrong trit, a consumer keying on the trit would be worse off than one
 * matching prose, because it would be confidently wrong.
 *
 * The exact wire strings are pinned too. They are the API now, in the way the
 * human-readable messages accidentally were before.
 */

#include <stdio.h>
#include <string.h>

#include "shell_outcome.h"

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

static void test_trits(void) {
    CHECK("ok is engaged", shell_outcome_trit(SHELL_OK) == 1);

    CHECK("usage is latent", shell_outcome_trit(SHELL_USAGE) == 0);
    CHECK("none is latent", shell_outcome_trit(SHELL_NONE) == 0);

    CHECK("denied is withheld", shell_outcome_trit(SHELL_DENIED) == -1);
    CHECK("invalid is withheld", shell_outcome_trit(SHELL_INVALID) == -1);
    CHECK("guard is withheld", shell_outcome_trit(SHELL_GUARD) == -1);
    CHECK("notfound is withheld", shell_outcome_trit(SHELL_NOTFOUND) == -1);
    CHECK("failed is withheld", shell_outcome_trit(SHELL_FAILED) == -1);
    CHECK("overflow is withheld", shell_outcome_trit(SHELL_OVERFLOW) == -1);

    /* The invariant the whole marker rests on. */
    int only_ok_engaged = 1;
    for (int r = 0; r < SHELL_REASON_COUNT; r++) {
        if (shell_outcome_trit((shell_reason_t)r) == 1 && r != SHELL_OK) only_ok_engaged = 0;
    }
    CHECK("no reason but ok reports engaged", only_ok_engaged);

    int all_in_range = 1;
    for (int r = 0; r < SHELL_REASON_COUNT; r++) {
        int8_t t = shell_outcome_trit((shell_reason_t)r);
        if (t < -1 || t > 1) all_in_range = 0;
    }
    CHECK("every reason maps to a real trit", all_in_range);

    /* "I do not know what happened" must never read as success. */
    CHECK("out-of-range reason is withheld",
          shell_outcome_trit((shell_reason_t)SHELL_REASON_COUNT) == -1);
    CHECK("negative reason is withheld", shell_outcome_trit((shell_reason_t)-1) == -1);
}

static void test_names(void) {
    CHECK("name ok", strcmp(shell_outcome_name(SHELL_OK), "ok") == 0);
    CHECK("name usage", strcmp(shell_outcome_name(SHELL_USAGE), "usage") == 0);
    CHECK("name none", strcmp(shell_outcome_name(SHELL_NONE), "none") == 0);
    CHECK("name denied", strcmp(shell_outcome_name(SHELL_DENIED), "denied") == 0);
    CHECK("name invalid", strcmp(shell_outcome_name(SHELL_INVALID), "invalid") == 0);
    CHECK("name guard", strcmp(shell_outcome_name(SHELL_GUARD), "guard") == 0);
    CHECK("name notfound", strcmp(shell_outcome_name(SHELL_NOTFOUND), "notfound") == 0);
    CHECK("name failed", strcmp(shell_outcome_name(SHELL_FAILED), "failed") == 0);
    CHECK("name overflow", strcmp(shell_outcome_name(SHELL_OVERFLOW), "overflow") == 0);
    CHECK("out-of-range name", strcmp(shell_outcome_name((shell_reason_t)99), "unknown") == 0);

    /* Names must be distinct, or a consumer cannot tell two reasons apart. */
    int distinct = 1;
    for (int a = 0; a < SHELL_REASON_COUNT; a++) {
        for (int b = a + 1; b < SHELL_REASON_COUNT; b++) {
            if (strcmp(shell_outcome_name((shell_reason_t)a),
                       shell_outcome_name((shell_reason_t)b)) == 0)
                distinct = 0;
        }
    }
    CHECK("every reason name is distinct", distinct);
}

static void test_format(void) {
    char b[32];

    shell_outcome_format(b, sizeof(b), SHELL_OK);
    CHECK("format ok", strcmp(b, "#R:+1,ok") == 0);
    shell_outcome_format(b, sizeof(b), SHELL_USAGE);
    CHECK("format usage", strcmp(b, "#R:0,usage") == 0);
    shell_outcome_format(b, sizeof(b), SHELL_NONE);
    CHECK("format none", strcmp(b, "#R:0,none") == 0);
    shell_outcome_format(b, sizeof(b), SHELL_DENIED);
    CHECK("format denied", strcmp(b, "#R:-1,denied") == 0);
    shell_outcome_format(b, sizeof(b), SHELL_INVALID);
    CHECK("format invalid", strcmp(b, "#R:-1,invalid") == 0);
    shell_outcome_format(b, sizeof(b), SHELL_GUARD);
    CHECK("format guard", strcmp(b, "#R:-1,guard") == 0);
    shell_outcome_format(b, sizeof(b), SHELL_NOTFOUND);
    CHECK("format notfound", strcmp(b, "#R:-1,notfound") == 0);
    shell_outcome_format(b, sizeof(b), SHELL_FAILED);
    CHECK("format failed", strcmp(b, "#R:-1,failed") == 0);
    shell_outcome_format(b, sizeof(b), SHELL_OVERFLOW);
    CHECK("format overflow", strcmp(b, "#R:-1,overflow") == 0);

    /* Latent must be "0", never "+0" — a consumer scanning for a leading '+'
     * would read that as engaged. */
    shell_outcome_format(b, sizeof(b), SHELL_USAGE);
    CHECK("latent has no sign", strchr(b, '+') == NULL);

    /* Return value is what was written, not what would have been. */
    size_t n = shell_outcome_format(b, sizeof(b), SHELL_DENIED);
    CHECK("returns written length", n == strlen(b));

    /* Truncation stays NUL-terminated and never over-reports. */
    char small[6];
    memset(small, 0x7E, sizeof(small));
    n = shell_outcome_format(small, sizeof(small), SHELL_NOTFOUND);
    CHECK("truncated is NUL-terminated", small[sizeof(small) - 1] == '\0');
    CHECK("truncated never over-reports", n < sizeof(small) && n == strlen(small));

    /* Degenerate buffers. */
    CHECK("cap 0 writes nothing", shell_outcome_format(b, 0, SHELL_OK) == 0);
    CHECK("NULL buffer is refused", shell_outcome_format(NULL, sizeof(b), SHELL_OK) == 0);

    /* Sweep: every reason produces a well-formed marker carrying its own name
     * and its own trit. */
    int sweep_ok = 1;
    for (int r = 0; r < SHELL_REASON_COUNT; r++) {
        shell_outcome_format(b, sizeof(b), (shell_reason_t)r);
        int8_t t = shell_outcome_trit((shell_reason_t)r);
        const char *want_t = (t > 0) ? "+1" : (t < 0) ? "-1" : "0";
        char want[32];
        snprintf(want, sizeof(want), "#R:%s,%s", want_t, shell_outcome_name((shell_reason_t)r));
        if (strcmp(b, want) != 0) sweep_ok = 0;
    }
    CHECK("sweep: every reason formats consistently", sweep_ok);
}

int test_shell_outcome(void) {
    printf("[outcome] ");
    test_trits();
    test_names();
    test_format();
    printf("ok\n");
    return s_fail;
}

int test_shell_outcome_passed(void) {
    return s_pass;
}
