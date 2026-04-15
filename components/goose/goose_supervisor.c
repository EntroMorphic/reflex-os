/**
 * @file goose_supervisor.c
 * @brief Harmonic Supervisor: Hardened AI Regulation
 */

#include "goose.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_random.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "GOOSE_SUPERVISOR";

#define MAX_SUPERVISED_FIELDS 32
static goose_field_t *supervised_fields[MAX_SUPERVISED_FIELDS];
static size_t supervised_field_count = 0;

static portMUX_TYPE supervisor_mux = portMUX_INITIALIZER_UNLOCKED;

static goose_cell_t system_balance = { .state = REFLEX_TRIT_POS };

esp_err_t goose_supervisor_init(void) {
    ESP_LOGI(TAG, "Initializing Harmonic Supervisor...");
    return ESP_OK;
}

esp_err_t goose_supervisor_register_field(goose_field_t *field) {
    taskENTER_CRITICAL(&supervisor_mux);
    if (supervised_field_count >= MAX_SUPERVISED_FIELDS) {
        taskEXIT_CRITICAL(&supervisor_mux);
        return ESP_ERR_NO_MEM;
    }
    for(size_t i=0; i<supervised_field_count; i++) {
        if (supervised_fields[i] == field) {
            taskEXIT_CRITICAL(&supervisor_mux);
            return ESP_OK;
        }
    }
    supervised_fields[supervised_field_count++] = field;
    taskEXIT_CRITICAL(&supervisor_mux);
    return ESP_OK;
}

esp_err_t goose_supervisor_check_equilibrium(goose_field_t *field) {
    if (!field) return ESP_ERR_INVALID_ARG;
    for (size_t i = 0; i < field->route_count; i++) {
        goose_route_t *r = &field->routes[i];
        if (r->cached_source && r->cached_source->type == GOOSE_CELL_HARDWARE_IN) continue;
        reflex_trit_t orient = r->cached_control ? (reflex_trit_t)r->cached_control->state : (r->learned_orientation ? r->learned_orientation : r->orientation);
        reflex_trit_t expected = (reflex_trit_t)((int)r->cached_source->state * (int)orient);
        if (r->cached_sink && r->cached_sink->state != (int8_t)expected) {
            if (r->cached_sink->type == GOOSE_CELL_HARDWARE_OUT) {
                system_balance.state = REFLEX_TRIT_ZERO;
                return ESP_FAIL; 
            }
        }
    }
    system_balance.state = REFLEX_TRIT_POS;
    return ESP_OK;
}

esp_err_t goose_supervisor_rebalance(goose_field_t *field) {
    int field_rebalance_limit = 0;
    for (size_t i = 0; i < field->route_count; i++) {
        goose_route_t *r = &field->routes[i];
        if (field_rebalance_limit++ > 10) {
            system_balance.state = REFLEX_TRIT_NEG;
            return ESP_ERR_INVALID_STATE;
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

extern int32_t swarm_accumulator;

esp_err_t goose_supervisor_swarm_sync(void) {
    if (swarm_accumulator > 0) swarm_accumulator--;
    else if (swarm_accumulator < -1) swarm_accumulator++;
    return ESP_OK;
}

/* Reward-gated co-activation Hebbian plasticity.
 *
 * When `sys.ai.reward` is positive, each supervised route's hebbian_counter
 * moves by +1 if source and sink currently fire the same sign (co-active),
 * -1 if they disagree. When the counter crosses +/-HEBBIAN_COMMIT_THRESHOLD,
 * the sign is committed into learned_orientation and the counter resets.
 *
 * When `sys.ai.pain` is negative, all counters decay toward zero, which
 * represents unlearning without catastrophically erasing state.
 *
 * This is the "fire-together, wire-together" rule in its cheapest honest
 * form: accumulate co-activation under reward, decay under pain, commit at
 * a threshold. Upgradable to STDP-lite or gradient methods later without
 * changing the per-route field layout. */
#define HEBBIAN_COMMIT_THRESHOLD 8
#define HEBBIAN_COUNTER_MAX      (HEBBIAN_COMMIT_THRESHOLD * 2)

esp_err_t goose_supervisor_learn_sync(void) {
    goose_cell_t *pain_cell = goonies_resolve_cell("sys.ai.pain");
    goose_cell_t *reward_cell = goonies_resolve_cell("sys.ai.reward");

    bool rewarded = (reward_cell && reward_cell->state == 1);
    bool pained   = (pain_cell && pain_cell->state == -1);
    if (!rewarded && !pained) return ESP_OK;

    /* Serialize against pulse readers. We're mutating route->hebbian_counter
     * and route->learned_orientation, both of which the pulse path reads.
     * Loom try_lock is non-blocking with a timeout; if contended, we skip
     * this plasticity pass and the next supervisor tick will retry. */
    if (!goose_loom_try_lock(NULL)) return ESP_OK;

    for (size_t f = 0; f < supervised_field_count; f++) {
        goose_field_t *field = supervised_fields[f];
        for (size_t r = 0; r < field->route_count; r++) {
            goose_route_t *route = &field->routes[r];
            if (!route->cached_source || !route->cached_sink) continue;

            if (rewarded) {
                /* Co-activation increment: same sign => +1, opposite => -1,
                 * zero state on either end => no change (no learning signal). */
                int s = route->cached_source->state;
                int k = route->cached_sink->state;
                if (s == 0 || k == 0) continue;
                int delta = (s == k) ? 1 : -1;
                int32_t next = (int32_t)route->hebbian_counter + delta;
                if (next >  HEBBIAN_COUNTER_MAX) next =  HEBBIAN_COUNTER_MAX;
                if (next < -HEBBIAN_COUNTER_MAX) next = -HEBBIAN_COUNTER_MAX;
                route->hebbian_counter = (int16_t)next;

                if (route->hebbian_counter >= HEBBIAN_COMMIT_THRESHOLD) {
                    route->learned_orientation = REFLEX_TRIT_POS;
                    route->hebbian_counter = 0;
                } else if (route->hebbian_counter <= -HEBBIAN_COMMIT_THRESHOLD) {
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
    return ESP_OK;
}

esp_err_t goose_supervisor_pulse(void) {
    taskENTER_CRITICAL(&supervisor_mux);
    size_t count = supervised_field_count;
    taskEXIT_CRITICAL(&supervisor_mux);

    for (size_t i = 0; i < count; i++) {
        if (goose_supervisor_check_equilibrium(supervised_fields[i]) != ESP_OK) {
            goose_supervisor_rebalance(supervised_fields[i]);
        }
    }

    // Mirror HP intent into LP coprocessor for Coherent Heartbeat.
    goose_lp_heartbeat_sync();
    
    // Autonomic Fabrication pass at 1Hz (offset by 2)
    static int weave_div = 0;
    if (weave_div++ >= 10) {
        extern esp_err_t goose_supervisor_weave_sync(void);
        goose_supervisor_weave_sync();
        weave_div = 0;
    }

    static int learn_div = 0;
    if (learn_div++ >= 10) {
        goose_supervisor_learn_sync();
        learn_div = 0;
    }

    static int sync_div = 0;
    if (sync_div++ >= 10) {
        goose_supervisor_swarm_sync();
        sync_div = 0;
    }
    return ESP_OK;
}
