/**
 * @file goose_supervisor.c
 * @brief Harmonic Supervisor: Hardened AI Regulation
 */

#include "goose.h"
#include "reflex_hal.h"
#include "reflex_kv.h"
#include "reflex_task.h"
#include "reflex_kernel.h"
#include "reflex_tuning.h"
#include <stdio.h>
#include <string.h>

#define TAG "GOOSE_SUPERVISOR"

#define MAX_SUPERVISED_FIELDS 32
static goose_field_t *supervised_fields[MAX_SUPERVISED_FIELDS];
static size_t supervised_field_count = 0;

static reflex_mutex_t supervisor_mux;
static bool supervisor_mux_initialized = false;

static goose_cell_t system_balance = { .state = REFLEX_TRIT_POS };

/* --- Kernel scheduling policy (Items 1-3) --- */

/* Task priority constants now live in reflex_tuning.h:
 * REFLEX_PULSE_BASE_PRIORITY, REFLEX_PULSE_PURPOSE_BOOST,
 * REFLEX_PULSE_HEBBIAN_BOOST, REFLEX_PULSE_PAIN_PENALTY,
 * REFLEX_PULSE_MAX_PRIORITY */

/* Holon: a named group of fields managed as a lifecycle unit.
 * The supervisor activates/deactivates holons based on purpose. */
#define MAX_HOLONS 8
#define MAX_HOLON_FIELDS 4

typedef struct {
    char name[16];
    char domain[16];
    goose_field_t *fields[MAX_HOLON_FIELDS];
    size_t field_count;
    bool active;
} reflex_holon_t;

static reflex_holon_t s_holons[MAX_HOLONS];
static size_t s_holon_count = 0;
static char s_last_purpose[16] = {0};

__attribute__((weak))
void reflex_kernel_set_policy(reflex_kernel_policy_fn fn) { (void)fn; }

static bool reflex_domain_match(const char *name, const char *domain) {
    size_t dlen = strlen(domain);
    const char *p = name;
    while ((p = strstr(p, domain)) != NULL) {
        bool left_ok = (p == name || *(p - 1) == '.');
        bool right_ok = (p[dlen] == '\0' || p[dlen] == '.');
        if (left_ok && right_ok) return true;
        p++;
    }
    return false;
}

static void goose_kernel_policy_tick(uint32_t tick) {
    (void)tick;
    const char *purpose = goose_purpose_get_name();
    bool purpose_active = (purpose && purpose[0]);
    bool purpose_changed = false;

    if (purpose_active) {
        if (strncmp(s_last_purpose, purpose, sizeof(s_last_purpose) - 1) != 0) {
            strncpy(s_last_purpose, purpose, sizeof(s_last_purpose) - 1);
            s_last_purpose[sizeof(s_last_purpose) - 1] = '\0';
            purpose_changed = true;
        }
    } else if (s_last_purpose[0]) {
        s_last_purpose[0] = '\0';
        purpose_changed = true;
    }

    /* Pain signal suppresses all non-essential task priorities */
    goose_cell_t *pain_cell = goonies_resolve_cell("sys.ai.pain");
    bool pained = (pain_cell && pain_cell->state == -1);

    /* Holon lifecycle: activate holons matching purpose, deactivate others */
    for (size_t h = 0; h < s_holon_count; h++) {
        reflex_holon_t *holon = &s_holons[h];
        bool should_be_active = !holon->domain[0] ||
            (purpose_active && strstr(purpose, holon->domain));
        if (holon->active != should_be_active) {
            holon->active = should_be_active;
            if (purpose_changed) {
                printf("[reflex.kernel] holon %s %s (domain=%s)\n",
                    holon->name, should_be_active ? "activated" : "deactivated",
                    holon->domain[0] ? holon->domain : "*");
            }
        }
    }

    for (size_t f = 0; f < supervised_field_count; f++) {
        goose_field_t *field = supervised_fields[f];
        int priority = REFLEX_PULSE_BASE_PRIORITY;

        /* Item 1: Purpose boost — domain-specific. Only boost fields
         * whose routes touch cells in the active purpose domain.
         * Domain matching uses dot-boundary: purpose "led" matches
         * "agency.led.intent" but not "misled" or "ledger". */
        if (purpose_active) {
            bool has_domain_route = false;
            for (size_t r = 0; r < field->route_count && !has_domain_route; r++) {
                const char *src = goonies_resolve_name_by_coord(field->routes[r].source_coord);
                const char *snk = goonies_resolve_name_by_coord(field->routes[r].sink_coord);
                if ((src && reflex_domain_match(src, s_last_purpose)) ||
                    (snk && reflex_domain_match(snk, s_last_purpose))) {
                    has_domain_route = true;
                }
            }
            if (has_domain_route) {
                priority += REFLEX_PULSE_PURPOSE_BOOST;
            } else {
                priority += 1;
            }
        }

        /* Item 2: Hebbian maturity — fields with learned routes are stable */
        int learned_routes = 0;
        for (size_t r = 0; r < field->route_count; r++) {
            if (field->routes[r].learned_orientation != 0) learned_routes++;
        }
        if (learned_routes > 0) priority += REFLEX_PULSE_HEBBIAN_BOOST;

        /* Item 3: Holon membership — field keeps boost if ANY holon is active.
         * Only suppress to base if ALL containing holons are inactive. */
        bool in_any_holon = false;
        bool in_active_holon = false;
        for (size_t h = 0; h < s_holon_count; h++) {
            for (size_t hf = 0; hf < s_holons[h].field_count; hf++) {
                if (s_holons[h].fields[hf] == field) {
                    in_any_holon = true;
                    if (s_holons[h].active) in_active_holon = true;
                }
            }
        }
        if (in_any_holon && !in_active_holon) priority = REFLEX_PULSE_BASE_PRIORITY;

        /* Pain dampening */
        if (pained && priority > REFLEX_PULSE_BASE_PRIORITY) {
            priority -= REFLEX_PULSE_PAIN_PENALTY;
            if (priority < REFLEX_PULSE_BASE_PRIORITY) priority = REFLEX_PULSE_BASE_PRIORITY;
        }

        if (priority > REFLEX_PULSE_MAX_PRIORITY) priority = REFLEX_PULSE_MAX_PRIORITY;

        char task_name[16];
        snprintf(task_name, sizeof(task_name), "p_%.13s", field->name);
        reflex_task_handle_t h = reflex_task_get_by_name(task_name);
        if (h) {
            int current = reflex_task_get_priority(h);
            if (current != priority) {
                reflex_task_set_priority(h, priority);
            }
        }
    }

    if (purpose_changed) {
        printf("[reflex.kernel] policy: purpose=%s fields=%u\n",
               purpose_active ? s_last_purpose : "(cleared)",
               (unsigned)supervised_field_count);
    }
}

