/**
 * @file goose_supervisor.c
 * @brief Harmonic Supervisor: Hardened AI Regulation
 */

#include "goose.h"
#include "goose_policy.h"
#include "reflex_hal.h"
#include "reflex_kv.h"
#include "reflex_task.h"
#include "reflex_kernel.h"
#include "reflex_service.h"
#include "reflex_tuning.h"
#include "goose_telemetry.h"
#include "goose_metabolic.h"
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

/* --- Ternary task disposition -------------------------------------------
 *
 * The host scheduler can only say "more urgent" or "less urgent": one integer,
 * two directions. The substrate's actual stance on a task has three values,
 * and the third is the load-bearing one:
 *
 *   +1  ENGAGED   this field serves the declared purpose — give it headroom
 *    0  LATENT    no commitment; the substrate has not decided this matters
 *   -1  WITHHELD  deliberately held back (pain, or every containing holon idle)
 *
 * LATENT is not a synonym for unimportant. A field running with no purpose
 * declared is *undecided*, and that is a different claim from one the
 * supervisor is actively suppressing. A single priority number cannot hold
 * both — it collapses "I have not decided" into "low", which is precisely the
 * collapse this substrate exists to refuse.
 *
 * So disposition is computed first, from purpose, holon lifecycle and pain,
 * and the integer handed to the host RTOS is derived from it. The integer is a
 * lossy projection of the stance onto a binary substrate, never its source.
 * That is an honest description of what this OS is today: ternary-native
 * scheduling policy running on a binary scheduler it does not own. */
#define REFLEX_DISPOSITION_ENGAGED   ((reflex_trit_t) 1)
#define REFLEX_DISPOSITION_LATENT    ((reflex_trit_t) 0)
#define REFLEX_DISPOSITION_WITHHELD  ((reflex_trit_t)-1)

