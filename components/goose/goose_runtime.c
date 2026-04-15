/**
 * @file goose_runtime.c
 * @brief GOOSE Runtime: The Loom of State
 * 
 * This module implements the "Loom" - a ternary state-space residing in RTC RAM.
 * It reinterprets machine memory as a geometric fabric where identity is 
 * defined by spatial coordinates rather than pointers.
 * 
 * Substrate Features:
 * 1. G.O.O.N.I.E.S. - Hierarchical Naming (Loom DNS)
 * 2. Lattice Hashing - O(1) Spatial Lookup
 * 3. Sanctuary Guard - Hardware Privilege Isolation
 * 4. Authority Sentry - Cycle-accurate Spinlock Watchdog
 */

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
#include <string.h>

#ifdef CONFIG_ULP_COPROC_ENABLED
#include "ulp_riscv.h"
#include "goose_ulp.h" 
#endif

static const char *TAG = "GOOSE_RUNTIME";

#define GOOSE_FABRIC_MAX_CELLS 256
#define GOOSE_LATTICE_BUCKETS 503 

static RTC_DATA_ATTR goose_cell_t fabric_cells[GOOSE_FABRIC_MAX_CELLS];
static RTC_DATA_ATTR int16_t lattice_index[GOOSE_LATTICE_BUCKETS]; 
static RTC_DATA_ATTR uint32_t fabric_cell_count = 0;
static RTC_DATA_ATTR uint32_t fabric_version = 0;
static RTC_DATA_ATTR volatile bool lattice_stable = false;

// G.O.O.N.I.E.S. Registry (DRAM-based)
typedef struct {
    char name[24]; // Slightly larger for hierarchical names like "agency.light.led0"
    reflex_tryte9_t coord;
} goonies_entry_t;

static goonies_entry_t goonies_registry[GOOSE_FABRIC_MAX_CELLS];
static uint32_t goonies_count = 0;

esp_err_t goonies_register(const char *name, reflex_tryte9_t coord) {
    if (goonies_count >= GOOSE_FABRIC_MAX_CELLS) return ESP_ERR_NO_MEM;
    if (!name || strlen(name) < 3) return ESP_ERR_INVALID_ARG;

    // Red-Team Remediation: Zone Protection
    // Names in 'sys' or 'agency' zones are IMMUTABLE once woven.
    bool is_protected = (strncmp(name, "sys.", 4) == 0 || strncmp(name, "agency.", 7) == 0);
    
    for (uint32_t i = 0; i < goonies_count; i++) {
        if (strcmp(goonies_registry[i].name, name) == 0) {
            if (is_protected) {
                ESP_LOGE("GOONIES", "Security Violation: Cannot re-weave protected name [%s]", name);
                return ESP_ERR_INVALID_STATE;
            }
            goonies_registry[i].coord = coord; // Update allowed for user zones
            return ESP_OK;
        }
    }

    snprintf(goonies_registry[goonies_count].name, 24, "%s", name);
    goonies_registry[goonies_count].coord = coord;
    goonies_count++;
    return ESP_OK;
}

esp_err_t goonies_resolve(const char *name, reflex_tryte9_t *out_coord) {
    for (uint32_t i = 0; i < goonies_count; i++) {
        if (strcmp(goonies_registry[i].name, name) == 0) {
            *out_coord = goonies_registry[i].coord;
            return ESP_OK;
        }
    }
    return ESP_ERR_NOT_FOUND;
}

goose_cell_t* goonies_resolve_cell(const char *name) {
    reflex_tryte9_t coord;
    if (goonies_resolve(name, &coord) == ESP_OK) {
        return goose_fabric_get_cell_by_coord(coord);
    }
    return NULL;
}

// Shell-facing helper to get registry count and entries
uint32_t goonies_get_count(void) { return goonies_count; }
const char* goonies_get_name_by_idx(uint32_t idx) { return (idx < goonies_count) ? goonies_registry[idx].name : NULL; }
reflex_tryte9_t goonies_get_coord_by_idx(uint32_t idx) { return (idx < goonies_count) ? goonies_registry[idx].coord : (reflex_tryte9_t){0}; }

static RTC_DATA_ATTR volatile uint32_t loom_authority = 0;

static uint32_t goose_lattice_hash(reflex_tryte9_t coord) {
    uint32_t hash = 0;
    for (int i = 0; i < 9; i++) {
        hash = (hash * 3) + (uint32_t)(coord.trits[i] + 1);
    }
    return hash % GOOSE_LATTICE_BUCKETS;
}

#define LOOM_LOCK_TIMEOUT_CYCLES 50000

/**
 * @brief MMIO Sanctuary Guard
 * Defines which hardware regions are "Safe Agency" for the Loom.
 */
