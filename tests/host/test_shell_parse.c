/**
 * @file test_shell_parse.c
 * @brief Tests for operator-input parsing in the shell.
 *
 * Both parsers replaced a C library call that signals failure by returning a
 * perfectly valid value. `atoi("abc")` is 0, a legal trit.
 * `strtoul("zz", NULL, 16)` is 0, a legal byte. In each case a typo became
 * data rather than an error and the shell reported success.
 *
 * The hex case was the one that mattered: `aura setkey` writes its 16 bytes
 * straight into the mesh HMAC key with nothing downstream to check them, so
 * 32 non-hex characters provisioned an all-zero key — a guessable shared
 * secret — and printed "aura: key provisioned". The cases below pin both the
 * rejection and the exact bytes produced on the happy path, since a decoder
 * that rejects correctly but decodes wrongly would be just as bad.
 */

#include <limits.h>
#include <stdio.h>
#include <string.h>

#include "shell_parse.h"

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

/* --- Bounded integers ---
 * `config set log_level abc` set the level to 0, and `mesh posture 1 abc`
 * broadcast weight 0, because atoi() reports failure as a valid value. */
static void test_int(void) {
    long v;

    v = 999;
    CHECK("parses 0", shell_parse_int("0", -10, 10, &v) && v == 0);
    v = 999;
    CHECK("parses a negative", shell_parse_int("-7", -10, 10, &v) && v == -7);
    v = 999;
    CHECK("parses an explicit plus", shell_parse_int("+7", -10, 10, &v) && v == 7);
    v = 999;
    CHECK("accepts the lower bound", shell_parse_int("-10", -10, 10, &v) && v == -10);
    v = 999;
    CHECK("accepts the upper bound", shell_parse_int("10", -10, 10, &v) && v == 10);

    CHECK("rejects below the lower bound", !shell_parse_int("-11", -10, 10, &v));
    CHECK("rejects above the upper bound", !shell_parse_int("11", -10, 10, &v));

    /* The defect: non-numeric must not become 0. */
    CHECK("rejects abc", !shell_parse_int("abc", -10, 10, &v));
    CHECK("rejects trailing garbage", !shell_parse_int("5x", -10, 10, &v));
    CHECK("rejects empty", !shell_parse_int("", -10, 10, &v));
    CHECK("rejects whitespace only", !shell_parse_int("   ", -10, 10, &v));
    CHECK("rejects NULL", !shell_parse_int(NULL, -10, 10, &v));
    CHECK("rejects hex form", !shell_parse_int("0x5", -10, 10, &v));

    /* Overflow of `long` itself must not wrap into an accepted range. */
    CHECK("rejects long overflow", !shell_parse_int("99999999999999999999999", -10, 10, &v));
    CHECK("rejects long underflow", !shell_parse_int("-99999999999999999999999", -10, 10, &v));

    /* With the full `long` range the bounds check cannot catch saturation, so
     * this is what makes the ERANGE test load-bearing: strtol returns
     * LONG_MAX, which is *inside* [LONG_MIN, LONG_MAX]. */
    CHECK("rejects overflow even at full long range",
          !shell_parse_int("99999999999999999999999", LONG_MIN, LONG_MAX, &v));
    CHECK("rejects underflow even at full long range",
          !shell_parse_int("-99999999999999999999999", LONG_MIN, LONG_MAX, &v));
    /* Built from LONG_MAX rather than written out, so this does not silently
     * become an overflow case on a platform with a 32-bit long. */
    char longmax[32];
    snprintf(longmax, sizeof(longmax), "%ld", LONG_MAX);
    v = 999;
    CHECK("accepts LONG_MAX itself at full range",
          shell_parse_int(longmax, LONG_MIN, LONG_MAX, &v) && v == LONG_MAX);

    /* The weight range `mesh posture` uses. */
    v = 999;
    CHECK("weight 0 accepted", shell_parse_int("0", 0, 255, &v) && v == 0);
    v = 999;
    CHECK("weight 255 accepted", shell_parse_int("255", 0, 255, &v) && v == 255);
    CHECK("weight 256 rejected", !shell_parse_int("256", 0, 255, &v));
    CHECK("weight -1 rejected", !shell_parse_int("-1", 0, 255, &v));

    v = 4242;
    (void)shell_parse_int("abc", -10, 10, &v);
    CHECK("failure leaves out untouched", v == 4242);
}

