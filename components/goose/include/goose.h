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

#pragma pack(push, 1)
/**
 * @brief GOOSE Cell
 * The atomic unit of state in the machine.
 */
typedef struct {
    char name[16];           ///< Semantic name for Tapestry identification
    reflex_tryte9_t coord;   ///< Geometric Coordinate (Field, Region, Cell)
    int8_t state;            ///< Current ternary state (-1, 0, +1)
    int8_t type;             ///< Role of the cell (Intent, Hardware-In, etc.)
    uint32_t hardware_addr;  ///< Physical mapping (GPIO num or MMIO addr)
    uint32_t bit_mask;       ///< Mask for multi-bit register mappings
} goose_cell_t;
#pragma pack(pop)

/**
 * @brief GOOSE Region
 * A collection of related cells forming a functional unit.
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
    reflex_trit_t orientation;  ///< Static orientation (Pass, Block, Invert)
    goose_cell_t *control;      ///< Meta-Agency: If non-null, its state overrides orientation
    goose_coupling_t coupling;  ///< Manifestation mode (Hardware/Software)
} goose_route_t;

/**
 * @brief GOOSE Transition
 * Defines a rule for state evolution over time.
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
 * @brief GOOSE Tiered Rhythms
 * Defines the pulse frequency for a field manifold.
 */
typedef enum {
    GOOSE_RHYTHM_AUTONOMIC = 1,    ///< 1Hz: Persistent/Sleep-safe (LP Core)
    GOOSE_RHYTHM_HARMONIC  = 10,   ///< 10Hz: General system posture (Supervisor)
    GOOSE_RHYTHM_REACTIVE  = 100,  ///< 100Hz: High-speed IO (Regional Task)
    GOOSE_RHYTHM_REALTIME  = 1000  ///< 1000Hz: Urgent agency
} goose_rhythm_t;

/**
 * @brief GOOSE Field
 * A top-level manifold containing regions, routes, and transitions.
 */
typedef struct {
    char name[16];
    goose_region_t *regions;
    size_t region_count;
    goose_route_t *routes;
    size_t route_count;
    goose_transition_t *transitions;
    size_t transition_count;
    goose_rhythm_t rhythm;       ///< Assigned pulse frequency
} goose_field_t;

// API Prototypes
esp_err_t goose_fabric_init(void);
esp_err_t goose_apply_route(goose_route_t *route);
esp_err_t goose_process_transitions(goose_field_t *field);
esp_err_t goose_field_start_pulse(goose_field_t *field); // Create high-speed regional task

// Global Fabric API
goose_cell_t* goose_fabric_get_cell(const char *name);
goose_cell_t* goose_fabric_get_cell_by_coord(reflex_tryte9_t coord);
goose_cell_t* goose_fabric_alloc_cell(const char *name, reflex_tryte9_t coord);
esp_err_t goose_fabric_process(void); // Global tapestry processing

// Fragment Weaver API
typedef struct {
    uint16_t cell_start;
    uint8_t cell_count;
    uint16_t route_start;
    uint8_t route_count;
} goose_fragment_handle_t;

typedef enum {
    GOOSE_FRAGMENT_HEARTBEAT,
    GOOSE_FRAGMENT_NOT,
    GOOSE_FRAGMENT_GATE,
    GOOSE_FRAGMENT_PRODUCT
} goose_fragment_type_t;

esp_err_t goose_weave_fragment(goose_fragment_type_t type, const char *name, reflex_tryte9_t base_coord, goose_fragment_handle_t *out_handle);
esp_err_t goose_unweave_fragment(goose_fragment_handle_t handle);

// Coordinate Helpers
reflex_tryte9_t goose_make_coord(int8_t field, int8_t region, int8_t cell);
bool goose_coord_equal(reflex_tryte9_t a, reflex_tryte9_t b);

#endif
