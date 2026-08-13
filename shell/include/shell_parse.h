/** @file shell_parse.h
 * @brief Turning operator-supplied text into typed values, safely.
 *
 * Every one of these existed inline in `shell.c` at some point, written
 * against a C library function that reports failure by returning a *valid
 * value*: `atoi("abc")` is 0, which is a legal trit, and
 * `strtoul("zz", NULL, 16)` is 0, which is a legal byte. In both cases a typo
 * became data instead of an error, and the shell reported success.
 *
 * That is not a theoretical concern here. `aura setkey` feeds its 16 bytes
 * straight into the mesh HMAC key with nothing downstream to check them, so
 * `aura setkey <32 non-hex chars>` provisioned an all-zero key — a guessable
 * shared secret — and printed "key provisioned".
 *
 * These live in their own translation unit so the host suite can reach them;
 * see `tests/host/test_shell_parse.c`.
 */
#ifndef REFLEX_SHELL_PARSE_H
#define REFLEX_SHELL_PARSE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/**
 * @brief Parse a bounded decimal integer.
 *
 * Rejects empty input, trailing garbage, values outside [@p min, @p max] and
 * anything that overflows `long`.
 *
 * @param s   NUL-terminated input.
 * @param min Lowest accepted value, inclusive.
 * @param max Highest accepted value, inclusive.
 * @param out Receives the value on success; untouched on failure.
 * @return true on success.
 */
bool shell_parse_int(const char *s, long min, long max, long *out);

/**
 * @brief Parse an operator-supplied balanced-ternary digit.
 *
 * Rejects trailing garbage, empty input and anything outside {-1, 0, +1}.
 *
 * @param s   NUL-terminated input.
 * @param out Receives the parsed trit on success; untouched on failure.
 * @return true on success.
 */
bool shell_parse_trit(const char *s, int8_t *out);

/**
 * @brief Decode a hex string into bytes, rejecting any non-hex character.
 *
 * Requires an even length of at least two characters. Accepts upper, lower
 * and mixed case. @p out is left untouched unless the whole string is valid
 * and fits, so a caller cannot act on a half-decoded buffer.
 *
 * @param s       NUL-terminated hex string.
 * @param out     Receives the decoded bytes on success.
 * @param out_cap Capacity of @p out in bytes.
 * @param out_len Receives the number of bytes decoded on success.
 * @return true on success.
 */
bool shell_parse_hex(const char *s, uint8_t *out, size_t out_cap, size_t *out_len);

#endif /* REFLEX_SHELL_PARSE_H */
