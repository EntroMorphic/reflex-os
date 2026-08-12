/**
 * @file goose_metabolic.c
 * @brief Metabolic Regulation — vital perception + circuit breaker + resource governance.
 *
 * Layer 1 (circuit breaker): sys.metabolic cell. Instant degradation, hysteretic recovery.
 * Layer 2 (resource governance): per-vital reads in supervisor sub-passes (external).
 *
 * Vital cells hold ternary state written by this module; they are not bound to
 * any MMIO address. They stay GOOSE_CELL_PINNED (the type goose_fabric_alloc_cell
 * gives a system weave) rather than GOOSE_CELL_HARDWARE_IN: binding them would
 * mean calling goose_fabric_set_agency with hardware_addr 0, which the Sanctuary
 * Guard correctly rejects because page 0 is not on its whitelist. Both types are
 * eviction-exempt, and internal_process_transitions skips a HARDWARE_IN cell with
 * a zero address anyway, so nothing is lost — but an earlier revision made those
 * set_agency calls and discarded the failure, so the header claimed HARDWARE_IN
 * while `goonies find` reported type=5.
 *
 * Battery defaults to +1 on USB-powered boards (no ADC variance detected).
 */

#include "goose_metabolic.h"
#include "goose_telemetry.h"
#include "reflex_hal.h"
#include "reflex_tuning.h"
#include <stdio.h>
#include <string.h>

#ifndef REFLEX_HOST_BUILD
#include "esp_heap_caps.h"
#endif

#define TAG "GOOSE_METABOLIC"

/* --- Vital cells --- */

static goose_cell_t *s_temp_cell   = NULL;  /* perception.temp.reading (existing, looked up) */
static goose_cell_t *s_batt_cell   = NULL;  /* perception.power.battery */
static goose_cell_t *s_mesh_cell   = NULL;  /* perception.mesh.health */
static goose_cell_t *s_heap_cell   = NULL;  /* perception.heap.pressure */
static goose_cell_t *s_metabolic   = NULL;  /* sys.metabolic */

/* --- Override system --- */

static bool s_override_temp = false;
static bool s_override_batt = false;
static bool s_override_mesh = false;
static bool s_override_heap = false;
static int8_t s_override_temp_val = 0;
static int8_t s_override_batt_val = 0;
static int8_t s_override_mesh_val = 0;
static int8_t s_override_heap_val = 0;

/* --- Mesh health tracking --- */

static uint32_t s_last_rx_total = 0;
static uint32_t s_mesh_idle_ticks = 0;
/* Rolling arc count over one isolation window, plus the previous completed
 * window, so "connected" is judged over a beacon period rather than a scan. */
static uint32_t s_mesh_window_arcs = 0;
static uint32_t s_mesh_window_ticks = 0;
static uint32_t s_mesh_window_last = 0;

/* --- Circuit breaker state --- */

static int8_t s_metabolic_state = 1;  /* +1 thriving */
static uint32_t s_recovery_ticks = 0;

/* --- Helpers --- */

static int8_t compute_heap_state(void) {
#ifndef REFLEX_HOST_BUILD
    /* MALLOC_CAP_DEFAULT is the capability malloc() itself draws against.
     * Passing 0 would mean "no capability required", which sums every region
     * — including DMA-only and RTC memory that malloc() can never serve — and
     * so over-reports free space, desensitising the circuit breaker that
     * goose_fabric_alloc_cell relies on to refuse allocations under pressure. */
    size_t free_bytes = heap_caps_get_free_size(MALLOC_CAP_DEFAULT);
#else
    size_t free_bytes = 65536;
#endif
    if (free_bytes < REFLEX_METABOLIC_HEAP_CRITICAL) return -1;
    if (free_bytes < REFLEX_METABOLIC_HEAP_TIGHT)    return  0;
    return 1;
}

