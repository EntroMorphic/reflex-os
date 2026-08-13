/** @file shell_outcome.c
 * @brief Ternary command outcomes. See shell_outcome.h.
 */
#include "shell_outcome.h"

#include <stdio.h>

/* One table, so the trit and the token cannot drift apart. Adding a reason
 * without a trit is a compile-time hole rather than a runtime surprise: the
 * count assertion below fails. */
typedef struct {
    const char *name;
    int8_t trit;
} outcome_entry_t;

static const outcome_entry_t s_outcomes[] = {
    [SHELL_OK] = {"ok", 1},
    [SHELL_USAGE] = {"usage", 0},
    [SHELL_NONE] = {"none", 0},
    [SHELL_DENIED] = {"denied", -1},
    [SHELL_INVALID] = {"invalid", -1},
    [SHELL_GUARD] = {"guard", -1},
    [SHELL_NOTFOUND] = {"notfound", -1},
    [SHELL_FAILED] = {"failed", -1},
};

_Static_assert(sizeof(s_outcomes) / sizeof(s_outcomes[0]) == SHELL_REASON_COUNT,
               "every shell_reason_t needs a name and a trit");

static int in_range(shell_reason_t r) {
    return (int)r >= 0 && (int)r < SHELL_REASON_COUNT;
}

int8_t shell_outcome_trit(shell_reason_t r) {
    /* An unrecognised reason reports withheld, never success — "I do not know
     * what happened" must not be indistinguishable from "it worked". */
    return in_range(r) ? s_outcomes[r].trit : -1;
}

const char *shell_outcome_name(shell_reason_t r) {
    return in_range(r) ? s_outcomes[r].name : "unknown";
}

size_t shell_outcome_format(char *buf, size_t cap, shell_reason_t r) {
    if (!buf || cap == 0) return 0;
    int8_t t = shell_outcome_trit(r);
    /* "+1" / "0" / "-1" — an unsigned-looking "+0" would misread as engaged. */
    const char *trit_txt = (t > 0) ? "+1" : (t < 0) ? "-1" : "0";
    int n = snprintf(buf, cap, "#R:%s,%s", trit_txt, shell_outcome_name(r));
    if (n < 0) {
        buf[0] = '\0';
        return 0;
    }
    /* snprintf returns what it *would* have written; report what it did. */
    return ((size_t)n >= cap) ? cap - 1 : (size_t)n;
}
