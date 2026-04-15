/**
 * @file goose_runtime.c
 * @brief GOOSE Runtime: Hardened Autonomous Substrate
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
#include "ulp_lp_core.h"
#include "goose_ulp.h"
extern const uint8_t ulp_main_bin_start[] asm("_binary_goose_ulp_bin_start");
extern const uint8_t ulp_main_bin_end[]   asm("_binary_goose_ulp_bin_end");
#endif

static const char *TAG = "GOOSE_RUNTIME";

#define GOOSE_FABRIC_MAX_CELLS 256
#define GOOSE_LATTICE_BUCKETS 503
#define GOOSE_FABRIC_MAGIC    0xF11BFABE

static RTC_DATA_ATTR goose_cell_t fabric_cells[GOOSE_FABRIC_MAX_CELLS];
static RTC_DATA_ATTR int16_t lattice_index[GOOSE_LATTICE_BUCKETS];
static RTC_DATA_ATTR uint32_t fabric_cell_count = 0;
static RTC_DATA_ATTR uint32_t fabric_version = 0;
static RTC_DATA_ATTR volatile bool lattice_stable = false;
static RTC_DATA_ATTR uint32_t fabric_magic = 0;

typedef struct {
    char name[24]; 
    reflex_tryte9_t coord;
} goonies_entry_t;

static goonies_entry_t goonies_registry[GOOSE_FABRIC_MAX_CELLS];
static uint32_t goonies_count = 0;

static RTC_DATA_ATTR volatile uint32_t loom_authority = 0;

static uint32_t goose_lattice_hash(reflex_tryte9_t coord) {
    uint32_t hash = 0;
    for (int i = 0; i < 9; i++) { hash = (hash * 3) + (uint32_t)(coord.trits[i] + 1); }
    return hash % GOOSE_LATTICE_BUCKETS;
}

esp_err_t goonies_register(const char *name, reflex_tryte9_t coord, bool is_system_weaving) {
    if (goonies_count >= GOOSE_FABRIC_MAX_CELLS) return ESP_ERR_NO_MEM;
    if (!name || strlen(name) < 3) return ESP_ERR_INVALID_ARG;
    extern esp_err_t goose_shadow_resolve(const char *name, uint32_t *out_addr, uint32_t *out_mask, reflex_tryte9_t *out_coord, goose_cell_type_t *out_type);
    uint32_t s_addr, s_mask; reflex_tryte9_t s_coord; goose_cell_type_t s_type;
    bool in_shadow = (goose_shadow_resolve(name, &s_addr, &s_mask, &s_coord, &s_type) == ESP_OK);
    bool is_protected = (strncmp(name, "sys.", 4) == 0 || strncmp(name, "agency.", 7) == 0 || in_shadow);
    for (uint32_t i = 0; i < goonies_count; i++) {
        if (strcmp(goonies_registry[i].name, name) == 0) {
            if (is_protected && !is_system_weaving) { return ESP_ERR_INVALID_STATE; }
            goonies_registry[i].coord = coord; return ESP_OK;
        }
    }
    if ((in_shadow || is_protected) && !is_system_weaving) { return ESP_ERR_NOT_SUPPORTED; }
    snprintf(goonies_registry[goonies_count].name, 24, "%s", name);
    goonies_registry[goonies_count].coord = coord;
    goonies_count++;
    return ESP_OK;
}

esp_err_t goonies_resolve(const char *name, reflex_tryte9_t *out_coord) {
    for (uint32_t i = 0; i < goonies_count; i++) {
        if (strcmp(goonies_registry[i].name, name) == 0) { *out_coord = goonies_registry[i].coord; return ESP_OK; }
    }
    return ESP_ERR_NOT_FOUND;
}

goose_cell_t* goonies_resolve_cell(const char *name) {
    if (!name) return NULL;
    reflex_tryte9_t coord;
    if (goonies_resolve(name, &coord) == ESP_OK) { return goose_fabric_get_cell_by_coord(coord); }
    if (strncmp(name, "peer.", 5) == 0) {
        extern esp_err_t goose_atmosphere_query(const char *name);
        goose_atmosphere_query(name);
        static int ghost_counter = 0;
        reflex_tryte9_t g_coord = goose_make_coord(5, 0, (int8_t)ghost_counter++);
        return goose_fabric_alloc_cell(name, g_coord, false);
    }
    extern esp_err_t goose_shadow_resolve(const char *name, uint32_t *out_addr, uint32_t *out_mask, reflex_tryte9_t *out_coord, goose_cell_type_t *out_type);
    uint32_t addr, mask; goose_cell_type_t type;
    if (goose_shadow_resolve(name, &addr, &mask, &coord, &type) == ESP_OK) {
        goose_cell_t *c = goose_fabric_alloc_cell(name, coord, true); 
        if (c) { goose_fabric_set_agency(c, addr, type); c->bit_mask = mask; return c; }
    }
    return NULL;
}

uint32_t goonies_get_count(void) { return goonies_count; }
const char* goonies_get_name_by_idx(uint32_t idx) { return (idx < goonies_count) ? goonies_registry[idx].name : NULL; }
reflex_tryte9_t goonies_get_coord_by_idx(uint32_t idx) { return (idx < goonies_count) ? goonies_registry[idx].coord : (reflex_tryte9_t){{0}}; }

#define LOOM_LOCK_TIMEOUT_CYCLES 50000

static bool is_sanctuary_address(uint32_t addr) {
    uint32_t base = addr & 0xFFFFF000;
    if (base == 0x60091000 || base == 0x60007000 || base == 0x60006000 || base == 0x60000000 || 
        base == 0x60001000 || base == 0x60004000 || base == 0x60003000 || base == 0x6000C000 || base == 0x6000E000) return false; 
    return true; 
}

static bool goose_loom_try_lock(goose_field_t *field) {
    uint32_t start = esp_cpu_get_cycle_count();
    while (__atomic_test_and_set(&loom_authority, __ATOMIC_ACQUIRE)) {
        if ((esp_cpu_get_cycle_count() - start) > LOOM_LOCK_TIMEOUT_CYCLES) {
            if (field) field->stats.lock_contention_cycles += LOOM_LOCK_TIMEOUT_CYCLES;
            ESP_LOGW(TAG, "LOOM_CONTENTION_FAULT field=%s skipping pulse",
                     field ? field->name : "?");
            return false;
        }
        esp_rom_delay_us(1);
    }
    uint32_t end = esp_cpu_get_cycle_count();
    if (field) field->stats.lock_contention_cycles += (end - start);
    return true;
}

static void goose_loom_unlock(void) { __atomic_clear(&loom_authority, __ATOMIC_RELEASE); }

esp_err_t goose_fabric_init(void) {
    lattice_stable = false; goonies_count = 0;
    bool cold_boot = (esp_sleep_get_wakeup_cause() == ESP_SLEEP_WAKEUP_UNDEFINED)
                  || (fabric_magic != GOOSE_FABRIC_MAGIC);
    if (cold_boot) {
        fabric_cell_count = 0;
        fabric_version = 0;
        loom_authority = 0;
        for (int i = 0; i < GOOSE_LATTICE_BUCKETS; i++) lattice_index[i] = -1;
        /* is_system_weaving=true already pins these cells; redundant type sets
         * here would deref NULL because lattice_stable is still false. */
        goose_fabric_alloc_cell("sys.origin", goose_make_coord(0, 0, 0), true);
        goose_fabric_alloc_cell("agency.led.intent", goose_make_coord(0, 0, 1), true);
        fabric_version = 1;
        fabric_magic = GOOSE_FABRIC_MAGIC;
    } else {
        for (int i = 0; i < GOOSE_LATTICE_BUCKETS; i++) lattice_index[i] = -1;
        for (uint32_t i = 0; i < fabric_cell_count; i++) { lattice_index[goose_lattice_hash(fabric_cells[i].coord)] = i; }
    }
    lattice_stable = true; return ESP_OK;
}