static int8_t compute_mesh_state(void) {
    goose_mesh_stats_t stats = goose_atmosphere_get_stats();
    /* Every authenticated inbound arc op counts as evidence of a live mesh.
     * rx_query is included deliberately: a peer that only ever QUERYs us is
     * still a peer, and omitting it made a QUERY-only neighbourhood read as
     * isolation — which then halves the discovery interval via the inverted
     * governance in goose_supervisor_pulse, hunting harder for peers we could
     * already hear. */
    uint32_t rx_total = stats.rx_sync + stats.rx_query + stats.rx_discover +
                        stats.rx_advertise + stats.rx_posture + stats.rx_mmio_sync;
    uint32_t delta = rx_total - s_last_rx_total;
    s_last_rx_total = rx_total;

    if (delta == 0) {
        s_mesh_idle_ticks++;
    } else {
        s_mesh_idle_ticks = 0;
    }

    /* Judge "connected" over a window, not a single scan.
     *
     * The threshold used to be compared against `delta` — arcs seen in one
     * ~1.1s vital scan — while a quiet mesh only produces a beacon every
     * ~10.1s. Reading +1 therefore needed ~4.5 arcs/sec against an actual
     * ~0.1, about 46x short, so `perception.mesh.health` was a ternary cell
     * that could only ever hold two of its three values and the
     * "connected: relax discovery" branch in goose_supervisor_pulse never ran.
     *
     * Counting arcs across the same window used for isolation makes all three
     * states reachable and stops the vital flickering between beacons:
     *
     *   +1 connected  traffic from at least SPARSE_THRESHOLD arcs this window
     *    0 sparse     some silence, but not yet isolated
     *   -1 isolated   nothing heard for two whole beacon periods
     */
    s_mesh_window_arcs += delta;
    if (++s_mesh_window_ticks >= REFLEX_METABOLIC_MESH_ISOLATED_TICKS) {
        s_mesh_window_last = s_mesh_window_arcs;
        s_mesh_window_arcs = 0;
        s_mesh_window_ticks = 0;
    }

    if (s_mesh_idle_ticks >= REFLEX_METABOLIC_MESH_ISOLATED_TICKS) return -1;
    /* Count the window in progress as well as the last completed one, so a
     * board that has just heard a beacon reports connected immediately rather
     * than waiting out the remainder of the window. */
    if ((s_mesh_window_arcs + s_mesh_window_last) >= REFLEX_METABOLIC_MESH_SPARSE_THRESHOLD) return 1;
    return 0;
}

static int8_t compute_circuit_breaker(int8_t temp, int8_t batt, int8_t mesh, int8_t heap) {
    (void)mesh;  /* Mesh is a connectivity issue, not a resource constraint.
                  * It's handled by discover_sync's inverted governance, not
                  * by the circuit breaker. See LMM Phase 3 resolution. */
    /* Hard constraints: battery and heap. */
    int hard_critical = (batt == -1) + (heap == -1);
    /* Soft constraints: temperature only. */
    int resource_stressed = (temp == -1) + (batt == -1) + (heap == -1);

    int8_t raw;
    if (hard_critical >= 1 || resource_stressed >= 2) raw = -1;
    else if (resource_stressed >= 1)                   raw =  0;
    else                                               raw =  1;

    /* Degradation: instant. Recovery: hysteretic. */
    if (raw < s_metabolic_state) {
        /* Dropping — immediate. */
        s_recovery_ticks = 0;
        s_metabolic_state = raw;
    } else if (raw > s_metabolic_state) {
        /* Improving — require sustained stability. */
        s_recovery_ticks++;
        if (s_recovery_ticks >= REFLEX_METABOLIC_RECOVERY_TICKS) {
            s_metabolic_state = (int8_t)(s_metabolic_state + 1);  /* step up by one level */
            s_recovery_ticks = 0;
        }
    } else {
        s_recovery_ticks = 0;
    }

    return s_metabolic_state;
}

/* --- Public API --- */

reflex_err_t goose_metabolic_init(void) {
    /* Look up the existing temp cell (registered by temp_service). */
    s_temp_cell = goonies_resolve_cell("perception.temp.reading");

    /* Register new vital cells. */
    reflex_tryte9_t coord_batt = goose_make_coord(-1, 4, 1);
    s_batt_cell = goose_fabric_ensure_cell("perception.power.battery", coord_batt, true);
    if (s_batt_cell) {
        s_batt_cell->state = 1;  /* default: full (USB-powered) */
    }

    reflex_tryte9_t coord_mesh = goose_make_coord(-1, 4, 2);
    s_mesh_cell = goose_fabric_ensure_cell("perception.mesh.health", coord_mesh, true);
    if (s_mesh_cell) {
        s_mesh_cell->state = 0;  /* default: unknown/sparse */
    }

    reflex_tryte9_t coord_heap = goose_make_coord(-1, 4, -1);
    s_heap_cell = goose_fabric_ensure_cell("perception.heap.pressure", coord_heap, true);
    if (s_heap_cell) {
        s_heap_cell->state = 1;  /* default: comfortable */
    }

    reflex_tryte9_t coord_meta = goose_make_coord(0, 0, 3);
    s_metabolic = goose_fabric_ensure_cell("sys.metabolic", coord_meta, true);
    if (s_metabolic) {
        s_metabolic->type = GOOSE_CELL_SYSTEM_ONLY;
        s_metabolic->state = 1;  /* thriving */
    }

    s_metabolic_state = 1;
    return REFLEX_OK;
}

