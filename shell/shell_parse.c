/** @file shell_parse.c
 * @brief Operator-input parsing. See shell_parse.h.
 */
#include "shell_parse.h"

#include <errno.h>
#include <stdlib.h>
#include <string.h>

bool shell_parse_int(const char *s, long min, long max, long *out) {
    if (!s || !out) return false;
    errno = 0;
    char *end;
    long v = strtol(s, &end, 10);
    /* An empty or all-whitespace string leaves end == s, so it needs no
     * separate check. */
    if (end == s || *end != '\0') return false;
    /* Only load-bearing when a caller passes the full `long` range — a
     * narrower [min,max] rejects the saturated LONG_MIN/LONG_MAX anyway. */
    if (errno == ERANGE) return false;
    if (v < min || v > max) return false;
    *out = v;
    return true;
}

bool shell_parse_trit(const char *s, int8_t *out) {
    long v;
    if (!out || !shell_parse_int(s, -1, 1, &v)) return false;
    *out = (int8_t)v;
    return true;
}

/* Returns 0-15, or -1 for anything that is not a hex digit. `strtoul` cannot
 * be used here: it maps "zz" to 0 without signalling, which is exactly the
 * defect this file exists to prevent. */
static int hex_nibble(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

bool shell_parse_hex(const char *s, uint8_t *out, size_t out_cap, size_t *out_len) {
    if (!s || !out || !out_len) return false;

    size_t slen = strlen(s);
    if (slen < 2 || (slen & 1)) return false;

    size_t blen = slen / 2;
    if (blen > out_cap) return false;

    /* Validate the whole string before writing anything, so a caller never
     * sees a partially decoded buffer after a failure. */
    for (size_t i = 0; i < slen; i++) {
        if (hex_nibble(s[i]) < 0) return false;
    }

    for (size_t i = 0; i < blen; i++) {
        out[i] = (uint8_t)((hex_nibble(s[i * 2]) << 4) | hex_nibble(s[i * 2 + 1]));
    }
    *out_len = blen;
    return true;
}