goose_cell_t* goose_fabric_get_cell_by_coord(reflex_tryte9_t coord) {
    if (!lattice_stable) return NULL; 
    uint32_t hash = goose_lattice_hash(coord);
    int16_t idx = lattice_index[hash];
    if (idx >= 0 && (uint32_t)idx < fabric_cell_count) { if (goose_coord_equal(fabric_cells[idx].coord, coord)) return &fabric_cells[idx]; }
    for (size_t i = 0; i < fabric_cell_count; i++) { if (goose_coord_equal(fabric_cells[i].coord, coord)) return &fabric_cells[i]; }
    return NULL;
}

goose_cell_t* goose_fabric_get_cell(const char *name) { return goonies_resolve_cell(name); }

static portMUX_TYPE fabric_mux = portMUX_INITIALIZER_UNLOCKED;
static portMUX_TYPE agency_mux = portMUX_INITIALIZER_UNLOCKED;
static uint32_t last_eviction_idx = 0;

goose_cell_t* goose_fabric_alloc_cell(const char *name, reflex_tryte9_t coord, bool is_system_weaving) {
    taskENTER_CRITICAL(&fabric_mux);
    if (goose_fabric_get_cell_by_coord(coord) != NULL) { taskEXIT_CRITICAL(&fabric_mux); return NULL; }
    uint32_t idx = 0;
    if (fabric_cell_count < GOOSE_FABRIC_MAX_CELLS) { idx = fabric_cell_count++; }
    else {
        bool found = false;
        for (int i = 0; i < GOOSE_FABRIC_MAX_CELLS; i++) {
            uint32_t target = (last_eviction_idx + i) % GOOSE_FABRIC_MAX_CELLS;
            if (fabric_cells[target].type != GOOSE_CELL_PINNED && fabric_cells[target].type != GOOSE_CELL_SYSTEM_ONLY) {
                for (uint32_t g = 0; g < goonies_count; g++) {
                    if (goose_coord_equal(goonies_registry[g].coord, fabric_cells[target].coord)) {
                        goonies_registry[g] = goonies_registry[--goonies_count]; break;
                    }
                }
                idx = target; last_eviction_idx = (target + 1) % GOOSE_FABRIC_MAX_CELLS; found = true; break;
            }
        }
        if (!found) { taskEXIT_CRITICAL(&fabric_mux); return NULL; }
    }
    goose_cell_t *c = &fabric_cells[idx];
    memset(c, 0, sizeof(goose_cell_t)); c->coord = coord;
    c->type = is_system_weaving ? GOOSE_CELL_PINNED : GOOSE_CELL_VIRTUAL;
    lattice_index[goose_lattice_hash(coord)] = (int16_t)idx;
    if (name) { goonies_register(name, coord, is_system_weaving); }
    fabric_version++; taskEXIT_CRITICAL(&fabric_mux); return c;
}

