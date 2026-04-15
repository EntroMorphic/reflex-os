#include "goose.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_cpu.h"
#include "driver/gpio.h"
#include "esp_rom_gpio.h"
#include "soc/gpio_sig_map.h"
#include "esp_sleep.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#ifdef CONFIG_ULP_COPROC_ENABLED
#include "ulp_riscv.h"
#include "goose_ulp.h" // Generated header from CMake
#endif

static const char *TAG = "GOOSE_RUNTIME";

#define GOOSE_FABRIC_MAX_CELLS 256
static RTC_DATA_ATTR goose_cell_t fabric_cells[GOOSE_FABRIC_MAX_CELLS];
static RTC_DATA_ATTR uint32_t fabric_cell_count = 0;
static RTC_DATA_ATTR uint32_t fabric_version = 0;

/**
 * Loom Authority (Trans-Core Spinlock)
 * 0 = Available
 * 1 = HP Core (Mind)
 * 2 = LP Core (Heart)
 */
static RTC_DATA_ATTR volatile uint32_t loom_authority = 0;

static uint32_t goose_loom_lock_with_stats(goose_field_t *field) {
    uint32_t start = esp_cpu_get_cycle_count();
    while (__sync_val_compare_and_swap(&loom_authority, 0, 1) != 0) {
        vTaskDelay(0); // Short yield
    }
    uint32_t end = esp_cpu_get_cycle_count();
    if (field) field->stats.lock_contention_cycles += (end - start);
    return end - start;
}

static void goose_loom_lock(void) {
    goose_loom_lock_with_stats(NULL);
}

static void goose_loom_unlock(void) {
    loom_authority = 0;
}

esp_err_t goose_fabric_init(void) {
    ESP_LOGI(TAG, "Initializing GOOSE Ternary Fabric (The Loom)...");
    
    // Check if we are waking up from sleep - if so, don't re-init Loom
    if (esp_sleep_get_wakeup_cause() == ESP_SLEEP_WAKEUP_UNDEFINED) {
        // [0,0,0] System:Origin
        fabric_cells[0].coord = goose_make_coord(0, 0, 0); 
        fabric_cells[0].state = 1; // POS (Balanced)
        fabric_cells[0].type = 0; // VIRTUAL
        
        // [0,0,1] Agency:LED_Intent (Legacy Proof)
        fabric_cells[1].coord = goose_make_coord(0, 0, 1); 
        fabric_cells[1].state = 0; // ZERO
        fabric_cells[1].type = 3; // INTENT
        fabric_cells[1].hardware_addr = 15; // LED PIN
        
        fabric_cell_count = 2;
        fabric_version = 1;
    } else {
        ESP_LOGI(TAG, "Wake-up detected. Loom persistence verified.");
    }

#ifdef CONFIG_ULP_COPROC_ENABLED
    ESP_LOGI(TAG, "Starting Autonomous Heartbeat (LP Core)...");
    esp_err_t err = ulp_riscv_load_binary(goose_ulp_bin_start, (goose_ulp_bin_end - goose_ulp_bin_start));
    if (err == ESP_OK) {
        ulp_riscv_run();
    } else {
        ESP_LOGE(TAG, "Failed to load LP Pulse binary: %d", err);
    }
#endif

    return ESP_OK;
}

goose_cell_t* goose_fabric_get_cell(const char *name) {
    for (size_t i = 0; i < fabric_cell_count; i++) {
        if (strcmp(fabric_cells[i].name, name) == 0) return &fabric_cells[i];
    }
    return NULL;
}

goose_cell_t* goose_fabric_get_cell_by_coord(reflex_tryte9_t coord) {
    for (size_t i = 0; i < fabric_cell_count; i++) {
        if (goose_coord_equal(fabric_cells[i].coord, coord)) return &fabric_cells[i];
    }
    return NULL;
}

// Global list of fields for route lookup
static goose_field_t *global_fields[16];
static size_t global_field_count = 0;

goose_route_t* goose_fabric_find_radio_route_by_source_coord(reflex_tryte9_t coord) {
    for (size_t f = 0; f < global_field_count; f++) {
        goose_field_t *field = global_fields[f];
        for (size_t r = 0; r < field->route_count; r++) {
            goose_route_t *route = &field->routes[r];
            if (route->coupling == GOOSE_COUPLING_RADIO && goose_coord_equal(route->source->coord, coord)) {
                return route;
            }
        }
    }
    return NULL;
}

