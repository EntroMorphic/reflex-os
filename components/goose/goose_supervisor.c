/**
 * @file goose_supervisor.c
 * @brief Harmonic Supervisor: Autonomic Posture Regulation
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
        reflex_trit_t orient = r->cached_control ? (reflex_trit_t)r->cached_control->state : r->orientation;
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
    else if (swarm_accumulator < 0) swarm_accumulator++;
    return ESP_OK;
}

/**
 * @brief Learning Pass (Topological Plasticity)
 * Adjusts the learned_orientation of routes based on sys.ai.pain.
 */
esp_err_t goose_supervisor_learn_sync(void) {
    goose_cell_t *pain_cell = goonies_resolve_cell("sys.ai.pain");
    goose_cell_t *reward_cell = goonies_resolve_cell("sys.ai.reward");
    
    if (pain_cell && pain_cell->state == -1) {
        // Pain sensed! Jiggle the Tapestry to find equilibrium.
        ESP_LOGW(TAG, "AI Pain detected. Increasing plasticity...");
        for (size_t f = 0; f < supervised_field_count; f++) {
            goose_field_t *field = supervised_fields[f];
            for (size_t r = 0; r < field->route_count; r++) {
                // Randomly shift 10% of routes to find a new manifold shape
                if ((esp_random() % 10) == 0) {
                    field->routes[r].learned_orientation = (int8_t)((esp_random() % 3) - 1);
                }
            }
        }
    } else if (reward_cell && reward_cell->state == 1) {
        // Success sensed! Pin the current learned orientations.
        for (size_t f = 0; f < supervised_field_count; f++) {
            goose_field_t *field = supervised_fields[f];
            for (size_t r = 0; r < field->route_count; r++) {
                field->routes[r].orientation = field->routes[r].learned_orientation;
            }
        }
        reward_cell->state = 0; // Reset reward
    }
    return ESP_OK;
}

esp_err_t goose_supervisor_pulse(void) {
    // ... (existing equilibrium checks)
    
    // Learning pass at 1Hz
    static int learn_div = 0;
    if (learn_div++ >= 10) {
        goose_supervisor_learn_sync();
        learn_div = 0;
    }
    
    // Swarm Sync at 1Hz (offset by 5)
    static int sync_div = 0;
    if (sync_div++ >= 10) {
        goose_supervisor_swarm_sync();
        sync_div = 0;
    }
    return ESP_OK;
}