reflex_err_t goose_metabolic_sync(void) {
    /* Update vitals (unless overridden). */
    if (!s_override_heap && s_heap_cell) {
        s_heap_cell->state = compute_heap_state();
    }
    if (!s_override_mesh && s_mesh_cell) {
        s_mesh_cell->state = compute_mesh_state();
    }
    /* Battery: stays at default (+1) for USB-powered boards. Future ADC path here. */
    /* Temperature: updated by temp_service at its own cadence. */

    /* Read vitals. Overrides take precedence (for bench testing). */
    int8_t temp = s_override_temp ? s_override_temp_val : (s_temp_cell ? s_temp_cell->state : 0);
    int8_t batt = s_override_batt ? s_override_batt_val : (s_batt_cell ? s_batt_cell->state : 1);
    int8_t mesh = s_override_mesh ? s_override_mesh_val : (s_mesh_cell ? s_mesh_cell->state : 0);
    int8_t heap = s_override_heap ? s_override_heap_val : (s_heap_cell ? s_heap_cell->state : 1);

    /* Compute circuit breaker. */
    int8_t state = compute_circuit_breaker(temp, batt, mesh, heap);
    if (s_metabolic) s_metabolic->state = state;

    /* Telemetry. */
    TELEM_IF(goose_telem_metabolic(state, temp, batt, mesh, heap));

    return REFLEX_OK;
}

int8_t goose_metabolic_get_state(void) {
    return s_metabolic_state;
}

reflex_err_t goose_metabolic_override(const char *vital, int8_t state) {
    if (!vital) return REFLEX_ERR_INVALID_ARG;
    if (state < -1 || state > 1) return REFLEX_ERR_INVALID_ARG;

    if (strcmp(vital, "temp") == 0) {
        s_override_temp = true; s_override_temp_val = state;
    } else if (strcmp(vital, "battery") == 0) {
        s_override_batt = true; s_override_batt_val = state;
    } else if (strcmp(vital, "mesh") == 0) {
        s_override_mesh = true; s_override_mesh_val = state;
    } else if (strcmp(vital, "heap") == 0) {
        s_override_heap = true; s_override_heap_val = state;
    } else {
        return REFLEX_ERR_INVALID_ARG;
    }
    return REFLEX_OK;
}

void goose_metabolic_clear_overrides(void) {
    s_override_temp = false;
    s_override_batt = false;
    s_override_mesh = false;
    s_override_heap = false;
    /* Reset circuit breaker to thriving and clear hysteresis.
     * This is an explicit operator action, not organic recovery. */
    s_metabolic_state = 1;
    s_recovery_ticks = 0;
    if (s_metabolic) s_metabolic->state = 1;
}

void goose_metabolic_format_vitals(char *buf, size_t buf_len) {
    const char *state_name;
    switch (s_metabolic_state) {
        case  1: state_name = "thriving";   break;
        case  0: state_name = "conserving"; break;
        case -1: state_name = "surviving";  break;
        default: state_name = "unknown";    break;
    }
    int8_t temp = s_override_temp ? s_override_temp_val : (s_temp_cell ? s_temp_cell->state : 0);
    int8_t batt = s_override_batt ? s_override_batt_val : (s_batt_cell ? s_batt_cell->state : 1);
    int8_t mesh = s_override_mesh ? s_override_mesh_val : (s_mesh_cell ? s_mesh_cell->state : 0);
    int8_t heap = s_override_heap ? s_override_heap_val : (s_heap_cell ? s_heap_cell->state : 1);

    snprintf(buf, buf_len,
             "temp=%d battery=%d mesh=%d heap=%d metabolic=%s(%d)",
             (int)temp, (int)batt, (int)mesh, (int)heap, state_name, (int)s_metabolic_state);
}