reflex_err_t reflex_holon_create(const char *name, const char *domain) {
    if (s_holon_count >= MAX_HOLONS) return REFLEX_ERR_NO_MEM;
    reflex_holon_t *h = &s_holons[s_holon_count++];
    memset(h, 0, sizeof(*h));
    strncpy(h->name, name, sizeof(h->name) - 1);
    if (domain) strncpy(h->domain, domain, sizeof(h->domain) - 1);
    h->active = true;
    return REFLEX_OK;
}

reflex_err_t reflex_holon_add_field(const char *holon_name, goose_field_t *field) {
    if (!holon_name || !field) return REFLEX_ERR_INVALID_ARG;
    for (size_t i = 0; i < s_holon_count; i++) {
        if (strcmp(s_holons[i].name, holon_name) == 0) {
            reflex_holon_t *h = &s_holons[i];
            if (h->field_count >= MAX_HOLON_FIELDS) return REFLEX_ERR_NO_MEM;
            h->fields[h->field_count++] = field;
            return REFLEX_OK;
        }
    }
    return REFLEX_ERR_NOT_FOUND;
}

reflex_err_t goose_supervisor_init(void) {
    supervisor_mux = reflex_mutex_init();
    supervisor_mux_initialized = true;
    REFLEX_LOGI(TAG, "Initializing Harmonic Supervisor...");
    reflex_kernel_set_policy(goose_kernel_policy_tick);
    reflex_holon_create("autonomy", "");
    reflex_holon_create("comm", "mesh");
    reflex_holon_create("agency", "led");
    return REFLEX_OK;
}

reflex_err_t goose_supervisor_register_field(goose_field_t *field) {
    if (!field) return REFLEX_ERR_INVALID_ARG;
    reflex_critical_enter(&supervisor_mux);
    if (supervised_field_count >= MAX_SUPERVISED_FIELDS) {
        reflex_critical_exit(&supervisor_mux);
        return REFLEX_ERR_NO_MEM;
    }
    for(size_t i=0; i<supervised_field_count; i++) {
        if (supervised_fields[i] == field) {
            reflex_critical_exit(&supervisor_mux);
            return REFLEX_OK;
        }
    }
    supervised_fields[supervised_field_count++] = field;
    reflex_critical_exit(&supervisor_mux);
    return REFLEX_OK;
}