esp_err_t goose_fabric_set_agency(goose_cell_t *cell, uint32_t hardware_addr, goose_cell_type_t type) {
    if (!cell) return ESP_ERR_INVALID_ARG;
    if (cell->type == GOOSE_CELL_FIELD_PROXY) { return ESP_ERR_NOT_SUPPORTED; }
    if (is_sanctuary_address(hardware_addr) && type != GOOSE_CELL_SYSTEM_ONLY) { return ESP_ERR_NOT_SUPPORTED; }
    cell->hardware_addr = hardware_addr; cell->type = type; return ESP_OK;
}

static goose_field_t *global_fields[16];
static size_t global_field_count = 0;
static uint32_t goose_name_hash(const char *name) {
    uint32_t h = 0x811c9dc5;
    for (int i = 0; name[i]; i++) { h ^= (uint32_t)name[i]; h *= 0x01000193; }
    return h;
}
goose_field_t* goose_fabric_find_field_by_name_hash(uint32_t hash) {
    for (size_t f = 0; f < global_field_count; f++) { if (goose_name_hash(global_fields[f]->name) == hash) return global_fields[f]; }
    return NULL;
}

goose_cell_t* goonies_resolve_by_capability(const char *suffix) {
    if (!suffix) return NULL;
    const char* trusted_zones[] = {"agency.", "perception.", "sys."};
    for (int p = 0; p < 3; p++) {
        for (uint32_t i = 0; i < goonies_count; i++) {
            const char *name = goonies_registry[i].name;
            if (strncmp(name, trusted_zones[p], strlen(trusted_zones[p])) == 0) {
                size_t nlen = strlen(name); size_t slen = strlen(suffix);
                if (nlen >= slen && strcmp(name + (nlen - slen), suffix) == 0) { return goose_fabric_get_cell_by_coord(goonies_registry[i].coord); }
            }
        }
    }
    return NULL;
}

