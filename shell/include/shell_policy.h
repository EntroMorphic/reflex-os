/** @file shell_policy.h
 * @brief Shell access-control policy — the role a command requires.
 *
 * This is the enforcement point for the role model documented in
 * `SECURITY.md` §2. It used to live inside `shell.c`, which no host test
 * compiles, so nothing pinned it: a subcommand added without a matching
 * escalation entry silently widened access, and no test could have caught it.
 * It is pure string-to-role logic with no hardware dependency, so it is
 * extracted here and covered by `tests/host/test_shell_policy.c` — the same
 * treatment `reflex_log_format` got for the same reason.
 *
 * Roles are a *capability ceiling*, not authentication. The serial cable is
 * the trust boundary; physical access bypasses this by design.
 */
#ifndef REFLEX_SHELL_POLICY_H
#define REFLEX_SHELL_POLICY_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define ROLE_OBSERVER 0 /* Read-only: status, goonies ls/find, temp, telemetry */
#define ROLE_AGENT 1    /* Observer + purpose, snapshot save/load */
#define ROLE_OPERATOR 2 /* Agent + led, vm run/stop, mesh emit/ping */
#define ROLE_ADMIN 3    /* Everything: reboot, aura, config set, vm loadhex */

#define ROLE_COUNT 4

/** @brief Role names, indexed by role level. Parallel to the ROLE_* values. */
extern const char *const shell_role_names[ROLE_COUNT];

/**
 * @brief The role required to run @p cmd with the given arguments.
 *
 * The result is the higher of the command's base role and any sub-command
 * escalation (`vm` is observer, but `vm loadhex` is admin).
 *
 * Fails closed: a command with no entry in the policy table requires
 * ROLE_ADMIN. A new command added to the shell's dispatch table but not to
 * the policy table is therefore locked down rather than exposed to observers.
 *
 * @param cmd   Command name (argv[0]).
 * @param argc  Full argument count, including the command itself.
 * @param argv  Full argument vector.
 * @return Minimum role level required.
 */
uint8_t shell_required_role(const char *cmd, int argc, char *argv[]);

/** @brief True if @p cmd has an explicit entry in the policy table. */
bool shell_command_known(const char *cmd);

/** @brief Number of commands in the policy table. */
size_t shell_policy_count(void);

/** @brief Name of policy table entry @p i, or NULL if out of range. */
const char *shell_policy_name(size_t i);

#endif /* REFLEX_SHELL_POLICY_H */