reflex_err_t goose_supervisor_check_equilibrium(goose_field_t *field) {
    if (!field) return REFLEX_ERR_INVALID_ARG;
    for (size_t i = 0; i < field->route_count; i++) {
        goose_route_t *r = &field->routes[i];
        if (r->cached_source && r->cached_source->type == GOOSE_CELL_HARDWARE_IN) continue;
        reflex_trit_t orient = r->cached_control ? (reflex_trit_t)r->cached_control->state : (r->learned_orientation ? r->learned_orientation : r->orientation);
        reflex_trit_t expected = (reflex_trit_t)((int)r->cached_source->state * (int)orient);
        if (r->cached_sink && r->cached_sink->state != (int8_t)expected) {
            if (r->cached_sink->type == GOOSE_CELL_HARDWARE_OUT) {
                system_balance.state = REFLEX_TRIT_ZERO;
                return REFLEX_FAIL; 
            }
        }
    }
    system_balance.state = REFLEX_TRIT_POS;
    return REFLEX_OK;
}

reflex_err_t goose_supervisor_rebalance(goose_field_t *field) {
    int field_rebalance_limit = 0;
    for (size_t i = 0; i < field->route_count; i++) {
        goose_route_t *r = &field->routes[i];
        if (field_rebalance_limit++ > REFLEX_MAX_REBALANCE_ITERATIONS) {
            system_balance.state = REFLEX_TRIT_NEG;
            return REFLEX_ERR_INVALID_STATE;
        }
        if (r->cached_source && r->cached_sink &&
            r->cached_source->type == GOOSE_CELL_INTENT && 
            r->cached_sink->type == GOOSE_CELL_HARDWARE_OUT &&
            r->orientation == REFLEX_TRIT_ZERO) {
            r->orientation = REFLEX_TRIT_POS;
        }
    }
    return goose_process_transitions(field);
}

reflex_err_t goose_supervisor_swarm_sync(void) {
    if (swarm_accumulator > 0) swarm_accumulator--;
    else if (swarm_accumulator < -1) swarm_accumulator++;
    return REFLEX_OK;
}

/* Reward-gated co-activation Hebbian plasticity.
 *
 * When `sys.ai.reward` is positive, each supervised route's hebbian_counter
 * moves by +1 if source and sink currently fire the same sign (co-active),
 * -1 if they disagree. When the counter crosses +/-REFLEX_HEBBIAN_COMMIT_THRESHOLD,
 * the sign is committed into learned_orientation and the counter resets.
 *
 * When `sys.ai.pain` is negative, all counters decay toward zero, which
 * represents unlearning without catastrophically erasing state.
 *
 * This is the "fire-together, wire-together" rule in its cheapest honest
 * form: accumulate co-activation under reward, decay under pain, commit at
 * a threshold. Upgradable to STDP-lite or gradient methods later without
 * changing the per-route field layout. */
/* Hebbian constants now live in reflex_tuning.h:
 * REFLEX_HEBBIAN_COMMIT_THRESHOLD, REFLEX_HEBBIAN_COUNTER_MAX */

reflex_err_t goose_supervisor_learn_sync(void) {
    goose_cell_t *pain_cell = goonies_resolve_cell("sys.ai.pain");
    goose_cell_t *reward_cell = goonies_resolve_cell("sys.ai.reward");

    bool rewarded = (reward_cell && reward_cell->state == 1);
    bool pained   = (pain_cell && pain_cell->state == -1);
    if (!rewarded && !pained) return REFLEX_OK;

    /* Purpose-aware amplification. When the user has declared an active
     * purpose (GOOSE_CELL_PURPOSE, set via `purpose set`), Hebbian
     * reward increments are doubled. This is the simplest concrete
     * expression of "awareness improves the substrate's ability to serve
     * the user's evolving purpose" — the OS learns faster when the user
     * tells it what they're trying to do. */
    goose_cell_t *purpose_cell = goonies_resolve_cell("sys.purpose");
    int purpose_multiplier = (purpose_cell && purpose_cell->state != 0) ? 2 : 1;

    if (!goose_loom_try_lock(NULL)) return REFLEX_OK;

    for (size_t f = 0; f < supervised_field_count; f++) {
        goose_field_t *field = supervised_fields[f];
        for (size_t r = 0; r < field->route_count; r++) {
            goose_route_t *route = &field->routes[r];
            if (!route->cached_source || !route->cached_sink) continue;

            if (rewarded) {
                int s = route->cached_source->state;
                int k = route->cached_sink->state;
                if (s == 0 || k == 0) continue;
                int delta = ((s == k) ? 1 : -1) * purpose_multiplier;
                int32_t next = (int32_t)route->hebbian_counter + delta;
                if (next >  REFLEX_HEBBIAN_COUNTER_MAX) next =  REFLEX_HEBBIAN_COUNTER_MAX;
                if (next < -REFLEX_HEBBIAN_COUNTER_MAX) next = -REFLEX_HEBBIAN_COUNTER_MAX;
                route->hebbian_counter = (int16_t)next;

                if (route->hebbian_counter >= REFLEX_HEBBIAN_COMMIT_THRESHOLD) {
                    route->learned_orientation = REFLEX_TRIT_POS;
                    route->hebbian_counter = 0;
                } else if (route->hebbian_counter <= -REFLEX_HEBBIAN_COMMIT_THRESHOLD) {
                    route->learned_orientation = REFLEX_TRIT_NEG;
                    route->hebbian_counter = 0;
                }
            } else if (pained) {
                /* Decay toward zero under pain signal. */
                if (route->hebbian_counter > 0) route->hebbian_counter--;
                else if (route->hebbian_counter < 0) route->hebbian_counter++;
            }
        }
    }

    if (rewarded) reward_cell->state = 0;
    goose_loom_unlock();
    return REFLEX_OK;
}

