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
        
        // Skip equilibrium check for input-driven routes (we don't control the environment)
        if (r->source->type == GOOSE_CELL_HARDWARE_IN) continue;

        // Equilibrium Check: Does Sink == Source * Orientation?
        reflex_trit_t expected = (reflex_trit_t)((int)r->source->state * (int)(r->control ? r->control->state : r->orientation));
        
        if (r->sink->state != expected) {
            // Disequilibrium is only a "Fault" if the sink is an output actor
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
    static int rebalance_limit = 0;
    if (rebalance_limit++ > 5) {
        ESP_LOGE(TAG, "Geometric Oscillation detected! Halting rebalance to prevent storm.");
        system_balance.state = REFLEX_TRIT_NEG; // System Fault
        return ESP_ERR_INVALID_STATE;
    }

    ESP_LOGI(TAG, "Attempting to re-level manifold for field [%s]...", field->name);
    
    for (size_t i = 0; i < field->route_count; i++) {
        goose_route_t *r = &field->routes[i];
        
        // Healing Rule: If an intent is blocked (+1 -> 0), restore it
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
