/** @file shell_policy.c
 * @brief Shell access-control policy. See shell_policy.h.
 */
#include "shell_policy.h"

#include <stdlib.h>
#include <string.h>

const char *const shell_role_names[ROLE_COUNT] = {"observer", "agent", "operator", "admin"};

typedef struct {
    const char *name;
    uint8_t min_role;
} shell_policy_entry_t;

/* Base role per command. Sub-command escalation is layered on top by
 * subcmd_min_role() — a command sits at the *lowest* role any of its
 * sub-commands needs, and the escalation table raises it from there. */
static const shell_policy_entry_t s_policy[] = {
    {"help", ROLE_OBSERVER},    {"status", ROLE_OBSERVER},    {"reboot", ROLE_ADMIN},
    {"sleep", ROLE_ADMIN},      {"goonies", ROLE_OBSERVER},   {"atlas", ROLE_OBSERVER},
    {"led", ROLE_OBSERVER},     {"bonsai", ROLE_OPERATOR},    {"kernel", ROLE_OBSERVER},
    {"loom", ROLE_OBSERVER},    {"tapestry", ROLE_OPERATOR},  {"services", ROLE_OBSERVER},
    {"config", ROLE_OBSERVER},  {"temp", ROLE_OBSERVER},      {"snapshot", ROLE_OBSERVER},
    {"purpose", ROLE_OBSERVER}, {"heartbeat", ROLE_OBSERVER}, {"mesh", ROLE_OBSERVER},
    {"aura", ROLE_ADMIN},       {"vm", ROLE_OBSERVER},        {"telemetry", ROLE_OBSERVER},
    {"vitals", ROLE_OBSERVER},  {"auth", ROLE_OBSERVER},
};

#define POLICY_COUNT (sizeof(s_policy) / sizeof(s_policy[0]))

size_t shell_policy_count(void) {
    return POLICY_COUNT;
}

const char *shell_policy_name(size_t i) {
    return (i < POLICY_COUNT) ? s_policy[i].name : NULL;
}

bool shell_command_known(const char *cmd) {
    if (!cmd) return false;
    for (size_t i = 0; i < POLICY_COUNT; i++) {
        if (strcmp(cmd, s_policy[i].name) == 0) return true;
    }
    return false;
}

/* Fails closed: an unlisted command requires admin rather than defaulting to
 * observer, so a command added to the dispatch table but forgotten here is
 * locked down instead of exposed. */
static uint8_t base_min_role(const char *cmd) {
    for (size_t i = 0; i < POLICY_COUNT; i++) {
        if (strcmp(cmd, s_policy[i].name) == 0) return s_policy[i].min_role;
    }
    return ROLE_ADMIN;
}

/* Sub-command role escalation: some commands have sub-commands that
 * require higher privilege than the base command. */
static uint8_t subcmd_min_role(const char *cmd, int argc, char *argv[]) {
    if (argc < 2) return ROLE_OBSERVER;
    const char *sub = argv[1];
    if (strcmp(cmd, "mesh") == 0) {
        /* mesh emit/ping/posture/query = operator; peer add = admin; peer ls = observer */
        if (strcmp(sub, "emit") == 0 || strcmp(sub, "ping") == 0 || strcmp(sub, "posture") == 0 ||
            strcmp(sub, "query") == 0)
            return ROLE_OPERATOR;
        if (strcmp(sub, "peer") == 0 && argc >= 3 && strcmp(argv[2], "add") == 0) return ROLE_ADMIN;
    } else if (strcmp(cmd, "vm") == 0) {
        /* vm run/stop = operator; vm loadhex = admin; rest = observer */
        if (strcmp(sub, "run") == 0 || strcmp(sub, "stop") == 0) return ROLE_OPERATOR;
        if (strcmp(sub, "loadhex") == 0) return ROLE_ADMIN;
    } else if (strcmp(cmd, "snapshot") == 0) {
        /* snapshot save/load = agent; snapshot clear = admin */
        if (strcmp(sub, "save") == 0 || strcmp(sub, "load") == 0) return ROLE_AGENT;
        if (strcmp(sub, "clear") == 0) return ROLE_ADMIN;
    } else if (strcmp(cmd, "purpose") == 0) {
        /* purpose get = observer; purpose set/clear = agent */
        if (strcmp(sub, "set") == 0 || strcmp(sub, "clear") == 0) return ROLE_AGENT;
    } else if (strcmp(cmd, "config") == 0) {
        /* config get = observer; config set = admin */
        if (strcmp(sub, "set") == 0) return ROLE_ADMIN;
    } else if (strcmp(cmd, "vitals") == 0) {
        /* vitals (display) = observer; vitals override/clear = admin */
        if (strcmp(sub, "override") == 0 || strcmp(sub, "clear") == 0) return ROLE_ADMIN;
    } else if (strcmp(cmd, "telemetry") == 0) {
        /* telemetry (status) = observer; on/off = operator */
        if (strcmp(sub, "on") == 0 || strcmp(sub, "off") == 0) return ROLE_OPERATOR;
    } else if (strcmp(cmd, "led") == 0) {
        /* led on/off = operator; led status = observer (base) */
        if (strcmp(sub, "on") == 0 || strcmp(sub, "off") == 0) return ROLE_OPERATOR;
    } else if (strcmp(cmd, "loom") == 0) {
        /* loom list/fragments = observer; loom load = admin. Weaving a fragment
         * introduces routes and starts a pulse task from operator-supplied
         * bytes — the privilege class of `vm loadhex`. */
        if (strcmp(sub, "load") == 0) return ROLE_ADMIN;
    } else if (strcmp(cmd, "goonies") == 0) {
        /* goonies ls/find = observer; read = operator. `read` dereferences a
         * hardware register, and sampling one is a side effect, so it does not
         * belong to a role whose whole definition is read-only. */
        if (strcmp(sub, "read") == 0) return ROLE_OPERATOR;
    }
    return ROLE_OBSERVER;
}

uint8_t shell_required_role(const char *cmd, int argc, char *argv[]) {
    if (!cmd) return ROLE_ADMIN;
    uint8_t required = base_min_role(cmd);
    uint8_t sub_required = subcmd_min_role(cmd, argc, argv);
    return (sub_required > required) ? sub_required : required;
}

bool shell_parse_trit(const char *s, int8_t *out) {
    if (!s || !out || *s == '\0') return false;
    char *end;
    long v = strtol(s, &end, 10);
    if (end == s || *end != '\0' || v < -1 || v > 1) return false;
    *out = (int8_t)v;
    return true;
}