goose_cell_t* goose_fabric_alloc_cell(const char *name, reflex_tryte9_t coord) {
    // Spatial Guard: Check for collision
    if (goose_fabric_get_cell_by_coord(coord) != NULL) {
        ESP_LOGE(TAG, "Geometric Collision at coord!");
        return NULL;
    }

    if (fabric_cell_count >= GOOSE_FABRIC_MAX_CELLS) {
        ESP_LOGE(TAG, "Loom is full!");
        return NULL;
    }

    goose_cell_t *c = &fabric_cells[fabric_cell_count++];
    memset(c, 0, sizeof(goose_cell_t));
    snprintf(c->name, 16, "%s", name);
    c->coord = coord;
    c->state = 0;
    
    ESP_LOGI(TAG, "Allocated Cell [%s] at new coord index %u", name, fabric_cell_count-1);
    return c;
}

reflex_tryte9_t goose_make_coord(int8_t field, int8_t region, int8_t cell) {
    reflex_tryte9_t t = {0};
    // Each component fits in 3 trits (base-3 balanced: -13 to +13)
    // Field: trits[0..2], Region: trits[3..5], Cell: trits[6..8]
    
    // Simple mapping: -1 -> -1, 0 -> 0, 1 -> 1 (only using 1 trit per component for now for simplicity)
    t.trits[0] = (reflex_trit_t)field;
    t.trits[3] = (reflex_trit_t)region;
    t.trits[6] = (reflex_trit_t)cell;
    return t;
}

bool goose_coord_equal(reflex_tryte9_t a, reflex_tryte9_t b) {
    for (int i = 0; i < 9; i++) {
        if (a.trits[i] != b.trits[i]) return false;
    }
    return true;
}

esp_err_t goose_fabric_process(void) {
    // In the future, this would process global system-wide routes
    return ESP_OK;
}

esp_err_t goose_apply_route(goose_route_t *route) {
    if (!route || !route->source || !route->sink) return ESP_ERR_INVALID_ARG;

    ESP_LOGI(TAG, "Applying Route: %s (%s -> %s) [Orient: %d]", 
             route->name, route->source->name, route->sink->name, route->orientation);

    if (route->coupling == GOOSE_COUPLING_HARDWARE) {
        /**
         * Shadow Agency Implementation
         * Direct silicon-level routing bypasses software registers.
         */
        if (route->sink->hardware_addr < GPIO_NUM_MAX) {
            bool invert = (route->orientation == -1);
            esp_rom_gpio_connect_out_signal((gpio_num_t)route->sink->hardware_addr, 
                                            (uint32_t)route->source->hardware_addr, 
                                            invert, false);
        }
    }

    return ESP_OK;
}