static goose_field_t *autonomy_field = NULL;
#define MAX_FABRICATED_ROUTES 16

esp_err_t goose_supervisor_weave_sync(void) {
    if (!autonomy_field) {
        goose_field_t *f = malloc(sizeof(goose_field_t));
        if (!f) return ESP_ERR_NO_MEM;
        memset(f, 0, sizeof(goose_field_t));
        snprintf(f->name, 16, "sys.autonomy");
        f->routes = malloc(sizeof(goose_route_t) * MAX_FABRICATED_ROUTES);
        if (!f->routes) { free(f); return ESP_ERR_NO_MEM; }
        f->rhythm = GOOSE_RHYTHM_HARMONIC;
        autonomy_field = f;
        goose_field_start_pulse(autonomy_field);
    }
    for (uint32_t i = 0; i < fabric_cell_count; i++) {
        goose_cell_t *need = &fabric_cells[i];
        if (need->type != GOOSE_CELL_NEED || need->state == 0) continue;

        /* Reverse-lookup the need's registered name so we can both gate on
         * trust and derive a capability suffix from the need's own identity. */
        const char *need_name = NULL;
        for (uint32_t g = 0; g < goonies_count; g++) {
            if (goose_coord_equal(goonies_registry[g].coord, need->coord)) {
                need_name = goonies_registry[g].name;
                break;
            }
        }
        if (!need_name) continue;
        bool is_trusted_need = (strncmp(need_name, "sys.", 4) == 0 ||
                                strncmp(need_name, "agency.", 7) == 0);
        if (!is_trusted_need) continue;

        /* Generic capability match: take everything from the last '.' of the
         * need name as the capability suffix. "sys.need.led" -> ".led". */
        const char *suffix = strrchr(need_name, '.');
        if (!suffix) continue;
        goose_cell_t *cap = goonies_resolve_by_capability(suffix);
        if (!cap) continue;

        if (autonomy_field->route_count >= MAX_FABRICATED_ROUTES) break;
        bool exists = false;
        for (size_t ridx = 0; ridx < autonomy_field->route_count; ridx++) {
            if (goose_coord_equal(autonomy_field->routes[ridx].source_coord, need->coord) &&
                goose_coord_equal(autonomy_field->routes[ridx].sink_coord, cap->coord)) {
                exists = true; break;
            }
        }
        if (exists) continue;
        goose_route_t *r = &autonomy_field->routes[autonomy_field->route_count++];
        snprintf(r->name, 16, "auto_%lu", (unsigned long)i);
        r->source_coord = need->coord;
        r->sink_coord = cap->coord;
        r->orientation = REFLEX_TRIT_POS;
        r->coupling = GOOSE_COUPLING_SOFTWARE;
        goose_apply_route(r);
    }
    return ESP_OK;
}

goose_route_t* goose_fabric_find_radio_route_by_source_coord(reflex_tryte9_t coord) {
    for (size_t f = 0; f < global_field_count; f++) {
        goose_field_t *field = global_fields[f];
        for (size_t r = 0; r < field->route_count; r++) {
            goose_route_t *route = &field->routes[r];
            if (route->coupling == GOOSE_COUPLING_RADIO && goose_coord_equal(route->source_coord, coord)) return route;
        }
    }
    return NULL;
}

reflex_tryte9_t goose_make_coord(int8_t field, int8_t region, int8_t cell) {
    reflex_tryte9_t t = {{0}};
    t.trits[0] = (reflex_trit_t)field; t.trits[3] = (reflex_trit_t)region; t.trits[6] = (reflex_trit_t)cell;
    return t;
}

