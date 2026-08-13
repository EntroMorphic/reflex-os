/**
 * @file test_log_format.c
 * @brief Regression tests for reflex_log_format.
 *
 * The formatting this covers used to live inlined in the ESP32-C6 HAL, where
 * no host test could reach it, and it was wrong: snprintf's return is the
 * length it *would* have written, and accumulating that as a byte count let
 * the offset run past the end of the buffer. The caller then wrote that many
 * bytes to the serial port, reading past the end and emitting adjacent .bss.
 *
 * Every case here asserts against a canary-guarded buffer, so an overrun is
 * detected directly rather than inferred.
 */

#include <stdio.h>
#include <string.h>
#include <stdarg.h>

#include "reflex_hal.h"

static int s_pass = 0, s_fail = 0;

#define CHECK(name, expr) do { \
    if (expr) { s_pass++; } \
    else { printf("  FAIL: %s\n", name); s_fail++; } \
} while (0)

/* A buffer with a poison region on each side. format_into() hands the middle
 * to reflex_log_format and then verifies the guards are untouched, so a write
 * one byte past the end fails the test instead of silently corrupting. */
#define GUARD 16
#define POISON 0xA5

typedef struct {
    unsigned char raw[GUARD + 512 + GUARD];
    size_t len;      /* usable length handed to the formatter */
    size_t written;  /* what it reported */
} guarded_t;

static void format_into(guarded_t *g, size_t usable,
                        const char *prefix, const char *tag,
                        const char *fmt, ...)
{
    memset(g->raw, POISON, sizeof(g->raw));
    g->len = usable;
    va_list args;
    va_start(args, fmt);
    g->written = reflex_log_format((char *)g->raw + GUARD, usable, prefix, tag, fmt, args);
    va_end(args);
}

static int guards_intact(const guarded_t *g)
{
    for (size_t i = 0; i < GUARD; i++) {
        if (g->raw[i] != POISON) return 0;                      /* underrun */
        if (g->raw[GUARD + g->len + i] != POISON) return 0;     /* overrun  */
    }
    return 1;
}

static const char *body(const guarded_t *g) { return (const char *)g->raw + GUARD; }

static void test_log_format_cases(void)
{
    printf("[logfmt]  ");
    guarded_t g;

    /* Ordinary line. */
    format_into(&g, 256, "I", "reflex.boot", "hello %d", 42);
    CHECK("normal: guards intact", guards_intact(&g));
    CHECK("normal: content", strncmp(body(&g), "I (reflex.boot) hello 42\n", g.written) == 0);
    CHECK("normal: length matches content", g.written == strlen("I (reflex.boot) hello 42\n"));
    CHECK("normal: ends with newline", g.raw[GUARD + g.written - 1] == '\n');

    /* The defect. A message far longer than the buffer must truncate, must not
     * overrun, and must not report more bytes than the buffer holds. */
    char huge[1024];
    memset(huge, 'x', sizeof(huge) - 1);
    huge[sizeof(huge) - 1] = '\0';
    format_into(&g, 256, "E", "tag", "%s", huge);
    CHECK("overflow: guards intact", guards_intact(&g));
    CHECK("overflow: reported length within buffer", g.written <= 256);
    CHECK("overflow: still newline-terminated", g.raw[GUARD + g.written - 1] == '\n');

    /* A tag long enough that the prefix alone fills the buffer. This is the
     * path where the old code produced a pointer past the end and a wrapped
     * size_t length for vsnprintf. */
    char longtag[512];
    memset(longtag, 't', sizeof(longtag) - 1);
    longtag[sizeof(longtag) - 1] = '\0';
    format_into(&g, 64, "W", longtag, "body %d", 7);
    CHECK("long tag: guards intact", guards_intact(&g));
    CHECK("long tag: reported length within buffer", g.written <= 64);
    CHECK("long tag: still newline-terminated", g.raw[GUARD + g.written - 1] == '\n');

    /* Message that exactly fills the space left for it. */
    format_into(&g, 32, "I", "t", "%s", "0123456789012345678901234");
    CHECK("exact fit: guards intact", guards_intact(&g));
    CHECK("exact fit: within buffer", g.written <= 32);

    /* Degenerate buffers must not underflow the size arithmetic. */
    format_into(&g, 1, "I", "tag", "anything");
    CHECK("len 1: guards intact", guards_intact(&g));
    CHECK("len 1: writes at most one byte", g.written <= 1);

    format_into(&g, 2, "I", "tag", "anything");
    CHECK("len 2: guards intact", guards_intact(&g));
    CHECK("len 2: within buffer", g.written <= 2);

    /* Zero length and NULL buffer are handled rather than crashing. */
    va_list dummy;
    memset(&dummy, 0, sizeof(dummy));
    CHECK("len 0 returns 0", reflex_log_format((char *)g.raw, 0, "I", "t", "x", dummy) == 0);
    CHECK("NULL buf returns 0", reflex_log_format(NULL, 64, "I", "t", "x", dummy) == 0);

    /* NULL prefix/tag/fmt must not dereference. */
    format_into(&g, 64, NULL, NULL, NULL);
    CHECK("NULL fields: guards intact", guards_intact(&g));
    CHECK("NULL fields: produced something bounded", g.written > 0 && g.written <= 64);

    /* Sweep every buffer size across the interesting range: the invariants
     * must hold for all of them, not just the sizes chosen above. */
    int sweep_ok = 1;
    for (size_t n = 1; n <= 300; n++) {
        format_into(&g, n, "I", "sweep.tag", "%s", huge);
        if (!guards_intact(&g) || g.written > n) { sweep_ok = 0; break; }
    }
    CHECK("sweep 1..300: never overruns, never over-reports", sweep_ok);

    printf("ok\n");
}

int test_log_format(void)
{
    test_log_format_cases();
    return s_fail;
}

int test_log_format_passed(void) { return s_pass; }