static reflex_trit_t s_field_disposition[MAX_SUPERVISED_FIELDS];
static goose_cell_t *s_kernel_disposition_cell = NULL;

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
            (purpose_active && reflex_domain_match(purpose, holon->domain));
        if (holon->active != should_be_active) {
            holon->active = should_be_active;
            if (purpose_changed) {
                printf("[reflex.kernel] holon %s %s (domain=%s)\n",
                    holon->name, should_be_active ? "activated" : "deactivated",
                    holon->domain[0] ? holon->domain : "*");
            }
        }
    }

    int engaged_count = 0, withheld_count = 0;

    for (size_t f = 0; f < supervised_field_count; f++) {
        goose_field_t *field = supervised_fields[f];

        /* --- Signals the stance is derived from ------------------------- */

        /* Domain match uses dot boundaries: purpose "led" matches
         * "agency.led.intent" but not "misled" or "ledger". */
        bool has_domain_route = false;
        if (purpose_active) {
            for (size_t r = 0; r < field->route_count && !has_domain_route; r++) {
                const char *src = goonies_resolve_name_by_coord(field->routes[r].source_coord);
                const char *snk = goonies_resolve_name_by_coord(field->routes[r].sink_coord);
                if ((src && reflex_domain_match(src, s_last_purpose)) ||
                    (snk && reflex_domain_match(snk, s_last_purpose))) {
                    has_domain_route = true;
                }
            }
        }

        int learned_routes = 0;
        for (size_t r = 0; r < field->route_count; r++) {
            if (field->routes[r].learned_orientation != 0) learned_routes++;
        }

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

        /* --- The ternary stance, decided before any integer exists ------ */

        /* Rule lives in goose_policy.c so the host suite exercises the real
         * decision. The comments explaining each branch live there too. */
        reflex_trit_t disposition = (reflex_trit_t)goose_policy_disposition(
            purpose_active, has_domain_route, in_any_holon, in_active_holon, pained);

        s_field_disposition[f] = disposition;
        if (disposition > 0) engaged_count++;
        else if (disposition < 0) withheld_count++;

        /* --- Projection onto the host scheduler's single integer -------- */

        int priority = REFLEX_PULSE_BASE_PRIORITY;
        if (disposition == REFLEX_DISPOSITION_ENGAGED) {
            priority += REFLEX_PULSE_PURPOSE_BOOST;
        } else if (disposition == REFLEX_DISPOSITION_LATENT && purpose_active) {
            priority += 1;  /* awake under a purpose, but uncommitted */
        }
        /* Hebbian maturity refines a field the substrate is willing to run.
         * It never lifts one that is being withheld — learning is evidence of
         * usefulness, not a licence to override a decision to hold back. */
        if (disposition != REFLEX_DISPOSITION_WITHHELD && learned_routes > 0) {
            priority += REFLEX_PULSE_HEBBIAN_BOOST;
        }
        if (disposition == REFLEX_DISPOSITION_WITHHELD) {
            priority -= REFLEX_PULSE_PAIN_PENALTY;
        }
        if (priority < REFLEX_PULSE_BASE_PRIORITY) priority = REFLEX_PULSE_BASE_PRIORITY;
        if (priority > REFLEX_PULSE_MAX_PRIORITY)  priority = REFLEX_PULSE_MAX_PRIORITY;

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

    /* Publish the aggregate stance as substrate state.
     *
     * Kept as a cell rather than an internal variable on purpose: a consensus
     * that is computed but published nowhere is inert, which is exactly how
     * sys.swarm.posture sat unnoticed. This one is routable and readable. */
    if (!s_kernel_disposition_cell) {
        s_kernel_disposition_cell = goonies_resolve_cell("sys.kernel.disposition");
    }
    if (s_kernel_disposition_cell) {
        /* Precedence, not unanimity.
         *
         * This first required *every* field to be withheld, which made the
         * aggregate's -1 unreachable in practice: sys.autonomy belongs to the
         * `autonomy` holon, whose empty domain keeps it permanently active, so
         * only an autonomously-raised pain signal could ever withhold it. The
         * result was an aggregate that reported "latent" while a field was
         * demonstrably being held back — losing exactly the information the
         * cell exists to carry.
         *
         * Reading it as precedence instead: if anything is engaged the system
         * is serving its purpose; if nothing is engaged but something is being
         * held back, the system's stance is withholding; otherwise it has not
         * decided. */
        reflex_trit_t aggregate =
            (reflex_trit_t)goose_policy_aggregate(engaged_count, withheld_count);
        s_kernel_disposition_cell->state = (int8_t)aggregate;
    }

    if (purpose_changed) {
        printf("[reflex.kernel] policy: purpose=%s fields=%u engaged=%d latent=%d withheld=%d\n",
               purpose_active ? s_last_purpose : "(cleared)",
               (unsigned)supervised_field_count,
               engaged_count,
               (int)supervised_field_count - engaged_count - withheld_count,
               withheld_count);
    }
}

size_t goose_kernel_field_count(void) { return supervised_field_count; }

reflex_trit_t goose_kernel_field_disposition(size_t idx) {
    return (idx < supervised_field_count) ? s_field_disposition[idx] : REFLEX_DISPOSITION_LATENT;
}

const char *goose_kernel_field_name(size_t idx) {
    return (idx < supervised_field_count) ? supervised_fields[idx]->name : NULL;
}