static bool is_sanctuary_address(uint32_t addr) {
    // Permitted: GPIO (0x60091000), LEDC (0x60007000), RMT (0x60006000)
    if ((addr & 0xFFFFF000) == 0x60091000) return false; // Safe
    if ((addr & 0xFFFFF000) == 0x60007000) return false; // Safe
    if ((addr & 0xFFFFF000) == 0x60006000) return false; // Safe
    
    // Everything else is Sanctuary (PMU, EFUSE, MMU, etc.)
    return true; 
}

static uint32_t goose_loom_lock_with_stats(goose_field_t *field) {
    uint32_t start = esp_cpu_get_cycle_count();
    
    // Red-Team Remediation: Authority Sentry (Anti-Deadlock)
    while (__atomic_test_and_set(&loom_authority, __ATOMIC_ACQUIRE)) {
        if ((esp_cpu_get_cycle_count() - start) > LOOM_LOCK_TIMEOUT_CYCLES) {
            ESP_LOGE("GOOSE", "AUTHORITY DEADLOCK! Force-breaking Loom lock.");
            __atomic_clear(&loom_authority, __ATOMIC_RELEASE);
            if (field) field->stats.lock_contention_cycles += LOOM_LOCK_TIMEOUT_CYCLES;
            return LOOM_LOCK_TIMEOUT_CYCLES;
        }
        esp_rom_delay_us(1);
    }

    uint32_t end = esp_cpu_get_cycle_count();
    if (field) field->stats.lock_contention_cycles += (end - start);
    return end - start;
}

static void goose_loom_unlock(void) {
    __atomic_clear(&loom_authority, __ATOMIC_RELEASE);
}

esp_err_t goose_fabric_init(void) {
    ESP_LOGI(TAG, "Initializing Secure GOOSE Ternary Fabric...");
    
    lattice_stable = false;
    goonies_count = 0;
    
    if (esp_sleep_get_wakeup_cause() == ESP_SLEEP_WAKEUP_UNDEFINED) {
        for (int i = 0; i < GOOSE_LATTICE_BUCKETS; i++) lattice_index[i] = -1;
        
        // Root Zone Entries (GOONIES Registry)
        goose_fabric_alloc_cell("sys.origin", goose_make_coord(0, 0, 0));
        goose_fabric_alloc_cell("agency.led.intent", goose_make_coord(0, 0, 1));
        
        fabric_version = 1;
    } else {
        ESP_LOGI(TAG, "Wake-up detected. Secure Loom persistence verified.");
        for (int i = 0; i < GOOSE_LATTICE_BUCKETS; i++) lattice_index[i] = -1;
        for (uint32_t i = 0; i < fabric_cell_count; i++) {
            lattice_index[goose_lattice_hash(fabric_cells[i].coord)] = i;
        }
    }

    lattice_stable = true;
    return ESP_OK;
}

goose_cell_t* goose_fabric_get_cell_by_coord(reflex_tryte9_t coord) {
    if (!lattice_stable) return NULL; 

    uint32_t hash = goose_lattice_hash(coord);
    int16_t idx = lattice_index[hash];
    
    if (idx >= 0 && idx < fabric_cell_count) {
        if (goose_coord_equal(fabric_cells[idx].coord, coord)) {
            return &fabric_cells[idx];
        }
    }

    for (size_t i = 0; i < fabric_cell_count; i++) {
        if (goose_coord_equal(fabric_cells[i].coord, coord)) {
            return &fabric_cells[i];
        }
    }
    return NULL;
}

goose_cell_t* goose_fabric_get_cell(const char *name) {
    return goonies_resolve_cell(name);
}

static portMUX_TYPE fabric_mux = portMUX_INITIALIZER_UNLOCKED;

goose_cell_t* goose_fabric_alloc_cell(const char *name, reflex_tryte9_t coord) {
    taskENTER_CRITICAL(&fabric_mux);
    
    if (goose_fabric_get_cell_by_coord(coord) != NULL) {
        taskEXIT_CRITICAL(&fabric_mux);
        return NULL;
    }
    
    if (fabric_cell_count >= GOOSE_FABRIC_MAX_CELLS) {
        taskEXIT_CRITICAL(&fabric_mux);
        return NULL;
    }

    uint32_t idx = fabric_cell_count++;
    goose_cell_t *c = &fabric_cells[idx];
    memset(c, 0, sizeof(goose_cell_t));
    c->coord = coord;
    c->state = 0;
    
    lattice_index[goose_lattice_hash(coord)] = (int16_t)idx;
    
    // Register in GOONIES
    if (name) {
        goonies_register(name, coord);
    }

    fabric_version++;
    taskEXIT_CRITICAL(&fabric_mux);
    return c;
}

