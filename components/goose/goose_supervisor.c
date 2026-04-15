/**
 * @file goose_supervisor.c
 * @brief Harmonic Supervisor: Autonomic Posture Regulation
 * 
 * The Supervisor acts as the "Immune System" of the machine. It perceives
 * disequilibrium within local and recursive manifolds and exercises 
 * meta-agency to restore harmonic state.
 * 
 * Features:
 * 1. Recursive Regulation - Monitors the health of nested holons via Field Proxies.
 * 2. Equilibrium Check - Validates geometric reality against physical sink state.
 * 3. Meta-Agency - Re-levels inhibited routes to restore signal flow.
 * 4. Storm Protection - Prevents geometric oscillations via rebalance limits.
 */

#include "goose.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "GOOSE_SUPERVISOR";

#define MAX_SUPERVISED_FIELDS 32
static goose_field_t *supervised_fields[MAX_SUPERVISED_FIELDS];
static size_t supervised_field_count = 0;

static portMUX_TYPE supervisor_mux = portMUX_INITIALIZER_UNLOCKED;

/**
 * @brief The Balance Trit
 * Reflects global system health:
 * -1: Irreconcilable Fault (Storm)
 *  0: Degraded (Correction in progress)
 * +1: Balanced (Equilibrium)
 */
static goose_cell_t system_balance = { .state = REFLEX_TRIT_POS };

esp_err_t goose_supervisor_init(void) {
    ESP_LOGI(TAG, "Initializing Harmonic Supervisor (Posture Regulation)...");
    return ESP_OK;
}

/**
 * @brief Weave a field manifold into global regulation.
 */
esp_err_t goose_supervisor_register_field(goose_field_t *field) {
    taskENTER_CRITICAL(&supervisor_mux);
    if (supervised_field_count >= MAX_SUPERVISED_FIELDS) {
        taskEXIT_CRITICAL(&supervisor_mux);
        return ESP_ERR_NO_MEM;
    }
    
    // Check for duplicate
    for(size_t i=0; i<supervised_field_count; i++) {
        if (supervised_fields[i] == field) {
            taskEXIT_CRITICAL(&supervisor_mux);
            return ESP_OK;
        }
    }

    supervised_fields[supervised_field_count++] = field;
    taskEXIT_CRITICAL(&supervisor_mux);
    ESP_LOGI(TAG, "Field [%s] woven into Supervisor regulation.", field->name);
    return ESP_OK;
}

/**
 * @brief Equilibrium Check
 * Determines if a manifold's reality (Sink) is consistent with its geometry (Source * Route).
 */
esp_err_t goose_supervisor_check_equilibrium(goose_field_t *field) {
    if (!field) return ESP_ERR_INVALID_ARG;

    for (size_t i = 0; i < field->route_count; i++) {
        goose_route_t *r = &field->routes[i];
        
        /**
         * Equilibrium Grounding
         * We skip input-driven routes (HARDWARE_IN) because physical reality is a 
         * "Higher Truth" than our geometry. The Supervisor cannot re-level the external world.
         */
        if (r->cached_source && r->cached_source->type == GOOSE_CELL_HARDWARE_IN) continue;

        // Geometric Expectation: Sink == Source * EffectiveOrientation
        reflex_trit_t orient = r->cached_control ? (reflex_trit_t)r->cached_control->state : r->orientation;
        reflex_trit_t expected = (reflex_trit_t)((int)r->cached_source->state * (int)orient);
        
        if (r->cached_sink && r->cached_sink->state != (int8_t)expected) {
            /**
             * Fault Attribution
             * Disequilibrium is only a "Fault" if the sink is an output actor we control.
             */
            if (r->cached_sink->type == GOOSE_CELL_HARDWARE_OUT) {
                ESP_LOGW(TAG, "Manifold tilt detected in route [%s]: expected %d, got %d", 
                         r->name, expected, r->cached_sink->state);
                system_balance.state = REFLEX_TRIT_ZERO; // Degraded
                return ESP_FAIL; 
            }
        }
    }

    system_balance.state = REFLEX_TRIT_POS; // Balanced
    return ESP_OK;
}

/**
 * @brief Re-balance Manifold
 * Exercises meta-agency to restore signal flow to inhibited intent-driven routes.
 */
esp_err_t goose_supervisor_rebalance(goose_field_t *field) {
    /**
     * Oscillation Protection
     * Isolated per-field to prevent "Storm Bleed" where one unstable manifold
     * prevents the regulation of others.
     */
    int field_rebalance_limit = 0;
    
    ESP_LOGI(TAG, "Attempting to re-level manifold for field [%s]...", field->name);
    
    for (size_t i = 0; i < field->route_count; i++) {
        goose_route_t *r = &field->routes[i];
        
        if (field_rebalance_limit++ > 10) {
            ESP_LOGE(TAG, "Geometric Oscillation in [%s]! Halting rebalance.", field->name);
            system_balance.state = REFLEX_TRIT_NEG;
            return ESP_ERR_INVALID_STATE;
        }

        /**
         * Healing Policy: Agency Restoration
         */
        if (r->cached_source && r->cached_sink &&
            r->cached_source->type == GOOSE_CELL_INTENT && 
            r->cached_sink->type == GOOSE_CELL_HARDWARE_OUT &&
            r->orientation == REFLEX_TRIT_ZERO) {
            
            ESP_LOGW(TAG, "Restoring agency to route [%s]", r->name);
            r->orientation = REFLEX_TRIT_POS;
        }
    }

    return goose_process_transitions(field);
}

/**
 * @brief Harmonic Pulse
 * Runs one regulation pass across all supervised manifolds.
 */
esp_err_t goose_supervisor_pulse(void) {
    taskENTER_CRITICAL(&supervisor_mux);
    size_t count = supervised_field_count;
    taskEXIT_CRITICAL(&supervisor_mux);

    for (size_t i = 0; i < count; i++) {
        // Perception pass: Detect disequilibrium
        if (goose_supervisor_check_equilibrium(supervised_fields[i]) != ESP_OK) {
            // Agency pass: Attempt to re-level the manifold
            goose_supervisor_rebalance(supervised_fields[i]);
        }
    }
    return ESP_OK;
}
