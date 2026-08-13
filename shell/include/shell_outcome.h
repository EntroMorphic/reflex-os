/** @file shell_outcome.h
 * @brief The ternary outcome a shell command reports to its caller.
 *
 * The shell has always computed this and thrown it away. Every command lands
 * in exactly one of three classes — it did the thing, it was refused, or
 * nothing happened — and `reflex_shell_dispatch` knows which at the moment it
 * decides. That was then flattened into English, leaving the Python SDK to
 * reconstruct it with `result.startswith("denied:")`, which makes every
 * human-readable string in the shell an undocumented API.
 *
 * The trit answers one question: **did the intended state change occur?**
 *
 *   +1 engaged   it did
 *    0 latent    nothing was attempted (usage, empty result, no-op)
 *   -1 withheld  it did not — refused, rejected, or failed
 *
 * This is strictly more informative than an exit code, and the difference is
 * load-bearing for the consumer that matters most: an agent running under
 * `role="agent"` has to tell "I was refused" from "I ran and had nothing to
 * report". A binary status cannot express that, which is the same argument
 * `goose_kernel_policy_tick` makes for scheduling stances — `latent` means
 * undecided and is deliberately distinct from `withheld`.
 *
 * The reason qualifies the trit with a short stable token, following the
 * `#T:U,<role>` telemetry convention already in the tree. A reason *implies*
 * its trit, so `#R:+1,denied` is unrepresentable — see
 * `tests/host/test_shell_outcome.c`.
 */
#ifndef REFLEX_SHELL_OUTCOME_H
#define REFLEX_SHELL_OUTCOME_H

#include <stddef.h>
#include <stdint.h>

typedef enum {
    SHELL_OK = 0,   /**< +1 — the command did the thing. */
    SHELL_USAGE,    /**<  0 — malformed invocation; usage printed, nothing done. */
    SHELL_NONE,     /**<  0 — ran, but there was nothing to report or act on. */
    SHELL_DENIED,   /**< -1 — refused by the role gate. */
    SHELL_INVALID,  /**< -1 — operator input rejected by a parser. */
    SHELL_GUARD,    /**< -1 — refused by a policy guard (sanctuary, `sys.*`). */
    SHELL_NOTFOUND, /**< -1 — the named target does not exist. */
    SHELL_FAILED,   /**< -1 — attempted and did not succeed. */
    SHELL_OVERFLOW, /**< -1 — the input line exceeded the buffer and was discarded. */
    SHELL_REASON_COUNT
} shell_reason_t;

/** @brief The trit implied by @p r. Returns -1 for an out-of-range reason,
 *  because "I do not know what happened" must not read as success. */
int8_t shell_outcome_trit(shell_reason_t r);

/** @brief The stable wire token for @p r ("ok", "denied", ...), or "unknown". */
const char *shell_outcome_name(shell_reason_t r);

/**
 * @brief Format the machine-readable result marker for @p r.
 *
 * Writes `#R:<trit>,<reason>` — e.g. `#R:+1,ok`, `#R:0,usage`,
 * `#R:-1,denied`. Always NUL-terminates when @p cap > 0.
 *
 * @return the number of characters written, excluding the NUL.
 */
size_t shell_outcome_format(char *buf, size_t cap, shell_reason_t r);

#endif /* REFLEX_SHELL_OUTCOME_H */