bool goose_coord_equal(reflex_tryte9_t a, reflex_tryte9_t b) { for (int i = 0; i < 9; i++) { if (a.trits[i] != b.trits[i]) return false; } return true; }

#ifdef CONFIG_ULP_COPROC_ENABLED
static bool lp_heartbeat_running = false;

esp_err_t goose_lp_heartbeat_init(void) {
    if (lp_heartbeat_running) return ESP_OK;
    esp_err_t rc = ulp_lp_core_load_binary(ulp_main_bin_start,
                                           ulp_main_bin_end - ulp_main_bin_start);
    if (rc != ESP_OK) {
        ESP_LOGW(TAG, "LP heartbeat: load failed rc=0x%x", rc);
        return rc;
    }
    ulp_lp_led_intent  = 0;
    ulp_lp_pulse_count = 0;
    ulp_lp_core_cfg_t cfg = {
        .wakeup_source = ULP_LP_CORE_WAKEUP_SOURCE_LP_TIMER,
        .lp_timer_sleep_duration_us = 1000 * 1000,  /* 1 Hz */
    };
    rc = ulp_lp_core_run(&cfg);
    if (rc != ESP_OK) {
        ESP_LOGW(TAG, "LP heartbeat: run failed rc=0x%x", rc);
        return rc;
    }
    lp_heartbeat_running = true;
    ESP_LOGI(TAG, "LP heartbeat: active (LP timer 1Hz)");
    return ESP_OK;
}

void goose_lp_heartbeat_sync(void) {
    if (!lp_heartbeat_running) return;
    goose_cell_t *intent = goonies_resolve_cell("agency.led.intent");
    if (intent) ulp_lp_led_intent = (int32_t)intent->state;
}

uint32_t goose_lp_heartbeat_count(void) {
    return lp_heartbeat_running ? (uint32_t)ulp_lp_pulse_count : 0;
}
#else
esp_err_t goose_lp_heartbeat_init(void) { return ESP_ERR_NOT_SUPPORTED; }
void goose_lp_heartbeat_sync(void) {}
uint32_t goose_lp_heartbeat_count(void) { return 0; }
#endif

esp_err_t goose_apply_route(goose_route_t *route) {
    if (!route) return ESP_ERR_INVALID_ARG;
    if (route->coupling == GOOSE_COUPLING_HARDWARE) {
        if (route->cached_sink && route->cached_source && route->cached_sink->hardware_addr < GPIO_NUM_MAX) {
            esp_rom_gpio_connect_out_signal((gpio_num_t)route->cached_sink->hardware_addr, (uint32_t)route->cached_source->hardware_addr, (route->orientation == -1), false);
        }
    }
    return ESP_OK;
}

/* NEURON aggregation: ternary sum across all routes in a sub-field,
 * thresholded at strict majority. For n routes the threshold is floor(n/2)+1:
 *   n=1 -> 1, n=2 -> 2, n=3 -> 2, n=4 -> 3, n=5 -> 3
 * Returns +1 / 0 / -1 representing the aggregate posture. */
static int8_t neuron_quorum(const goose_field_t *sub_field) {
    if (!sub_field) return 0;
    int32_t sum = 0;
    int count = 0;
    for (size_t i = 0; i < sub_field->route_count; i++) {
        const goose_cell_t *src = sub_field->routes[i].cached_source;
        if (src) { sum += (int32_t)src->state; count++; }
    }
    if (count == 0) return 0;
    int32_t threshold = (count / 2) + 1;
    if (sum >= threshold) return 1;
    if (sum <= -threshold) return -1;
    return 0;
}

