/**
 * @file goose_supervisor.c
 * @brief Harmonic Supervisor: Hardened AI Regulation
 */

#include "goose.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_random.h"
#include "nvs.h"
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

    /* Purpose-aware amplification. When the user has declared an active
     * purpose (GOOSE_CELL_PURPOSE, set via `purpose set`), Hebbian
     * reward increments are doubled. This is the simplest concrete
     * expression of "awareness improves the substrate's ability to serve
     * the user's evolving purpose" — the OS learns faster when the user
     * tells it what they're trying to do. */
    goose_cell_t *purpose_cell = goonies_resolve_cell("sys.purpose");
    int purpose_multiplier = (purpose_cell && purpose_cell->state != 0) ? 2 : 1;

    if (!goose_loom_try_lock(NULL)) return ESP_OK;

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

esp_err_t goose_snapshot_save(void) {
    nvs_handle_t h;
    esp_err_t rc = nvs_open("goose", NVS_READWRITE, &h);
    if (rc != ESP_OK) return rc;

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
        snprintf(key, sizeof(key), "snap_%.10s", field->name);
        rc = nvs_set_blob(h, key, buf, pos);
        if (rc == ESP_OK) {
            saved_fields++;
            saved_routes += (count > 16 ? 16 : count);
        }
    }

    nvs_commit(h);
    nvs_close(h);
    ESP_LOGI(TAG, "snapshot saved: %lu fields, %lu routes",
             (unsigned long)saved_fields, (unsigned long)saved_routes);
    return ESP_OK;
}

esp_err_t goose_snapshot_load(void) {
    nvs_handle_t h;
    esp_err_t rc = nvs_open("goose", NVS_READONLY, &h);
    if (rc != ESP_OK) return rc;

    uint32_t restored_routes = 0;

    for (size_t f = 0; f < supervised_field_count; f++) {
        goose_field_t *field = supervised_fields[f];
        if (field->route_count == 0) continue;

        char key[16];
        snprintf(key, sizeof(key), "snap_%.10s", field->name);

        uint8_t buf[2 + 16 * SNAP_ROUTE_ENTRY_SIZE];
        size_t len = sizeof(buf);
        rc = nvs_get_blob(h, key, buf, &len);
        if (rc != ESP_OK || len < 2) continue;

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

    nvs_close(h);
    if (restored_routes > 0) {
        ESP_LOGI(TAG, "snapshot loaded: %lu routes restored",
                 (unsigned long)restored_routes);
    }
    return ESP_OK;
}

esp_err_t goose_snapshot_clear(void) {
    nvs_handle_t h;
    esp_err_t rc = nvs_open("goose", NVS_READWRITE, &h);
    if (rc != ESP_OK) return rc;
    for (size_t f = 0; f < supervised_field_count; f++) {
        char key[16];
        snprintf(key, sizeof(key), "snap_%.10s", supervised_fields[f]->name);
        nvs_erase_key(h, key);
    }
    nvs_commit(h);
    nvs_close(h);
    ESP_LOGI(TAG, "snapshot cleared");
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

    /* LP core liveness monitor. If lp_pulse_count stops advancing for
     * LP_STALL_THRESHOLD_US while HP is otherwise healthy, log once per
     * stall so the stall is visible in the serial log. Observation only;
     * no auto-restart. */
    #define LP_STALL_THRESHOLD_US (5 * 1000 * 1000ULL)
    static uint32_t lp_last_count = 0;
    static uint64_t lp_last_advance_us = 0;
    static bool lp_stall_warned = false;
    uint32_t cur = goose_lp_heartbeat_count();
    uint64_t now_us = esp_timer_get_time();
    if (cur != lp_last_count) {
        lp_last_count = cur;
        lp_last_advance_us = now_us;
        lp_stall_warned = false;
    } else if (cur > 0 && lp_last_advance_us > 0 &&
               (now_us - lp_last_advance_us) > LP_STALL_THRESHOLD_US &&
               !lp_stall_warned) {
        ESP_LOGW(TAG, "LP_HEARTBEAT_STALLED count=%lu stalled_us=%llu",
                 (unsigned long)cur,
                 (unsigned long long)(now_us - lp_last_advance_us));
        lp_stall_warned = true;
    }
    
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