const char *goose_kernel_disposition_name(reflex_trit_t d) {
    return (d > 0) ? "engaged" : (d < 0) ? "withheld" : "latent";
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

    /* Seed the autonomous evaluation signals.
     *
     * These are read in five places across the supervisor and the kernel policy
     * and were never created anywhere. Every goonies_resolve_cell("sys.ai.pain")
     * and ("sys.ai.reward") returned NULL, which made the consequences far
     * larger than a missing cell:
     *
     *   - goose_supervisor_evaluate computed reward_score and pain_triggered
     *     correctly and then published neither, because both writes are guarded
     *     on a cell that did not exist.
     *   - goose_supervisor_learn_sync opens with
     *     `if (!rewarded && !pained) return REFLEX_OK;`, so with both signals
     *     permanently absent it returned immediately every single time.
     *     Hebbian plasticity has therefore never run: learned_orientation and
     *     hebbian_counter never moved off zero, and the snapshots dutifully
     *     persisted those zeros.
     *   - the kernel policy's pain dampening could never fire.
     *
     * So the reward-gated co-activation learning the substrate is built around
     * was inert, while every surface that reports on it looked healthy. Seeded
     * here as SYSTEM_ONLY so the supervisor owns them and the mesh cannot write
     * them. */
    goose_cell_t *reward = goose_fabric_ensure_cell("sys.ai.reward",
                                                    goose_make_coord(0, 0, 6), true);
    if (reward) { reward->type = GOOSE_CELL_SYSTEM_ONLY; reward->state = 0; }
    else REFLEX_LOGW(TAG, "sys.ai.reward not seeded; Hebbian learning stays inert");

    goose_cell_t *pain = goose_fabric_ensure_cell("sys.ai.pain",
                                                  goose_make_coord(0, 0, 7), true);
    if (pain) { pain->type = GOOSE_CELL_SYSTEM_ONLY; pain->state = 0; }
    else REFLEX_LOGW(TAG, "sys.ai.pain not seeded; pain dampening stays inert");

    /* Seed the aggregate stance cell so it reads a neutral 0 from boot rather
     * than failing to resolve until the first policy tick. */
    s_kernel_disposition_cell = goose_fabric_ensure_cell("sys.kernel.disposition",
                                                         goose_make_coord(0, 0, 5), true);
    if (s_kernel_disposition_cell) {
        s_kernel_disposition_cell->type = GOOSE_CELL_SYSTEM_ONLY;
        s_kernel_disposition_cell->state = REFLEX_DISPOSITION_LATENT;
    }
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
    uint32_t version = goose_fabric_get_version();
    for (size_t i = 0; i < field->route_count; i++) {
        goose_route_t *r = &field->routes[i];

        /* This function only *reads* the route cache; internal_process_transitions
         * owns re-resolution, and every supervised field has a pulse task driving
         * it (goose_field_start_pulse is paired with every register_field call
         * site). So a route that is unresolved or stale is simply not evaluable
         * this pulse — defer to the next one rather than read a dangling or
         * recycled cell.
         *
         * Both conditions are reachable. cached_source is NULL until the first
         * successful resolve and again whenever a coord stops resolving; a stale
         * cached_version means the round-robin eviction path in
         * goose_fabric_alloc_cell recycled a slot, so the pointer may now
         * reference a different cell entirely. */
        if (!r->cached_source || !r->cached_sink) continue;
        if (r->cached_version != version) continue;

        if (r->cached_source->type == GOOSE_CELL_HARDWARE_IN) continue;
        reflex_trit_t orient = r->cached_control ? (reflex_trit_t)r->cached_control->state : (r->learned_orientation ? r->learned_orientation : r->orientation);
        reflex_trit_t expected = (reflex_trit_t)((int)r->cached_source->state * (int)orient);
        if (r->cached_sink->state != (int8_t)expected) {
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

static uint8_t s_stuck_ticks[MAX_SUPERVISED_FIELDS];

reflex_err_t goose_supervisor_evaluate(void) {
    const char *purpose = goose_purpose_get_name();
    if (!purpose || !purpose[0]) {
        TELEM_IF(goose_telem_eval(0, false));
        return REFLEX_OK;
    }

    int reward_score = 0;
    bool pain_triggered = false;

    for (size_t f = 0; f < supervised_field_count; f++) {
        goose_field_t *field = supervised_fields[f];
        bool field_stuck = false;
        for (size_t r = 0; r < field->route_count; r++) {
            goose_route_t *route = &field->routes[r];
            if (!route->cached_source || !route->cached_sink) continue;

            const char *src = goonies_resolve_name_by_coord(route->source_coord);
            const char *snk = goonies_resolve_name_by_coord(route->sink_coord);
            bool touches_purpose = (src && reflex_domain_match(src, purpose)) ||
                                   (snk && reflex_domain_match(snk, purpose));
            if (!touches_purpose) continue;

            if (route->cached_source->state != 0 &&
                route->cached_source->state == route->cached_sink->state)
                reward_score++;

            if (route->cached_sink->type == GOOSE_CELL_HARDWARE_OUT) {
                reflex_trit_t orient = route->learned_orientation ? route->learned_orientation : route->orientation;
                int8_t expected = (int8_t)((int)route->cached_source->state * (int)orient);
                if (route->cached_sink->state != expected && expected != 0)
                    field_stuck = true;
            }
        }
        if (field_stuck) {
            if (f < MAX_SUPERVISED_FIELDS && ++s_stuck_ticks[f] >= REFLEX_AUTO_PAIN_STUCK_TICKS)
                pain_triggered = true;
        } else if (f < MAX_SUPERVISED_FIELDS) {
            s_stuck_ticks[f] = 0;
        }
    }

    if (reward_score >= REFLEX_AUTO_REWARD_THRESHOLD) {
        goose_cell_t *rc = goonies_resolve_cell("sys.ai.reward");
        if (rc) rc->state = 1;
    }
    if (pain_triggered) {
        goose_cell_t *pc = goonies_resolve_cell("sys.ai.pain");
        if (pc) pc->state = -1;
    }
    TELEM_IF(goose_telem_eval(reward_score, pain_triggered));
    return REFLEX_OK;
}

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

    /* Deferred telemetry: snapshot route state inside lock, emit outside. */
    typedef struct { char name[16]; int16_t counter; int8_t learned; } telem_snap_t;
    telem_snap_t tsnap[32];
    size_t tsnap_count = 0;

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
                if (tsnap_count < 32) {
                    memcpy(tsnap[tsnap_count].name, route->name, 16);
                    tsnap[tsnap_count].counter = route->hebbian_counter;
                    tsnap[tsnap_count].learned = route->learned_orientation;
                    tsnap_count++;
                }
            } else if (pained) {
                /* Decay toward zero under pain signal. */
                if (route->hebbian_counter > 0) route->hebbian_counter--;
                else if (route->hebbian_counter < 0) route->hebbian_counter++;
                if (tsnap_count < 32) {
                    memcpy(tsnap[tsnap_count].name, route->name, 16);
                    tsnap[tsnap_count].counter = route->hebbian_counter;
                    tsnap[tsnap_count].learned = route->learned_orientation;
                    tsnap_count++;
                }
            }
        }
    }

    if (rewarded) reward_cell->state = 0;
    goose_loom_unlock();

    /* Emit telemetry outside the lock. */
    for (size_t t = 0; t < tsnap_count; t++) {
        TELEM_IF(goose_telem_hebbian(tsnap[t].name, tsnap[t].counter, tsnap[t].learned));
    }
    return REFLEX_OK;
}