esp_err_t goose_fabric_set_agency(goose_cell_t *cell, uint32_t hardware_addr, goose_cell_type_t type) {
    if (!cell) return ESP_ERR_INVALID_ARG;

    // Red-Team Remediation: MMIO Sanctuary Guard
    if (is_sanctuary_address(hardware_addr) && type != GOOSE_CELL_SYSTEM_ONLY) {
        ESP_LOGE("GOOSE", "SANCTUARY VIOLATION: Refusing to map cell to address 0x%08lX", hardware_addr);
        return ESP_ERR_NOT_SUPPORTED;
    }

    cell->hardware_addr = hardware_addr;
    cell->type = type;
    return ESP_OK;
}

// --- Radio Route Registry (Required for Atmospheric Arcing) ---
static goose_field_t *global_fields[16];
static size_t global_field_count = 0;

goose_route_t* goose_fabric_find_radio_route_by_source_coord(reflex_tryte9_t coord) {
    for (size_t f = 0; f < global_field_count; f++) {
        goose_field_t *field = global_fields[f];
        for (size_t r = 0; r < field->route_count; r++) {
            goose_route_t *route = &field->routes[r];
            if (route->coupling == GOOSE_COUPLING_RADIO && goose_coord_equal(route->source_coord, coord)) {
                return route;
            }
        }
    }
    return NULL;
}

reflex_tryte9_t goose_make_coord(int8_t field, int8_t region, int8_t cell) {
    reflex_tryte9_t t = {0};
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

esp_err_t goose_apply_route(goose_route_t *route) {
    if (!route) return ESP_ERR_INVALID_ARG;
    if (route->coupling == GOOSE_COUPLING_HARDWARE) {
        if (route->cached_sink && route->cached_source && route->cached_sink->hardware_addr < GPIO_NUM_MAX) {
            esp_rom_gpio_connect_out_signal((gpio_num_t)route->cached_sink->hardware_addr, 
                                            (uint32_t)route->cached_source->hardware_addr, 
                                            (route->orientation == -1), false);
        }
    }
    return ESP_OK;
}

esp_err_t goose_process_transitions(goose_field_t *field) {
    if (!field) return ESP_ERR_INVALID_ARG;
    uint64_t now = esp_timer_get_time();

    goose_loom_lock_with_stats(field);

    // Evolution
    for (size_t i = 0; i < field->transition_count; i++) {
        goose_transition_t *t = &field->transitions[i];
        if (!t->cached_target || t->cached_version != fabric_version) {
            t->cached_target = goose_fabric_get_cell_by_coord(t->target_coord);
            t->cached_version = fabric_version;
        }
        if (t->cached_target && t->evolution_fn && (now - t->last_run_us) >= (t->interval_ms * 1000)) {
            t->cached_target->state = t->evolution_fn(t->context);
            t->last_run_us = now;
        }
    }

    // Propagation
    for (size_t i = 0; i < field->route_count; i++) {
        goose_route_t *r = &field->routes[i];
        if (!r->cached_source || r->cached_version != fabric_version) {
            r->cached_source = goose_fabric_get_cell_by_coord(r->source_coord);
            r->cached_sink = goose_fabric_get_cell_by_coord(r->sink_coord);
            r->cached_control = goose_fabric_get_cell_by_coord(r->control_coord);
            r->cached_version = fabric_version;
        }
        if (!r->cached_source || !r->cached_sink) continue;

        if (r->coupling == GOOSE_COUPLING_SOFTWARE) {
            reflex_trit_t orient = r->cached_control ? (reflex_trit_t)r->cached_control->state : r->orientation;
            r->cached_sink->state = (int8_t)((int)r->cached_source->state * (int)orient);
            
            if (r->cached_sink->hardware_addr > 0 && r->cached_sink->hardware_addr < GPIO_NUM_MAX) {
                gpio_set_level((gpio_num_t)r->cached_sink->hardware_addr, r->cached_sink->state == 1 ? 1 : 0);
            }
        } else if (r->coupling == GOOSE_COUPLING_RADIO) {
            extern esp_err_t goose_atmosphere_emit_arc(goose_cell_t *source);
            goose_atmosphere_emit_arc(r->cached_source);
        }
    }

    goose_loom_unlock();
    field->stats.total_pulses++;
    return ESP_OK;
}

static void goose_regional_pulse_task(void *arg) {
    goose_field_t *field = (goose_field_t *)arg;
    uint32_t delay_ms = 1000 / (uint32_t)field->rhythm;
    while(1) {
        goose_process_transitions(field);
        vTaskDelay(pdMS_TO_TICKS(delay_ms ? delay_ms : 1));
    }
}

esp_err_t goose_field_start_pulse(goose_field_t *field) {
    if (!field) return ESP_ERR_INVALID_ARG;

    // Register for radio route lookups
    if (global_field_count < 16) {
        global_fields[global_field_count++] = field;
    }

    char task_name[16];
    snprintf(task_name, sizeof(task_name), "p_%.13s", field->name);
    xTaskCreate(goose_regional_pulse_task, task_name, 4096, field, 10, NULL);
    return ESP_OK;
}
