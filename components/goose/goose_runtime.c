#include "goose.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "driver/gpio.h"
#include "esp_rom_gpio.h"
#include "soc/gpio_sig_map.h"

static const char *TAG = "GOOSE_RUNTIME";

esp_err_t goose_fabric_init(void) {
    ESP_LOGI(TAG, "Initializing GOOSE Ternary Fabric...");
    return ESP_OK;
}

esp_err_t goose_apply_route(goose_route_t *route) {
    if (!route || !route->source || !route->sink) return ESP_ERR_INVALID_ARG;

    ESP_LOGI(TAG, "Applying Route: %s (%s -> %s) [Orient: %d]", 
             route->name, route->source->name, route->sink->name, route->orientation);

    if (route->coupling == GOOSE_COUPLING_HARDWARE) {
        // Shadow Agency: Connect hardware signals directly via GPIO Matrix
        // Note: This requires the hardware_addr to be a valid Matrix Signal Index
        if (route->sink->hardware_addr < GPIO_NUM_MAX) {
            bool invert = (route->orientation == REFLEX_TRIT_NEG);
            esp_rom_gpio_connect_out_signal((gpio_num_t)route->sink->hardware_addr, 
                                            (uint32_t)route->source->hardware_addr, 
                                            invert, false);
            ESP_LOGI(TAG, "Hardware route established: Signal %lu -> GPIO %lu (Invert: %d)", 
                     (uint32_t)route->source->hardware_addr, 
                     (uint32_t)route->sink->hardware_addr, invert);
        }
    }

    return ESP_OK;
}

esp_err_t goose_process_transitions(goose_field_t *field) {
    if (!field) return ESP_ERR_INVALID_ARG;
    uint64_t now = esp_timer_get_time();

    // 1. Process Evolutions (Transitions)
    for (size_t i = 0; i < field->transition_count; i++) {
        goose_transition_t *t = &field->transitions[i];
        if (t->evolution_fn && (now - t->last_run_us) >= (t->interval_ms * 1000)) {
            t->target->state = t->evolution_fn(t->context);
            t->last_run_us = now;
        }
    }

    // 2. Process Routes (State Propagation)
    for (size_t i = 0; i < field->route_count; i++) {
        goose_route_t *r = &field->routes[i];
        
        if (r->coupling == GOOSE_COUPLING_SOFTWARE) {
            // Apply ternary product rule: Sink = Source * Orientation
            r->sink->state = (reflex_trit_t)((int)r->source->state * (int)r->orientation);
            
            // Update hardware if mapped to a physical GPIO
            if (r->sink->hardware_addr < GPIO_NUM_MAX) {
                gpio_set_level((gpio_num_t)r->sink->hardware_addr, 
                               r->sink->state == REFLEX_TRIT_POS ? 1 : 0);
            }
        }
    }

    return ESP_OK;
}