/* --- Tapestry Snapshots (Phase 29) ---
 *
 * Persist learned_orientation and hebbian_counter from supervised routes to
 * NVS, one blob per field, keyed "s_<fnv1a(field->name) as 8 hex digits>" —
 * the field name is hashed rather than embedded so the key fits NVS's limit.
 * Route matching on restore is by name (16-byte fixed-width). Format:
 *
 *   [uint16_t version]        SNAP_VERSION; load rejects a mismatch
 *   [uint16_t route_count]    as declared by the field, see truncation note
 *   for each route (up to 16):
 *     [char name[16]]
 *     [int8_t learned_orientation]
 *     [int16_t hebbian_counter]
 *
 * Max per-field blob: 4 + 32*(16+1+2) = 612 bytes. Well within NVS limits.
 *
 * The count is the number of entries that actually follow, never the field's
 * declared route_count. SNAP_MAX_ROUTES matches the largest route_count any
 * field the system can currently build will have, so truncation is unreachable
 * in practice; if it ever happens it is logged rather than silent, and load
 * remains bounded by the blob length regardless.
 */
#define SNAP_ROUTE_ENTRY_SIZE (16 + 1 + 2)
/* Route cap per field. Matches MAX_ROUTES_PER_FRAGMENT in goose_library.c, the
 * largest route_count a LoomScript fragment can produce, so truncation is
 * unreachable for every field the system can currently build. Kept explicit
 * rather than derived because the two limits are independent by design. */