/* --- Tapestry Snapshots (Phase 29) ---
 *
 * Persist learned_orientation and hebbian_counter from supervised routes
 * to NVS under "goose/snap_<fieldname>". One blob per field. Route
 * matching on restore is by name (16-byte fixed-width). The format is:
 *
 *   [uint16_t route_count]
 *   for each route:
 *     [char name[16]]
 *     [int8_t learned_orientation]
 *     [int16_t hebbian_counter]
 *
 * Max per-field blob: 2 + 16*(16+1+2) = 306 bytes. Well within NVS
 * blob limits.
 */
#define SNAP_ROUTE_ENTRY_SIZE (16 + 1 + 2)

reflex_err_t goose_snapshot_save(void) {
    reflex_kv_handle_t h;
    reflex_err_t rc = reflex_kv_open("goose", false, &h);
    if (rc != REFLEX_OK) return rc;

    /* Hold loom_authority while reading route plasticity so we get a
     * consistent snapshot — learn_sync writes these fields under the
     * same lock. If contended, the save is skipped (try again later). */
    if (!goose_loom_try_lock(NULL)) { reflex_kv_close(h); return REFLEX_ERR_TIMEOUT; }

    uint32_t saved_fields = 0;
    uint32_t saved_routes = 0;

    for (size_t f = 0; f < supervised_field_count; f++) {
        goose_field_t *field = supervised_fields[f];
        if (field->route_count == 0) continue;

        uint8_t buf[2 + 16 * SNAP_ROUTE_ENTRY_SIZE];
        size_t pos = 0;
        uint16_t count = (uint16_t)field->route_count;
        memcpy(&buf[pos], &count, 2); pos += 2;

        for (size_t r = 0; r < field->route_count && r < 16; r++) {
            goose_route_t *route = &field->routes[r];
            memcpy(&buf[pos], route->name, 16); pos += 16;
            buf[pos++] = (uint8_t)route->learned_orientation;
            int16_t hc = route->hebbian_counter;
            memcpy(&buf[pos], &hc, 2); pos += 2;
        }

        char key[16];
        snprintf(key, sizeof(key), "s_%08lx", (unsigned long)goose_fnv1a(field->name));
        rc = reflex_kv_set_blob(h, key, buf, pos);
        if (rc == REFLEX_OK) {
            saved_fields++;
            saved_routes += (count > 16 ? 16 : count);
        }
    }

    goose_loom_unlock();
    reflex_kv_commit(h);
    reflex_kv_close(h);
    REFLEX_LOGI(TAG, "snapshot saved: %lu fields, %lu routes",
             (unsigned long)saved_fields, (unsigned long)saved_routes);
    return REFLEX_OK;
}

