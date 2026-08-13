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
    test_trit();
    test_hex();
    printf("ok\n");
    return s_fail;
}

int test_shell_parse_passed(void) {
    return s_pass;
}