/* --- Trits --- */
static void test_trit(void) {
    int8_t v;

    v = 42;
    CHECK("parses -1", shell_parse_trit("-1", &v) && v == -1);
    v = 42;
    CHECK("parses 0", shell_parse_trit("0", &v) && v == 0);
    v = 42;
    CHECK("parses 1", shell_parse_trit("1", &v) && v == 1);
    v = 42;
    CHECK("parses +1", shell_parse_trit("+1", &v) && v == 1);

    /* Out of range must not reach a three-value enum. */
    CHECK("rejects 99", !shell_parse_trit("99", &v));
    CHECK("rejects -99", !shell_parse_trit("-99", &v));
    CHECK("rejects 2", !shell_parse_trit("2", &v));
    CHECK("rejects -2", !shell_parse_trit("-2", &v));

    /* The silent-zero defect: non-numeric must fail, not yield 0. */
    CHECK("rejects abc", !shell_parse_trit("abc", &v));
    CHECK("rejects empty", !shell_parse_trit("", &v));
    CHECK("rejects 1abc", !shell_parse_trit("1abc", &v));
    CHECK("rejects 0x1", !shell_parse_trit("0x1", &v));
    CHECK("rejects whitespace only", !shell_parse_trit("  ", &v));
    CHECK("rejects NULL", !shell_parse_trit(NULL, &v));

    /* Overflow must not wrap into range. */
    CHECK("rejects 4294967297", !shell_parse_trit("4294967297", &v));
    CHECK("rejects 99999999999999999999", !shell_parse_trit("99999999999999999999", &v));

    v = 7;
    (void)shell_parse_trit("99", &v);
    CHECK("failure leaves out untouched", v == 7);
}

