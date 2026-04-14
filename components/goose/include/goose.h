#ifndef REFLEX_GOOSE_H
#define REFLEX_GOOSE_H

#include "reflex_ternary.h"
#include <stdint.h>
#include "esp_err.h"

/**
 * @brief GOOSE Coupling Mode
 * Defines how a transition or route is physically manifested.
 */
typedef enum {
    GOOSE_COUPLING_HARDWARE, // Silicon-level (GPIO Matrix, Peripheral routing)
    GOOSE_COUPLING_SOFTWARE, // CPU-polled (Shell/Runtime loop)
    GOOSE_COUPLING_ASYNC     // Interrupt-driven
} goose_coupling_t;

/**
 * @brief GOOSE Cell
 * The atomic unit of state.
 */
typedef struct {
    char name[16];
    reflex_trit_t state;
    uintptr_t hardware_addr; // MMIO address or GPIO number
    uint32_t bit_mask;       // For multi-bit mappings
} goose_cell_t;

/**
 * @brief GOOSE Region
 * A collection of related cells.
 */
typedef struct {
    char name[16];
    goose_cell_t *cells;
    size_t cell_count;
} goose_region_t;

/**
 * @brief GOOSE Route
 * A persistent structural orientation between two cells.
 */
typedef struct {
    char name[16];
    goose_cell_t *source;
    goose_cell_t *sink;
    reflex_trit_t orientation; // +1: Pass, 0: Block, -1: Invert
    goose_coupling_t coupling;
} goose_route_t;

/**
 * @brief GOOSE Transition
 * Defines a rule for state evolution.
 */
typedef struct {
    char name[16];
    goose_cell_t *target;
    reflex_trit_t (*evolution_fn)(void *context);
    void *context;
    uint32_t interval_ms;
    uint64_t last_run_us;
} goose_transition_t;

/**
 * @brief GOOSE Field
 * A top-level container for regions and their interactions.
 */
typedef struct {
    char name[16];
    goose_region_t *regions;
    size_t region_count;
    goose_route_t *routes;
    size_t route_count;
    goose_transition_t *transitions;
    size_t transition_count;
} goose_field_t;

// API Prototypes
esp_err_t goose_fabric_init(void);
esp_err_t goose_apply_route(goose_route_t *route);
esp_err_t goose_process_transitions(goose_field_t *field);

#endif
