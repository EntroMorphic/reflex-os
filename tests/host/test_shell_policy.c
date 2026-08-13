/**
 * @file test_shell_policy.c
 * @brief Tests for the shell's role gating.
 *
 * The role model in SECURITY.md §2 is enforced by exactly one function, and
 * until this suite existed that function lived in `shell.c` — a file the host
 * build does not compile. Nothing pinned it. The gating was correct, but a
 * sub-command added without a matching escalation entry would have silently
 * dropped to the base role, and no test would have failed.
 *
 * The cases below are written against SECURITY.md's table rather than against
 * the implementation, so a change to one that is not mirrored in the other is
 * a failure here.
 */

#include <stdio.h>
#include <string.h>

#include "shell_policy.h"

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

/* shell_required_role takes argv as char*[] because that is what the dispatcher
 * has. String literals are const, so build a writable vector. */
#define MAX_ARGV 4
static uint8_t role_of(const char *a0, const char *a1, const char *a2, const char *a3) {
    static char buf[MAX_ARGV][32];
    char *argv[MAX_ARGV];
    const char *in[MAX_ARGV] = {a0, a1, a2, a3};
    int argc = 0;
    for (int i = 0; i < MAX_ARGV; i++) {
        if (!in[i]) break;
        snprintf(buf[i], sizeof(buf[i]), "%s", in[i]);
        argv[i] = buf[i];
        argc++;
    }
    return shell_required_role(argv[0], argc, argv);
}

#define ROLE1(c) role_of(c, NULL, NULL, NULL)
#define ROLE2(c, a) role_of(c, a, NULL, NULL)
#define ROLE3(c, a, b) role_of(c, a, b, NULL)
#define ROLE4(c, a, b, d) role_of(c, a, b, d)

/* --- Base roles: SECURITY.md §2 --- */
static void test_base_roles(void) {
    CHECK("help is observer", ROLE1("help") == ROLE_OBSERVER);
    CHECK("status is observer", ROLE1("status") == ROLE_OBSERVER);
    CHECK("temp is observer", ROLE1("temp") == ROLE_OBSERVER);
    CHECK("services is observer", ROLE1("services") == ROLE_OBSERVER);
    CHECK("heartbeat is observer", ROLE1("heartbeat") == ROLE_OBSERVER);
    CHECK("kernel is observer", ROLE1("kernel") == ROLE_OBSERVER);
    CHECK("auth is observer", ROLE1("auth") == ROLE_OBSERVER);

    CHECK("bonsai is operator", ROLE1("bonsai") == ROLE_OPERATOR);
    CHECK("tapestry is operator", ROLE1("tapestry") == ROLE_OPERATOR);

    CHECK("reboot is admin", ROLE1("reboot") == ROLE_ADMIN);
    CHECK("sleep is admin", ROLE1("sleep") == ROLE_ADMIN);
    CHECK("aura is admin", ROLE1("aura") == ROLE_ADMIN);
    /* aura's base role is what protects it: both sub-commands are admin, so
     * there is no escalation entry to carry the weight. */
    CHECK("aura setkey is admin", ROLE3("aura", "setkey", "00") == ROLE_ADMIN);
    CHECK("aura clear is admin", ROLE2("aura", "clear") == ROLE_ADMIN);
}

/* --- Sub-command escalation --- */
static void test_escalation(void) {
    /* goonies: read dereferences a live register, so it is not observer */
    CHECK("goonies ls is observer", ROLE2("goonies", "ls") == ROLE_OBSERVER);
    CHECK("goonies find is observer", ROLE3("goonies", "find", "x") == ROLE_OBSERVER);
    CHECK("goonies read is operator", ROLE3("goonies", "read", "x") == ROLE_OPERATOR);

    /* vm */
    CHECK("vm info is observer", ROLE2("vm", "info") == ROLE_OBSERVER);
    CHECK("vm list is observer", ROLE2("vm", "list") == ROLE_OBSERVER);
    CHECK("vm run is operator", ROLE3("vm", "run", "p") == ROLE_OPERATOR);
    CHECK("vm stop is operator", ROLE2("vm", "stop") == ROLE_OPERATOR);
    CHECK("vm loadhex is admin", ROLE3("vm", "loadhex", "AA") == ROLE_ADMIN);

    /* loom load weaves operator-supplied bytes — vm loadhex's privilege class */
    CHECK("loom list is observer", ROLE2("loom", "list") == ROLE_OBSERVER);
    CHECK("loom evictions is observer", ROLE2("loom", "evictions") == ROLE_OBSERVER);
    CHECK("loom fragments is observer", ROLE2("loom", "fragments") == ROLE_OBSERVER);
    CHECK("loom load is admin", ROLE3("loom", "load", "AA") == ROLE_ADMIN);

    /* snapshot */
    CHECK("snapshot save is agent", ROLE2("snapshot", "save") == ROLE_AGENT);
    CHECK("snapshot load is agent", ROLE2("snapshot", "load") == ROLE_AGENT);
    CHECK("snapshot clear is admin", ROLE2("snapshot", "clear") == ROLE_ADMIN);

    /* purpose: the AI guardrail role */
    CHECK("purpose get is observer", ROLE2("purpose", "get") == ROLE_OBSERVER);
    CHECK("purpose set is agent", ROLE3("purpose", "set", "nav") == ROLE_AGENT);
    CHECK("purpose clear is agent", ROLE2("purpose", "clear") == ROLE_AGENT);

    /* config */
    CHECK("config get is observer", ROLE3("config", "get", "k") == ROLE_OBSERVER);
    CHECK("config set is admin", ROLE4("config", "set", "k", "v") == ROLE_ADMIN);

    /* led */
    CHECK("led status is observer", ROLE2("led", "status") == ROLE_OBSERVER);
    CHECK("led on is operator", ROLE2("led", "on") == ROLE_OPERATOR);
    CHECK("led off is operator", ROLE2("led", "off") == ROLE_OPERATOR);

    /* telemetry */
    CHECK("telemetry bare is observer", ROLE1("telemetry") == ROLE_OBSERVER);
    CHECK("telemetry on is operator", ROLE2("telemetry", "on") == ROLE_OPERATOR);
    CHECK("telemetry off is operator", ROLE2("telemetry", "off") == ROLE_OPERATOR);

    /* vitals */
    CHECK("vitals bare is observer", ROLE1("vitals") == ROLE_OBSERVER);
    CHECK("vitals override is admin", ROLE4("vitals", "override", "temp", "1") == ROLE_ADMIN);
    CHECK("vitals clear is admin", ROLE2("vitals", "clear") == ROLE_ADMIN);

    /* mesh: radio emission is operator; changing the peer table is admin */
    CHECK("mesh mac is observer", ROLE2("mesh", "mac") == ROLE_OBSERVER);
    CHECK("mesh stat is observer", ROLE2("mesh", "stat") == ROLE_OBSERVER);
    CHECK("mesh status is observer", ROLE2("mesh", "status") == ROLE_OBSERVER);
    CHECK("mesh emit is operator", ROLE3("mesh", "emit", "1") == ROLE_OPERATOR);
    CHECK("mesh ping is operator", ROLE2("mesh", "ping") == ROLE_OPERATOR);
    CHECK("mesh query is operator", ROLE3("mesh", "query", "n") == ROLE_OPERATOR);
    CHECK("mesh posture is operator", ROLE4("mesh", "posture", "1", "4") == ROLE_OPERATOR);
    CHECK("mesh peer ls is observer", ROLE3("mesh", "peer", "ls") == ROLE_OBSERVER);
    CHECK("mesh peer add is admin", ROLE4("mesh", "peer", "add", "b") == ROLE_ADMIN);
}