static esp_err_t internal_process_transitions(goose_field_t *field, int depth) {
    if (!field || depth > 3) return ESP_ERR_INVALID_STATE;
    uint64_t now = esp_timer_get_time();
    if (depth == 0 && !goose_loom_try_lock(field)) return ESP_ERR_TIMEOUT;
    for (size_t i = 0; i < field->transition_count; i++) {
        goose_transition_t *t = &field->transitions[i];
        if (!t->cached_target || t->cached_version != fabric_version) { t->cached_target = goose_fabric_get_cell_by_coord(t->target_coord); t->cached_version = fabric_version; }
        if (t->cached_target && t->evolution_fn && (now - t->last_run_us) >= (t->interval_ms * 1000)) { t->cached_target->state = t->evolution_fn(t->context); t->last_run_us = now; }
    }
    for (size_t i = 0; i < field->route_count; i++) {
        goose_route_t *r = &field->routes[i];
        if (!r->cached_source || r->cached_version != fabric_version) {
            r->cached_source = goose_fabric_get_cell_by_coord(r->source_coord);
            r->cached_sink = goose_fabric_get_cell_by_coord(r->sink_coord);
            r->cached_control = goose_fabric_get_cell_by_coord(r->control_coord);
            r->cached_version = fabric_version;
        }
        if (!r->cached_source || !r->cached_sink) continue;
        if (r->cached_source->type == GOOSE_CELL_FIELD_PROXY) {
            /* Projection: parent samples a single "success trit" from child.
             * Uses routes[0] as the designated projection route per
             * ARCHITECTURE.md §5 (Asynchronous Pulse). */
            goose_field_t *sub_field = goose_fabric_find_field_by_name_hash(r->cached_source->hardware_addr);
            if (sub_field && sub_field->route_count > 0 && sub_field->routes[0].cached_source) {
                r->cached_source->state = sub_field->routes[0].cached_source->state;
            }
        } else if (r->cached_source->type == GOOSE_CELL_NEURON) {
            /* Aggregation: multi-route ternary sum with majority threshold. */
            goose_field_t *sub_field = goose_fabric_find_field_by_name_hash(r->cached_source->hardware_addr);
            r->cached_source->state = neuron_quorum(sub_field);
        }
        if (r->coupling == GOOSE_COUPLING_SOFTWARE) {
            reflex_trit_t effective_orient = r->learned_orientation ? r->learned_orientation : r->orientation;
            reflex_trit_t control = r->cached_control ? (reflex_trit_t)r->cached_control->state : effective_orient;
            r->cached_sink->state = (int8_t)((int)r->cached_source->state * (int)control);
            if (r->cached_sink->hardware_addr > 0 && r->cached_sink->hardware_addr < GPIO_NUM_MAX) { gpio_set_level((gpio_num_t)r->cached_sink->hardware_addr, r->cached_sink->state == 1 ? 1 : 0); } 
            else if (r->cached_sink->hardware_addr >= 0x60000000) {
                taskENTER_CRITICAL(&agency_mux);
                volatile uint32_t *reg = (volatile uint32_t *)r->cached_sink->hardware_addr;
                uint32_t mask = r->cached_sink->bit_mask ? r->cached_sink->bit_mask : 0xFFFFFFFF;
                if (r->cached_sink->state == 1) *reg |= mask; else if (r->cached_sink->state == -1) *reg &= ~mask;
                taskEXIT_CRITICAL(&agency_mux);
            }
        } else if (r->coupling == GOOSE_COUPLING_RADIO) {
            extern esp_err_t goose_atmosphere_emit_arc(goose_cell_t *source);
            goose_atmosphere_emit_arc(r->cached_source);
        }
    }
    if (depth == 0) goose_loom_unlock();
    field->stats.total_pulses++; return ESP_OK;
}

esp_err_t goose_process_transitions(goose_field_t *field) { return internal_process_transitions(field, 0); }
static void goose_regional_pulse_task(void *arg) {
    goose_field_t *field = (goose_field_t *)arg; uint32_t delay_ms = 1000 / (uint32_t)field->rhythm;
    while(1) { goose_process_transitions(field); vTaskDelay(pdMS_TO_TICKS(delay_ms ? delay_ms : 1)); }
}
esp_err_t goose_field_start_pulse(goose_field_t *field) {
    if (!field) return ESP_ERR_INVALID_ARG;
    if (global_field_count < 16) { global_fields[global_field_count++] = field; }
    char task_name[16]; snprintf(task_name, sizeof(task_name), "p_%.13s", field->name);
    xTaskCreate(goose_regional_pulse_task, task_name, 4096, field, 10, NULL);
    return ESP_OK;
}