reflex_err_t goose_snapshot_load(void) {
    reflex_kv_handle_t h;
    reflex_err_t rc = reflex_kv_open("goose", true, &h);
    if (rc != REFLEX_OK) return rc;

    if (!goose_loom_try_lock(NULL)) { reflex_kv_close(h); return REFLEX_ERR_TIMEOUT; }

    uint32_t restored_routes = 0;

    for (size_t f = 0; f < supervised_field_count; f++) {
        goose_field_t *field = supervised_fields[f];
        if (field->route_count == 0) continue;

        char key[16];
        snprintf(key, sizeof(key), "s_%08lx", (unsigned long)goose_fnv1a(field->name));

        uint8_t buf[2 + 16 * SNAP_ROUTE_ENTRY_SIZE];
        size_t len = sizeof(buf);
        rc = reflex_kv_get_blob(h, key, buf, &len);
        if (rc != REFLEX_OK || len < 2) continue;

        uint16_t snap_count;
        memcpy(&snap_count, buf, 2);
        size_t pos = 2;

        for (uint16_t s = 0; s < snap_count && pos + SNAP_ROUTE_ENTRY_SIZE <= len; s++) {
            char snap_name[16];
            memcpy(snap_name, &buf[pos], 16); pos += 16;
            int8_t snap_orient = (int8_t)buf[pos++];
            int16_t snap_hc;
            memcpy(&snap_hc, &buf[pos], 2); pos += 2;

            for (size_t r = 0; r < field->route_count; r++) {
                if (memcmp(field->routes[r].name, snap_name, 16) == 0) {
                    field->routes[r].learned_orientation = snap_orient;
                    field->routes[r].hebbian_counter = snap_hc;
                    restored_routes++;
                    break;
                }
            }
        }
    }

    goose_loom_unlock();
    reflex_kv_close(h);
    if (restored_routes > 0) {
        REFLEX_LOGI(TAG, "snapshot loaded: %lu routes restored",
                 (unsigned long)restored_routes);
    }
    return REFLEX_OK;
}

reflex_err_t goose_snapshot_clear(void) {
    reflex_kv_handle_t h;
    reflex_err_t rc = reflex_kv_open("goose", false, &h);
    if (rc != REFLEX_OK) return rc;
    for (size_t f = 0; f < supervised_field_count; f++) {
        char key[16];
        snprintf(key, sizeof(key), "s_%08lx", (unsigned long)goose_fnv1a(supervised_fields[f]->name));
        reflex_kv_erase(h, key);
    }
    reflex_kv_commit(h);
    reflex_kv_close(h);
    REFLEX_LOGI(TAG, "snapshot cleared");
    return REFLEX_OK;
}

reflex_err_t goose_supervisor_pulse(void) {
    reflex_critical_enter(&supervisor_mux);
    size_t count = supervised_field_count;
    reflex_critical_exit(&supervisor_mux);

    for (size_t i = 0; i < count; i++) {
        if (goose_supervisor_check_equilibrium(supervised_fields[i]) != REFLEX_OK) {
            goose_supervisor_rebalance(supervised_fields[i]);
        }
    }

    // Mirror HP intent into LP coprocessor for Coherent Heartbeat.
    goose_lp_heartbeat_sync();

    /* LP core liveness monitor. If lp_pulse_count stops advancing for
     * REFLEX_REPLAY_WINDOW_US while HP is otherwise healthy, log once per
     * stall so the stall is visible in the serial log. Observation only;
     * no auto-restart. */
    static uint32_t lp_last_count = 0;
    static uint64_t lp_last_advance_us = 0;
    static bool lp_stall_warned = false;
    uint32_t cur = goose_lp_heartbeat_count();
    uint64_t now_us = reflex_hal_time_us();
    if (cur != lp_last_count) {
        lp_last_count = cur;
        lp_last_advance_us = now_us;
        lp_stall_warned = false;
    } else if (cur > 0 && lp_last_advance_us > 0 &&
               (now_us - lp_last_advance_us) > REFLEX_REPLAY_WINDOW_US &&
               !lp_stall_warned) {
        REFLEX_LOGW(TAG, "LP_HEARTBEAT_STALLED count=%lu stalled_us=%llu",
                 (unsigned long)cur,
                 (unsigned long long)(now_us - lp_last_advance_us));
        lp_stall_warned = true;
    }
    
    // Autonomic Fabrication pass at 1Hz (offset by 2)
    static int weave_div = 0;
    if (weave_div++ >= REFLEX_SUPERVISOR_WEAVE_DIV) {
        goose_supervisor_weave_sync();
        weave_div = 0;
    }

    static int learn_div = 0;
    if (learn_div++ >= REFLEX_SUPERVISOR_LEARN_DIV) {
        goose_supervisor_learn_sync();
        learn_div = 0;
    }

    static int sync_div = 0;
    if (sync_div++ >= REFLEX_SUPERVISOR_SYNC_DIV) {
        goose_supervisor_swarm_sync();
        sync_div = 0;
    }

    static int stale_div = 0;
    if (stale_div++ >= REFLEX_SUPERVISOR_STALE_DIV) {
        goose_mmio_sync_staleness_check();
        stale_div = 0;
    }

    static int snap_div = 0;
    if (snap_div++ >= REFLEX_SUPERVISOR_SNAP_DIV) {
        goose_snapshot_save();
        snap_div = 0;
    }
    return REFLEX_OK;
}