esp_err_t goose_process_transitions(goose_field_t *field) {
    if (!field) return ESP_ERR_INVALID_ARG;
    uint32_t start_cycles = esp_cpu_get_cycle_count();
    uint64_t now = esp_timer_get_time();

    goose_loom_lock_with_stats(field);

    /**
     * Phase 1: Evolution (Autonomous Rhythm)
     */
    for (size_t i = 0; i < field->transition_count; i++) {
        goose_transition_t *t = &field->transitions[i];
        
        if (!t->cached_target || t->cached_version != fabric_version) {
            t->cached_target = goose_fabric_get_cell_by_coord(t->target_coord);
            t->cached_version = fabric_version;
            field->stats.tlb_miss_count++;
        }

        if (t->cached_target && t->evolution_fn && (now - t->last_run_us) >= (t->interval_ms * 1000)) {
            t->cached_target->state = t->evolution_fn(t->context);
            t->last_run_us = now;
        }
    }

    /**
     * Phase 2: Propagation (Geometric Flow)
     */
    for (size_t i = 0; i < field->route_count; i++) {
        goose_route_t *r = &field->routes[i];
        
        if (!r->cached_source || r->cached_version != fabric_version) {
            r->cached_source = goose_fabric_get_cell_by_coord(r->source_coord);
            r->cached_sink = goose_fabric_get_cell_by_coord(r->sink_coord);
            r->cached_control = goose_fabric_get_cell_by_coord(r->control_coord);
            r->cached_version = fabric_version;
            field->stats.tlb_miss_count++;
        }

        if (!r->cached_source || !r->cached_sink) continue;

        if (r->coupling == GOOSE_COUPLING_SOFTWARE) {
            reflex_trit_t orient = r->cached_control ? (reflex_trit_t)r->cached_control->state : r->orientation;
            r->cached_sink->state = (int8_t)((int)r->cached_source->state * (int)orient);
            
            if (r->cached_sink->hardware_addr > 0 && r->cached_sink->hardware_addr < GPIO_NUM_MAX) {
                gpio_set_level((gpio_num_t)r->cached_sink->hardware_addr, 
                               r->cached_sink->state == 1 ? 1 : 0);
            } else if (r->cached_sink->hardware_addr >= 0x60000000) {
                volatile uint32_t *reg = (volatile uint32_t *)r->cached_sink->hardware_addr;
                uint32_t mask = r->cached_sink->bit_mask ? r->cached_sink->bit_mask : 0xFFFFFFFF;
                
                if (r->cached_sink->state == 1) {
                    *reg |= mask;
                } else if (r->cached_sink->state == -1) {
                    *reg &= ~mask;
                }
            }
        } else if (r->coupling == GOOSE_COUPLING_RADIO) {
            extern esp_err_t goose_atmosphere_emit_arc(goose_cell_t *source);
            goose_atmosphere_emit_arc(r->cached_source);
        }
    }

    goose_loom_unlock();

    uint32_t end_cycles = esp_cpu_get_cycle_count();
    uint32_t diff = end_cycles - start_cycles;
    field->stats.last_pulse_cycles = diff;
    if (diff > field->stats.max_pulse_cycles) field->stats.max_pulse_cycles = diff;
    field->stats.total_pulses++;

    return ESP_OK;
}

goose_stats_t goose_field_get_stats(goose_field_t *field) {
    if (!field) return (goose_stats_t){0};
    return field->stats;
}

/**
 * @brief Regional Pulse Task
 * This high-frequency task executes a single field's transitions at its defined Rhythm.
 */
static void goose_regional_pulse_task(void *arg) {
    goose_field_t *field = (goose_field_t *)arg;
    uint32_t delay_ms = 1000 / (uint32_t)field->rhythm;
    if (delay_ms == 0) delay_ms = 1;

    ESP_LOGI(TAG, "Regional Pulse Task started for [%s] at %u Hz", field->name, (uint32_t)field->rhythm);

    while(1) {
        goose_process_transitions(field);
        vTaskDelay(pdMS_TO_TICKS(delay_ms));
    }
}
    }

    // 2. Process Routes (State Propagation)
    for (size_t i = 0; i < field->route_count; i++) {
        goose_route_t *r = &field->routes[i];
        
        if (r->coupling == GOOSE_COUPLING_SOFTWARE) {
            reflex_trit_t orient = r->control ? r->control->state : r->orientation;
            r->sink->state = (int8_t)((int)r->source->state * (int)orient);
            
            if (r->sink->hardware_addr < GPIO_NUM_MAX) {
                gpio_set_level((gpio_num_t)r->sink->hardware_addr, 
                               r->sink->state == 1 ? 1 : 0);
            }
        }
    }

    return ESP_OK;
}

static void goose_regional_pulse_task(void *arg) {
    goose_field_t *field = (goose_field_t *)arg;
    uint32_t delay_ms = 1000 / (uint32_t)field->rhythm;
    if (delay_ms == 0) delay_ms = 1;

    ESP_LOGI(TAG, "Regional Pulse Task started for [%s] at %u Hz", field->name, (uint32_t)field->rhythm);

    while(1) {
        goose_process_transitions(field);
        vTaskDelay(pdMS_TO_TICKS(delay_ms));
    }
}

esp_err_t goose_field_start_pulse(goose_field_t *field) {
    if (!field) return ESP_ERR_INVALID_ARG;
    
    // Register field for global route lookups
    if (global_field_count < 16) {
        global_fields[global_field_count++] = field;
    }

    char task_name[16];
    snprintf(task_name, 16, "pulse_%s", field->name);
    
    xTaskCreate(goose_regional_pulse_task, task_name, 4096, field, 10, NULL);
    return ESP_OK;
}

