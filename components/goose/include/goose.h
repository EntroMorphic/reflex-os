#ifndef REFLEX_GOOSE_H
#define REFLEX_GOOSE_H

#include "reflex_ternary.h"
#include <stdint.h>
#include "esp_err.h"

/**
 * @file goose.h
 * @brief Geometric Ontologic Operating System Execution (GOOSE) Core Substrate
 * 
 * GOOSE reinterprets the machine as a coherent geometric field of state and route.
 * It replaces traditional binary software abstractions with ternary state propagation.
 */

/**
 * @brief GOOSE Coupling Mode
 * Defines the physical substrate of a geometric transition.
 */
typedef enum {
    GOOSE_COUPLING_HARDWARE, ///< Silicon-level routing (Shadow Agency: GPIO Matrix)
    GOOSE_COUPLING_SOFTWARE, ///< CPU-polled state propagation (Ternary Product Rule)
    GOOSE_COUPLING_ASYNC     ///< Interrupt-driven evolution
} goose_coupling_t;

/**
 * @brief GOOSE Cell Type
 * Defines the ontological role of a state unit.
 */
typedef enum {
    GOOSE_CELL_VIRTUAL,      ///< Pure manifold state
    GOOSE_CELL_HARDWARE_IN,  ///< Perception: Physical Input (Observer)
    GOOSE_CELL_HARDWARE_OUT, ///< Agency: Physical Output (Actor)
    GOOSE_CELL_INTENT,       ///< Direction: Logical Goal (User/Service controlled)
    GOOSE_CELL_SYSTEM_ONLY   ///< Protected: Only the Supervisor/System can access
} goose_cell_type_t;

/**
 * @brief GOOSE Tiered Rhythms
 * Defines the pulse frequency for a field manifold.
 */
typedef enum {
    GOOSE_RHYTHM_AUTONOMIC = 1,    ///< 1Hz: Persistent/Sleep-safe regulation (LP Core)
    GOOSE_RHYTHM_HARMONIC  = 10,   ///< 10Hz: General system posture (Supervisor)
    GOOSE_RHYTHM_REACTIVE  = 100,  ///< 100Hz: High-speed reactive agency
    GOOSE_RHYTHM_REALTIME  = 1000  ///< 1000Hz: Urgent hardware agency
} goose_rhythm_t;

#pragma pack(push, 1)
/**
 * @brief GOOSE Cell
 * The atomic unit of state in the machine.
 * Isomorphic to the binary layout used by the LP core pulse.
 */
typedef struct {
    char name[16];           ///< Semantic name for Tapestry identification
    reflex_tryte9_t coord;   ///< Geometric Coordinate (Field, Region, Cell)
    int8_t state;            ///< Current ternary state (-1, 0, +1)
    int8_t type;             ///< Role of the cell (goose_cell_type_t)
    uint32_t hardware_addr;  ///< Physical mapping (GPIO num or MMIO addr)
    uint32_t bit_mask;       ///< Mask for multi-bit register mappings
} goose_cell_t;
#pragma pack(pop)

/**
 * @brief GOOSE Region
 * A collection of related cells forming a functional unit (e.g. a GPIO block).
 */
typedef struct {
    char name[16];
    goose_cell_t *cells;
    size_t cell_count;
} goose_region_t;

/**
 * @brief GOOSE Route
 * A persistent structural orientation between two cells.
 * Source -> Orientation -> Sink.
 */
typedef struct {
    char name[16];
    goose_cell_t *source;
    goose_cell_t *sink;
    reflex_trit_t orientation;  ///< Static orientation (Pass, Block, Invert)
    goose_cell_t *control;      ///< Meta-Agency: Dynamic orientation overriding static
    goose_coupling_t coupling;  ///< Manifestation mode (Hardware/Software)
} goose_route_t;

/**
 * @brief GOOSE Transition
 * Defines a rule for state evolution over time (Rhythm).
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
 * A top-level manifold containing regions, routes, and transitions.
 * Pulsed at a specific Rhythm frequency.
 */
typedef struct {
    char name[16];
    goose_region_t *regions;
    size_t region_count;
    goose_route_t *routes;
    size_t route_count;
    goose_transition_t *transitions;
    size_t transition_count;
    goose_rhythm_t rhythm;       ///< Assigned tiered rhythm
} goose_field_t;

// --- Substrate API ---

/**
 * @brief Initialize the Global Fabric (The Loom) in LP RAM.
 */
esp_err_t goose_fabric_init(void);

/**
 * @brief Manifest a structural hardware route (GPIO Matrix).
 */
esp_err_t goose_apply_route(goose_route_t *route);

/**
 * @brief Evaluate one rhythmic pass of a field manifold.
 */
esp_err_t goose_process_transitions(goose_field_t *field);

/**
 * @brief Create a high-priority reactive task to pulse a field.
 */
esp_err_t goose_field_start_pulse(goose_field_t *field);

// --- Loom API ---

/**
 * @brief Initialize the GOOSE Gateway (Legacy Message Bridge).
 */
esp_err_t goose_gateway_init(void);

/**
 * @brief Find a cell in the Tapestry by its semantic name.
 */
goose_cell_t* goose_fabric_get_cell(const char *name);

/**
 * @brief Find a cell in the Tapestry by its 9-trit geometric coordinate.
 */
goose_cell_t* goose_fabric_get_cell_by_coord(reflex_tryte9_t coord);

/**
 * @brief Allocate a new spatial unit in the LP Loom.
 */
goose_cell_t* goose_fabric_alloc_cell(const char *name, reflex_tryte9_t coord);

/**
 * @brief Reserve a coordinate space in the Loom.
 */
esp_err_t goose_fabric_process(void);

// --- Fragment Weaver API ---

typedef struct {
    uint16_t cell_start;
    uint8_t cell_count;
    uint16_t route_start;
    uint8_t route_count;
} goose_fragment_handle_t;

typedef enum {
    GOOSE_FRAGMENT_HEARTBEAT, ///< Autonomous oscillation pattern
    GOOSE_FRAGMENT_NOT,       ///< Inversion pattern
    GOOSE_FRAGMENT_GATE,      ///< Rerouting/Inhibition pattern
    GOOSE_FRAGMENT_PRODUCT    ///< Intersection pattern
} goose_fragment_type_t;

/**
 * @brief Weave a pre-woven geometric fragment into the Tapestry.
 */
esp_err_t goose_weave_fragment(goose_fragment_type_t type, const char *name, reflex_tryte9_t base_coord, goose_fragment_handle_t *out_handle);

/**
 * @brief Remove a fragment from the Tapestry, restoring spatial neutral.
 */
esp_err_t goose_unweave_fragment(goose_fragment_handle_t handle);

// --- Coordinate Helpers ---

/**
 * @brief Generate a 9-trit spatial coordinate.
 */
reflex_tryte9_t goose_make_coord(int8_t field, int8_t region, int8_t cell);

/**
 * @brief Compare two geometric spatial vectors.
 */
bool goose_coord_equal(reflex_tryte9_t a, reflex_tryte9_t b);

// --- Atlas API ---

/**
 * @brief Weave the full hardware spectrum of the ESP32-C6 into the Loom.
 */
esp_err_t goose_atlas_manifest_weave(void);

#endif

