/**
 * @file reflex_log_format.c
 * @brief Log line formatting, shared by every platform HAL.
 *
 * Pure: no hardware, no SDK, no state. It lives under platform/ because the
 * HALs are its only callers — core/ already REQUIRES the platform component,
 * so putting it there would invert the dependency — but nothing in it is
 * platform-specific, which is the point. The ESP32-C6 HAL used to inline this
 * logic where no host test could reach it, and it was wrong.
 */

#include "reflex_hal.h"

#include <stdarg.h>
#include <stdio.h>

size_t reflex_log_format(char *buf, size_t buf_len,
                         const char *prefix, const char *tag,
                         const char *fmt, va_list args)
{
    /* snprintf and vsnprintf return the length they *would* have written, not
     * the length they did. The C6 HAL accumulated those returns as if they were
     * byte counts, which let the running offset pass the end of its buffer:
     *
     *   - `buf + off` pointed past the buffer;
     *   - `buf_len - off` is size_t arithmetic, so it underflowed to roughly
     *     1.8e19 and told vsnprintf it had unlimited room there;
     *   - the caller then wrote `off` bytes to the serial port, reading past
     *     the end and streaming whatever followed the buffer in .bss.
     *
     * The last one needed nothing exotic: any line longer than about 236
     * characters did it. Clamping after every write is the whole fix.
     *
     * Contract: never touches buf beyond buf_len, and the return is always
     * <= buf_len, so a caller can hand it straight to a write primitive. The
     * result is not NUL-terminated when it fills the buffer exactly — callers
     * are expected to use the returned length, not strlen.
     */
    if (!buf || buf_len == 0) return 0;

    size_t off = 0;
    int n = snprintf(buf, buf_len, "%s (%s) ",
                     prefix ? prefix : "?", tag ? tag : "?");
    if (n > 0) off = ((size_t)n < buf_len) ? (size_t)n : buf_len - 1;

    if (fmt && off + 1 < buf_len) {
        n = vsnprintf(buf + off, buf_len - off, fmt, args);
        if (n > 0) {
            size_t remaining = buf_len - off - 1;   /* keep room for the newline */
            off += ((size_t)n < remaining) ? (size_t)n : remaining;
        }
    }

    if (off < buf_len) buf[off++] = '\n';
    return off;
}
