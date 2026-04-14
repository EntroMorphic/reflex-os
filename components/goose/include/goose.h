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
 * @brief GOOSE Cell Type
 */
typedef enum {
    GOOSE_CELL_VIRTUAL,      // Pure software state
    GOOSE_CELL_HARDWARE_IN,  // Physical Input (Observer)
    GOOSE_CELL_HARDWARE_OUT, // Physical Output (Actor)
    GOOSE_CELL_INTENT        // Logical Goal (User/Service controlled)
} goose_cell_type_t;

/**
 * @brief GOOSE Cell
 * The atomic unit of state.
 */
typedef struct {
    char name[16];
    reflex_trit_t state;
    goose_cell_type_t type;
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
    reflex_trit_t orientation;  // Static fallback
    goose_cell_t *control;     // Meta-Agency: If set, its state overrides orientation
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

// Global Fabric API
goose_cell_t* goose_fabric_get_cell(const char *name);
esp_err_t goose_fabric_process(void); // Global tapestry processing

// Supervisor / Regulator API
esp_err_t goose_supervisor_init(void);
esp_err_t goose_supervisor_register_field(goose_field_t *field);
esp_err_t goose_supervisor_check_equilibrium(goose_field_t *field);
esp_err_t goose_supervisor_rebalance(goose_field_t *field);
esp_err_t goose_supervisor_pulse(void); // Run one regulation cycle

#endif