#define SNAP_MAX_ROUTES 32
#define SNAP_VERSION 2  /* v2: header count is entries WRITTEN; cap raised 16 -> 32.
                         * Increment on format change; load rejects mismatches. */
#define SNAP_HEADER_SIZE 4  /* version(2) + entry_count(2) */

reflex_err_t goose_snapshot_save(void) {
    reflex_kv_handle_t h;
    reflex_err_t rc = reflex_kv_open("goose", false, &h);
    if (rc != REFLEX_OK) return rc;

    /* The loom is taken per field, around the serialisation only — never
     * across the NVS write.
     *
     * This used to hold loom_authority for the whole loop, flash writes
     * included. reflex_kv_set_blob goes to NVS, which takes milliseconds, while
     * the lock budget is 300us: every snapshot save starved the 10Hz pulse for
     * the duration. Measured on the dual-core ESP32, LOOM_CONTENTION_FAULTs
     * cluster directly around saves (FAULT SNAP FAULT FAULT SNAP), with a peak
     * hold of 1021us against that 300us budget.
     *
     * Serialise under the lock, write outside it — the same discipline
     * goose_supervisor_learn_sync already uses for telemetry. Atomicity is now
     * per field rather than across all fields, which is the level that actually
     * means anything: routes belong to a field and a field's blob is still
     * captured in one consistent pass. */
    uint32_t saved_fields = 0;
    uint32_t saved_routes = 0;

    for (size_t f = 0; f < supervised_field_count; f++) {
        goose_field_t *field = supervised_fields[f];
        if (field->route_count == 0) continue;

        uint8_t buf[SNAP_HEADER_SIZE + SNAP_MAX_ROUTES * SNAP_ROUTE_ENTRY_SIZE];
        size_t pos = 0;
        if (!goose_loom_try_lock(NULL)) continue;   /* busy — catch this field next time */
        uint16_t version = SNAP_VERSION;
        memcpy(&buf[pos], &version, 2); pos += 2;

        /* Write the number of entries that actually follow, not the field's
         * full route_count. Recording the latter while emitting at most the cap
         * made the header describe a payload that was not there — harmless to
         * read, because load is bounded by the blob length, but a lie that
         * would mislead any other consumer of the format. */
        size_t n = goose_policy_snap_entry_count(field->route_count, SNAP_MAX_ROUTES);
        if (field->route_count > SNAP_MAX_ROUTES) {
            REFLEX_LOGW(TAG, "snapshot: field %s has %u routes, persisting first %u",
                        field->name, (unsigned)field->route_count, (unsigned)SNAP_MAX_ROUTES);
        }
        uint16_t count = (uint16_t)n;
        memcpy(&buf[pos], &count, 2); pos += 2;

        for (size_t r = 0; r < n; r++) {
            goose_route_t *route = &field->routes[r];
            memcpy(&buf[pos], route->name, 16); pos += 16;
            buf[pos++] = (uint8_t)route->learned_orientation;
            int16_t hc = route->hebbian_counter;
            memcpy(&buf[pos], &hc, 2); pos += 2;
        }

        goose_loom_unlock();   /* serialised; the flash write must not hold it */

        char key[16];
        snprintf(key, sizeof(key), "s_%08lx", (unsigned long)goose_fnv1a(field->name));
        rc = reflex_kv_set_blob(h, key, buf, pos);
        if (rc == REFLEX_OK) {
            saved_fields++;
            saved_routes += count;
        }
    }

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

    /* Same split as save: the NVS read happens outside the loom, and only the
     * application of the restored values takes it. */
    uint32_t restored_routes = 0;

    for (size_t f = 0; f < supervised_field_count; f++) {
        goose_field_t *field = supervised_fields[f];
        if (field->route_count == 0) continue;

        char key[16];
        snprintf(key, sizeof(key), "s_%08lx", (unsigned long)goose_fnv1a(field->name));

        uint8_t buf[SNAP_HEADER_SIZE + SNAP_MAX_ROUTES * SNAP_ROUTE_ENTRY_SIZE];
        size_t len = sizeof(buf);
        rc = reflex_kv_get_blob(h, key, buf, &len);
        if (rc != REFLEX_OK || len < SNAP_HEADER_SIZE) continue;

        if (!goose_loom_try_lock(NULL)) continue;   /* busy — retry on the next load */

        uint16_t snap_version;
        memcpy(&snap_version, buf, 2);
        if (snap_version != SNAP_VERSION) {
            REFLEX_LOGW(TAG, "snapshot version mismatch: field=%s got=%u want=%u (skipped)",
                     field->name, (unsigned)snap_version, (unsigned)SNAP_VERSION);
            continue;
        }

        uint16_t snap_count;
        memcpy(&snap_count, &buf[2], 2);
        size_t pos = SNAP_HEADER_SIZE;

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
        goose_loom_unlock();
    }

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

/* --- Self-Expanding Perception (Phase 33): Curiosity Attractor ---
 *
 * The OS is curious when purpose is active. It probes HARDWARE_IN
 * shadow atlas entries by reading the register twice (1 second apart).
 * Registers whose value changed are HOT — something is alive there.
 * Hot registers get paged into the Loom. Hebbian learning decides
 * which are relevant to purpose; eviction forgets the rest.
 *
 * Three layers, each doing one job:
 *   Curiosity (probing)  → finds what's alive
 *   Learning  (Hebbian)  → finds what's relevant
 *   Forgetting (eviction) → discards what's not
 *
 * Pain doesn't trigger exploration — it amplifies it. The OS is
 * always a little curious; it becomes voraciously curious under
 * zero progress. */

#define EXPLORE_PROBE_SLOTS 8

typedef struct {
    size_t atlas_idx;
    uint32_t last_value;
} explore_probe_t;

static size_t s_explore_cursor = 0;
static uint16_t s_explore_pain_ticks = 0;
static uint16_t s_explore_total = 0;
static char s_explore_purpose[16] = {0};
static explore_probe_t s_probes[EXPLORE_PROBE_SLOTS];
static uint8_t s_probe_count = 0;

uint16_t goose_explore_pain_ticks(void) { return s_explore_pain_ticks; }
uint16_t goose_explore_cursor(void) { return (uint16_t)(s_explore_cursor < 0xFFFF ? s_explore_cursor : 0xFFFF); }
uint16_t goose_explore_active(void) { return s_explore_total; }

static void goose_supervisor_explore(void) {
    const char *purpose = goose_purpose_get_name();
    if (!purpose || !purpose[0]) {
        s_explore_pain_ticks = 0;
        s_explore_purpose[0] = '\0';
        s_probe_count = 0;
        return;
    }

    /* Reset exploration state if purpose changed. */
    if (strncmp(s_explore_purpose, purpose, sizeof(s_explore_purpose) - 1) != 0) {
        strncpy(s_explore_purpose, purpose, sizeof(s_explore_purpose) - 1);
        s_explore_purpose[sizeof(s_explore_purpose) - 1] = '\0';
        s_explore_pain_ticks = 0;
        s_explore_total = 0;
        s_probe_count = 0;
    }

    if (s_explore_total >= REFLEX_EXPLORE_MAX_ACTIVE) return;

    /* Heap guard — don't explore if heap is tight. */
    goose_cell_t *heap_cell = goonies_resolve_cell("perception.heap.pressure");
    if (heap_cell && heap_cell->state < 0) return;

    /* Track pain for rate amplification. */
    goose_cell_t *reward_cell = goonies_resolve_cell("sys.ai.reward");
    goose_cell_t *pain_cell = goonies_resolve_cell("sys.ai.pain");
    bool making_progress = (reward_cell && reward_cell->state > 0);
    bool in_pain = (pain_cell && pain_cell->state == -1);
    if (making_progress && !in_pain) {
        s_explore_pain_ticks = 0;
    } else {
        if (s_explore_pain_ticks < 0xFFFF) s_explore_pain_ticks++;
    }

    bool urgent = (s_explore_pain_ticks >= REFLEX_EXPLORE_PAIN_THRESHOLD);
    int8_t metabolic = goose_metabolic_get_state();

    /* Probe count: baseline curiosity, doubled under pain, halved conserving. */
    int probe_slots = REFLEX_EXPLORE_PROBE_COUNT;
    if (urgent) probe_slots *= 2;
    if (metabolic == 0) probe_slots /= 2;
    if (probe_slots < 1) probe_slots = 1;
    if (probe_slots > EXPLORE_PROBE_SLOTS) probe_slots = EXPLORE_PROBE_SLOTS;

    /* --- Phase B: Check pending probes from last pulse --- */
    for (uint8_t i = 0; i < s_probe_count; i++) {
        if (s_explore_total >= REFLEX_EXPLORE_MAX_ACTIVE) break;

        const shadow_node_t *entry = &shadow_map[s_probes[i].atlas_idx];
        /* Re-checked rather than assumed: Phase A is the gate, but this is the
         * second read of the same address and the cost of being wrong is a
         * disturbed peripheral, so the guard is repeated here deliberately. */
        if (goose_fabric_addr_is_sanctuary(entry->addr)) continue;
        volatile uint32_t *reg = (volatile uint32_t *)entry->addr;
        uint32_t now = *reg;

        if (now == s_probes[i].last_value) continue; /* Cold — static. */

        /* HOT — the register changed. Something is alive. Page it in. */
        reflex_tryte9_t dummy;
        if (goonies_resolve(entry->name, &dummy) == REFLEX_OK) continue;

        reflex_tryte9_t coord = goose_make_shadow_coord(entry->f, entry->r, entry->c);
        goose_cell_t *cell = NULL;
        for (int attempt = 0; attempt < 5 && !cell; attempt++) {
            cell = goose_fabric_alloc_cell(entry->name, coord, true);
        }
        if (!cell) continue;

        goose_fabric_set_agency(cell, entry->addr, GOOSE_CELL_HARDWARE_IN);
        s_explore_total++;
        TELEM_IF(goose_telem_explore(entry->name));
    }
    s_probe_count = 0;

    /* --- Phase A: Probe new entries for next pulse --- */
    size_t examined = 0;
    size_t map_count = shadow_map_count;
    size_t max_scan = 2000;

    while (s_probe_count < (uint8_t)probe_slots &&
           examined < map_count && examined < max_scan) {
        const shadow_node_t *entry = &shadow_map[s_explore_cursor];
        s_explore_cursor++;
        if (s_explore_cursor >= map_count) s_explore_cursor = 0;
        examined++;

        if (entry->type != GOOSE_CELL_HARDWARE_IN) continue;
        if (entry->bit_mask != 0xFFFFFFFF) continue;

        /* Sanctuary Guard applies to probing, not just binding. This is the
         * primary gate: a sanctuary address must never be sampled, because the
         * read itself can clear an interrupt status bit or pop a FIFO belonging
         * to a peripheral something else is actively using. */
        if (goose_fabric_addr_is_sanctuary(entry->addr)) continue;

        reflex_tryte9_t dummy;
        if (goonies_resolve(entry->name, &dummy) == REFLEX_OK) continue;

        /* Read the register and store for next-pulse comparison. */
        volatile uint32_t *reg = (volatile uint32_t *)entry->addr;
        s_probes[s_probe_count].atlas_idx = s_explore_cursor > 0 ? s_explore_cursor - 1 : map_count - 1;
        s_probes[s_probe_count].last_value = *reg;
        s_probe_count++;
    }
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
    TELEM_IF(goose_telem_balance(system_balance.state));

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
    
    /* Metabolic regulation — 1Hz vital scan + circuit breaker. */
    static int metabolic_div = 0;
    if (++metabolic_div >= REFLEX_SUPERVISOR_METABOLIC_DIV) {
        goose_metabolic_sync();
        metabolic_div = 0;
    }

    int8_t metabolic = goose_metabolic_get_state();

    /* Autonomic Fabrication — gated: suspended in surviving mode. */
    static int weave_div = 0;
    if (metabolic >= 0 && ++weave_div >= REFLEX_SUPERVISOR_WEAVE_DIV) {
        goose_supervisor_weave_sync();
        weave_div = 0;
    }

    /* Autonomous evaluation — always runs (provides reward/pain signal). */
    static int eval_div = 0;
    if (++eval_div >= REFLEX_SUPERVISOR_EVAL_DIV) {
        goose_supervisor_evaluate();
        eval_div = 0;
    }

    /* Self-Expanding Perception — 1Hz, gated: suspended in surviving.
     * Runs after eval so the pain/reward signals are fresh. */
    static int explore_div = 0;
    if (metabolic >= 0 && ++explore_div >= REFLEX_SUPERVISOR_EXPLORE_DIV) {
        goose_supervisor_explore();
        explore_div = 0;
    }

    /* Hebbian learning — gated: suspended in surviving, halved in conserving.
     * Resource governance (Layer 2): temp stress further doubles divisor. */
    static int learn_div = 0;
    if (metabolic > 0) {
        /* Thriving: normal rate. */
        if (++learn_div >= REFLEX_SUPERVISOR_LEARN_DIV) {
            goose_supervisor_learn_sync();
            learn_div = 0;
        }
    } else if (metabolic == 0) {
        /* Conserving: half rate. */
        if (++learn_div >= REFLEX_SUPERVISOR_LEARN_DIV * 2) {
            goose_supervisor_learn_sync();
            learn_div = 0;
        }
    }
    /* metabolic == -1: learning suspended entirely. */

    /* Swarm sync — gated: suspended in surviving. */
    static int sync_div = 0;
    if (metabolic >= 0 && ++sync_div >= REFLEX_SUPERVISOR_SYNC_DIV) {
        goose_supervisor_swarm_sync();
        sync_div = 0;
    }

    /* Staleness check — always runs (safety: detect dead peers). */
    static int stale_div = 0;
    if (++stale_div >= REFLEX_SUPERVISOR_STALE_DIV) {
        goose_mmio_sync_staleness_check();
        stale_div = 0;
    }

    /* Snapshot save — gated: suspended in surviving. */
    static int snap_div = 0;
    if (metabolic >= 0 && ++snap_div >= REFLEX_SUPERVISOR_SNAP_DIV) {
        goose_snapshot_save();
        snap_div = 0;
    }

    /* Watchdog — always runs (safety). */
    static int watchdog_div = 0;
    if (++watchdog_div >= REFLEX_SUPERVISOR_WATCHDOG_DIV) {
        reflex_service_watchdog_tick();
        watchdog_div = 0;
    }

    /* Discovery heartbeat — resource governance (Layer 2):
     * Mesh isolation INCREASES discovery frequency (inverted logic).
     * Mesh health reads perception.mesh.health directly. */
    static int discover_div = 0;
    {
        goose_cell_t *mesh_cell = goonies_resolve_cell("perception.mesh.health");
        int discover_interval = REFLEX_SUPERVISOR_DISCOVER_DIV;
        if (mesh_cell && mesh_cell->state == -1) discover_interval /= 2;  /* isolated: hunt faster */
        else if (mesh_cell && mesh_cell->state == 1) discover_interval *= 2;  /* connected: relax */
        if (discover_interval < 1) discover_interval = 1;
        if (++discover_div >= discover_interval) {
            goose_atmosphere_emit_discover();
            discover_div = 0;
        }
    }
    return REFLEX_OK;
}