/* --- Hex --- */
static void test_hex(void) {
    uint8_t buf[16];
    size_t n;

    /* Happy path: the bytes must be right, not merely accepted. */
    memset(buf, 0xAA, sizeof(buf));
    n = 0;
    CHECK("decodes lowercase", shell_parse_hex("deadbeef", buf, sizeof(buf), &n) && n == 4 &&
                                   buf[0] == 0xde && buf[1] == 0xad && buf[2] == 0xbe &&
                                   buf[3] == 0xef);
    memset(buf, 0xAA, sizeof(buf));
    n = 0;
    CHECK("decodes uppercase", shell_parse_hex("DEADBEEF", buf, sizeof(buf), &n) && n == 4 &&
                                   buf[0] == 0xde && buf[3] == 0xef);
    memset(buf, 0xAA, sizeof(buf));
    n = 0;
    CHECK("decodes mixed case", shell_parse_hex("DeAdBeEf", buf, sizeof(buf), &n) && n == 4 &&
                                    buf[0] == 0xde && buf[3] == 0xef);
    memset(buf, 0xAA, sizeof(buf));
    n = 0;
    CHECK("decodes 00 and ff", shell_parse_hex("00ff", buf, sizeof(buf), &n) && n == 2 &&
                                   buf[0] == 0x00 && buf[1] == 0xff);
    memset(buf, 0xAA, sizeof(buf));
    n = 0;
    CHECK("decodes every nibble value", shell_parse_hex("0123456789abcdef", buf, sizeof(buf), &n) &&
                                            n == 8 && buf[0] == 0x01 && buf[1] == 0x23 &&
                                            buf[2] == 0x45 && buf[3] == 0x67 && buf[4] == 0x89 &&
                                            buf[5] == 0xab && buf[6] == 0xcd && buf[7] == 0xef);

    /* Exactly the aura setkey shape: 32 chars -> 16 bytes. */
    memset(buf, 0xAA, sizeof(buf));
    n = 0;
    CHECK("decodes a full 16-byte key",
          shell_parse_hex("000102030405060708090a0b0c0d0e0f", buf, sizeof(buf), &n) && n == 16 &&
              buf[0] == 0x00 && buf[15] == 0x0f);

    /* The defect: non-hex must be rejected, never coerced to zero. */
    CHECK("rejects all non-hex (the aura all-zero key)",
          !shell_parse_hex("zzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzz", buf, sizeof(buf), &n));
    CHECK("rejects partial non-hex",
          !shell_parse_hex("deadbeefzzzzzzzzdeadbeefzzzzzzzz", buf, sizeof(buf), &n));
    CHECK("rejects a single bad nibble", !shell_parse_hex("dexd", buf, sizeof(buf), &n));
    CHECK("rejects bad nibble in last position", !shell_parse_hex("deax", buf, sizeof(buf), &n));
    CHECK("rejects 0x prefix", !shell_parse_hex("0xdead", buf, sizeof(buf), &n));
    CHECK("rejects embedded space", !shell_parse_hex("de ad", buf, sizeof(buf), &n));
    CHECK("rejects leading space", !shell_parse_hex(" dead", buf, sizeof(buf), &n));
    CHECK("rejects sign", !shell_parse_hex("-dead", buf, sizeof(buf), &n));

    /* Length rules. */
    CHECK("rejects odd length", !shell_parse_hex("abc", buf, sizeof(buf), &n));
    CHECK("rejects single char", !shell_parse_hex("a", buf, sizeof(buf), &n));
    CHECK("rejects empty", !shell_parse_hex("", buf, sizeof(buf), &n));

    /* Capacity. */
    CHECK("rejects overflow by one byte",
          !shell_parse_hex("000102030405060708090a0b0c0d0e0f10", buf, sizeof(buf), &n));
    n = 0;
    CHECK("accepts exactly capacity",
          shell_parse_hex("000102030405060708090a0b0c0d0e0f", buf, sizeof(buf), &n) && n == 16);
    CHECK("rejects two bytes into a one-byte buffer", !shell_parse_hex("dead", buf, 1, &n));
    n = 0;
    CHECK("accepts one byte into a one-byte buffer",
          shell_parse_hex("de", buf, 1, &n) && n == 1 && buf[0] == 0xde);

    /* The `mesh peer add` shape: six independent one-byte decodes out of a
     * colon-separated MAC. Its accepting path cannot be exercised on hardware
     * without growing a peer registry capped at 8, so it is pinned here. */
    {
        const char *mac_txt[6] = {"B4", "3A", "45", "8a", "c7", "d4"};
        const uint8_t want[6] = {0xb4, 0x3a, 0x45, 0x8a, 0xc7, 0xd4};
        uint8_t mac[6];
        int all_ok = 1;
        for (int i = 0; i < 6; i++) {
            size_t got = 0;
            if (!shell_parse_hex(mac_txt[i], &mac[i], 1, &got) || got != 1) all_ok = 0;
        }
        CHECK("MAC octets decode (mixed case)", all_ok && memcmp(mac, want, 6) == 0);

        /* One bad octet must fail that octet, which is what makes the shell's
         * loop reject the whole address. */
        size_t got = 0;
        CHECK("a non-hex MAC octet is refused", !shell_parse_hex("zz", &mac[0], 1, &got));
        CHECK("a one-and-a-half octet is refused", !shell_parse_hex("b", &mac[0], 1, &got));
    }

    /* NULL handling. */
    CHECK("rejects NULL string", !shell_parse_hex(NULL, buf, sizeof(buf), &n));
    CHECK("rejects NULL out", !shell_parse_hex("dead", NULL, sizeof(buf), &n));
    CHECK("rejects NULL out_len", !shell_parse_hex("dead", buf, sizeof(buf), NULL));

    /* A rejected parse must not leave a half-decoded buffer behind — `aura
     * setkey` would otherwise write a partially-typo'd key. */
    memset(buf, 0xAA, sizeof(buf));
    (void)shell_parse_hex("deadbeefzzzzzzzzdeadbeefzzzzzzzz", buf, sizeof(buf), &n);
    int untouched = 1;
    for (size_t i = 0; i < sizeof(buf); i++) {
        if (buf[i] != 0xAA) untouched = 0;
    }
    CHECK("failure leaves the output buffer untouched", untouched);

    /* out_len must not be advanced on failure either. */
    n = 1234;
    (void)shell_parse_hex("zz", buf, sizeof(buf), &n);
    CHECK("failure leaves out_len untouched", n == 1234);
}

int test_shell_parse(void) {
    printf("[shellparse] ");
    test_int();
    test_trit();
    test_hex();
    printf("ok\n");
    return s_fail;
}

int test_shell_parse_passed(void) {
    return s_pass;
}
