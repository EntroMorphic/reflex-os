/**
 * @file goose_supervisor.c
 * @brief Harmonic Supervisor (System Immune System)
 * 
 * The Supervisor regulates general system posture by perceiving disequilibrium
 * and exercising meta-agency (re-patching routes) to restore equilibrium.
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
static goose_cell_t system_balance = { .name = "balance", .state = 1 };

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
        if (r->source->type == GOOSE_CELL_HARDWARE_IN) continue;

        // Geometric Expectation: Sink == Source * EffectiveOrientation
        reflex_trit_t orient = r->control ? (reflex_trit_t)r->control->state : r->orientation;
        reflex_trit_t expected = (reflex_trit_t)((int)r->source->state * (int)orient);
        
        if (r->sink->state != (int8_t)expected) {
            /**
             * Fault Attribution
             * Disequilibrium is only a "Fault" if the sink is an output actor we control.
             */
            if (r->sink->type == GOOSE_CELL_HARDWARE_OUT) {
                ESP_LOGW(TAG, "Manifold tilt detected in route [%s]: expected %d, got %d", 
                         r->name, expected, r->sink->state);
                system_balance.state = 0; // Degraded
                return ESP_FAIL; 
            }
        }
    }

    system_balance.state = 1; // Balanced
    return ESP_OK;
}

/**
 * @brief Re-balance Manifold
 * Exercises meta-agency to restore signal flow to inhibited intent-driven routes.
 */
esp_err_t goose_supervisor_rebalance(goose_field_t *field) {
    /**
     * Oscillation Protection
     * A "Geometric Storm" occurs when re-balancing creates a feedback loop.
     * We limit attempts per pulse to ensure system stability.
     */
    static int rebalance_limit = 0;
    if (rebalance_limit++ > 5) {
        ESP_LOGE(TAG, "Geometric Oscillation detected! Halting rebalance to prevent storm.");
        system_balance.state = -1; // System Fault
        return ESP_ERR_INVALID_STATE;
    }

    ESP_LOGI(TAG, "Attempting to re-level manifold for field [%s]...", field->name);
    
    for (size_t i = 0; i < field->route_count; i++) {
        goose_route_t *r = &field->routes[i];
        
        /**
         * Healing Policy: Agency Restoration
         * If a route connecting INTENT to HARDWARE_OUT is inhibited (0), 
         * we assume it's a blockage and force it back to ADMIT (+1).
         */
        if (r->source->type == GOOSE_CELL_INTENT && 
            r->sink->type == GOOSE_CELL_HARDWARE_OUT &&
            r->orientation == 0) {
            
            ESP_LOGW(TAG, "Restoring agency to route [%s]", r->name);
            r->orientation = 1;
        }
    }

    rebalance_limit = 0;
    return goose_process_transitions(field);
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

esp_err_t goose_supervisor_pulse(void) {
    taskENTER_CRITICAL(&supervisor_mux);
    size_t count = supervised_field_count;
    taskEXIT_CRITICAL(&supervisor_mux);

    for (size_t i = 0; i < count; i++) {
        // Perception pass
        if (goose_supervisor_check_equilibrium(supervised_fields[i]) != ESP_OK) {
            // Agency pass
            goose_supervisor_rebalance(supervised_fields[i]);
        }
    }
    return ESP_OK;
}

esp_err_t goose_supervisor_check_equilibrium(goose_field_t *field) {
    if (!field) return ESP_ERR_INVALID_ARG;

    for (size_t i = 0; i < field->route_count; i++) {
        goose_route_t *r = &field->routes[i];
        
        /**
         * Equilibrium Grounding
         * We skip input-driven routes (HARDWARE_IN) because physical reality is a 
         * "Higher Truth" than our geometry. The Supervisor cannot re-level the external world.
         */
        if (r->source->type == GOOSE_CELL_HARDWARE_IN) continue;

        // Geometric Expectation: Sink == Source * EffectiveOrientation
        reflex_trit_t expected = (reflex_trit_t)((int)r->source->state * (int)(r->control ? r->control->state : r->orientation));
        
        if (r->sink->state != expected) {
            /**
             * Fault Attribution
             * Disequilibrium is only a "Fault" if the sink is an output actor we control.
             */
            if (r->sink->type == GOOSE_CELL_HARDWARE_OUT) {
                ESP_LOGW(TAG, "Manifold tilt detected in route [%s]: expected %d, got %d", 
                         r->name, expected, r->sink->state);
                system_balance.state = REFLEX_TRIT_ZERO;
                return ESP_FAIL; 
            }
        }
    }

    system_balance.state = REFLEX_TRIT_POS; 
    return ESP_OK;
}

esp_err_t goose_supervisor_rebalance(goose_field_t *field) {
    /**
     * Oscillation Protection
     * A "Geometric Storm" occurs when re-balancing creates a feedback loop.
     * We limit attempts per pulse to ensure system stability.
     */
    static int rebalance_limit = 0;
    if (rebalance_limit++ > 5) {
        ESP_LOGE(TAG, "Geometric Oscillation detected! Halting rebalance to prevent storm.");
        system_balance.state = REFLEX_TRIT_NEG; // System Fault
        return ESP_ERR_INVALID_STATE;
    }

    ESP_LOGI(TAG, "Attempting to re-level manifold for field [%s]...", field->name);
    
    for (size_t i = 0; i < field->route_count; i++) {
        goose_route_t *r = &field->routes[i];
        
        /**
         * Healing Policy: Agency Restoration
         * If a route connecting INTENT to HARDWARE_OUT is inhibited (0), 
         * we assume it's a blockage and force it back to ADMIT (+1).
         */
        if (r->source->type == GOOSE_CELL_INTENT && 
            r->sink->type == GOOSE_CELL_HARDWARE_OUT &&
            r->orientation == REFLEX_TRIT_ZERO) {
            
            ESP_LOGW(TAG, "Restoring agency to route [%s]", r->name);
            r->orientation = REFLEX_TRIT_POS;
        }
    }

    rebalance_limit = 0;
    return goose_process_transitions(field);
}