/* --- Failure modes the extraction is meant to make impossible --- */
static void test_fails_closed(void) {
    /* A command absent from the policy table must not land on observer. This
     * is the regression that motivated the extraction: the old code defaulted
     * an unlisted command to the base role of whatever row it sat in. */
    CHECK("unknown command requires admin", ROLE1("no_such_command") == ROLE_ADMIN);
    CHECK("unknown is not known", !shell_command_known("no_such_command"));
    CHECK("NULL command requires admin", shell_required_role(NULL, 0, NULL) == ROLE_ADMIN);

    /* Escalation must not be dodgeable by an unknown sub-command: an
     * unrecognised sub-command falls back to the command's base role, never
     * below it. */
    CHECK("vm bogus subcmd stays >= base", ROLE2("vm", "bogus") >= ROLE1("vm"));
    CHECK("aura bogus subcmd stays admin", ROLE2("aura", "bogus") == ROLE_ADMIN);
    CHECK("bonsai bogus subcmd stays operator", ROLE2("bonsai", "bogus") == ROLE_OPERATOR);

    /* `mesh peer` without `add` is the peer *listing*, not a mutation. Guard
     * the argc bound: reading argv[2] when it does not exist would be a
     * dispatcher-side overread. */
    CHECK("mesh peer alone is observer", ROLE2("mesh", "peer") == ROLE_OBSERVER);

    /* Sub-command matching must be exact, not a prefix. */
    CHECK("vm loadhexx is not admin-by-prefix", ROLE2("vm", "loadhexx") != ROLE_ADMIN);
    CHECK("goonies readx is not operator-by-prefix", ROLE2("goonies", "readx") == ROLE_OBSERVER);

    /* Command matching must be exact too. */
    CHECK("vmm is unknown", !shell_command_known("vmm"));
    CHECK("empty command is unknown", !shell_command_known(""));
}

/* --- Table integrity --- */
static void test_table(void) {
    CHECK("policy table covers 23 commands", shell_policy_count() == 23);

    /* Every listed command resolves to a real role and reports as known. */
    int all_known = 1, all_in_range = 1;
    for (size_t i = 0; i < shell_policy_count(); i++) {
        const char *n = shell_policy_name(i);
        if (!n || !shell_command_known(n)) all_known = 0;
        if (n) {
            uint8_t r = ROLE1(n);
            if (r > ROLE_ADMIN) all_in_range = 0;
        }
    }
    CHECK("every table entry is known", all_known);
    CHECK("every base role is in range", all_in_range);
    CHECK("out-of-range index is NULL", shell_policy_name(shell_policy_count()) == NULL);

    /* Role names are what the SDK and the `denied:` message print. */
    CHECK("role name observer", strcmp(shell_role_names[ROLE_OBSERVER], "observer") == 0);
    CHECK("role name agent", strcmp(shell_role_names[ROLE_AGENT], "agent") == 0);
    CHECK("role name operator", strcmp(shell_role_names[ROLE_OPERATOR], "operator") == 0);
    CHECK("role name admin", strcmp(shell_role_names[ROLE_ADMIN], "admin") == 0);
}

int test_shell_policy(void) {
    printf("[shellpol] ");
    test_base_roles();
    test_escalation();
    test_fails_closed();
    test_table();
    printf("ok\n");
    return s_fail;
}

int test_shell_policy_passed(void) {
    return s_pass;
}
