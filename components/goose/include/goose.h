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
    GOOSE_COUPLING_ASYNC,    ///< Interrupt-driven evolution
    GOOSE_COUPLING_RADIO,    ///< Geometric Arcing: ESP-NOW state propagation
    GOOSE_COUPLING_DMA,      ///< Geometric Flow: Authority-led binary data transfer
    GOOSE_COUPLING_SILICON   ///< Silicon Agency: Sub-100ns ETM Matrix
} goose_coupling_t;

/**
 * @brief GOOSE Cell Type
 * Defines the ontological role of a state unit.
 */
typedef enum {
    GOOSE_CELL_VIRTUAL,      ///< Pure manifold state (Swappable)
    GOOSE_CELL_HARDWARE_IN,  ///< Perception: Physical Input (Pinned)
    GOOSE_CELL_HARDWARE_OUT, ///< Agency: Physical Output (Pinned)
    GOOSE_CELL_INTENT,       ///< Direction: Logical Goal (Swappable)
    GOOSE_CELL_SYSTEM_ONLY,  ///< Protected: Only the Supervisor/System (Pinned)
    GOOSE_CELL_PINNED,       ///< Explicitly pinned: Never evicted from RTC RAM
    GOOSE_CELL_FIELD_PROXY,  ///< Recursive: Projects a sub-field into a single cell
    GOOSE_CELL_NEURON,       ///< Neural: Aggregates multiple routes via Quorum
    GOOSE_CELL_NEED          ///< Autonomic: System mandate that triggers self-weaving
} goose_cell_type_t;

/**
 * @brief GOOSE Tiered Rhythms
 * Defines the pulse frequency for a field manifold.
 */
typedef enum {
    GOOSE_RHYTHM_AUTONOMIC = 1,    ///< 1Hz: Persistent/Sleep-safe regulation (LP Core)
    GOOSE_RHYTHM_DISTRIBUTED = 5,  ///< 5Hz: Geometric Arcing (Radio Pulse)
    GOOSE_RHYTHM_HARMONIC  = 10,   ///< 10Hz: General system posture (Supervisor)
    GOOSE_RHYTHM_REACTIVE  = 100,  ///< 100Hz: High-speed reactive agency
    GOOSE_RHYTHM_REALTIME  = 1000  ///< 1000Hz: Urgent hardware agency
} goose_rhythm_t;

#pragma pack(push, 1)
/**
 * @brief Compact GOOSE Cell
 * Optimized for spatial footprint in RTC RAM.
 * Identity (Name) is moved to the Flash-based Spatial Catalog.
 */
typedef struct {
    reflex_tryte9_t coord;   ///< Geometric Primary Key
    int8_t state;            ///< Current ternary state (-1, 0, +1)
    int8_t type;             ///< Role of the cell (goose_cell_type_t)
    uint32_t hardware_addr;  ///< Physical mapping (GPIO num or MMIO addr)
    uint32_t bit_mask;       ///< Mask for multi-bit register mappings
} __attribute__((aligned(4))) goose_cell_t;
#pragma pack(pop)

typedef struct {
    uint64_t total_pulses;
    uint32_t last_pulse_cycles;
    uint32_t max_pulse_cycles;
    uint32_t phase_evolution_cycles;
    uint32_t phase_propagation_cycles;
    uint32_t tlb_miss_count;
    uint32_t lock_contention_cycles;
    uint32_t lattice_collision_count;
} goose_stats_t;

/**
 * @brief GOOSE Region
 * A collection of related cells forming a functional unit.
 */
typedef struct {
    reflex_tryte9_t base_coord;
    goose_cell_t *cells;
    size_t cell_count;
} goose_region_t;

/**
 * @brief GOOSE Route
 * A persistent structural orientation between two cells.
 * Uses Geometric Coordinates to survive Spatial Paging.
 */
typedef struct {
    char name[16];
    reflex_tryte9_t source_coord;
    reflex_tryte9_t sink_coord;
    reflex_trit_t orientation;          ///< Static instinct (Fixed)
    reflex_trit_t learned_orientation;   ///< Topological plasticity (Mutable)
    reflex_tryte9_t control_coord; ///< Meta-Agency: Dynamic orientation
    goose_coupling_t coupling;  ///< Manifestation mode (Hardware/Software/Radio)

    // Geometric TLB (Spatial Cache)
    goose_cell_t *cached_source;
    goose_cell_t *cached_sink;
    goose_cell_t *cached_control;
    uint32_t cached_version;

    // Hebbian plasticity (reward-gated co-activation counter)
    int16_t hebbian_counter;
} goose_route_t;

/**
 * @brief GOOSE Transition
 * Defines a rule for state evolution over time (Rhythm).
 */
typedef struct {
    char name[16];
    reflex_tryte9_t target_coord;
    reflex_trit_t (*evolution_fn)(void *context);
    void *context;
    uint32_t interval_ms;
    uint64_t last_run_us;
    
    // Cache
    goose_cell_t *cached_target;
    uint32_t cached_version;
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
    goose_stats_t stats;         ///< Performance instrumentation
} goose_field_t;

// --- Atmosphere API (Geometric Arcing) ---

/**
 * @brief Initialize the ESP-NOW substrate for Geometric Arcing.
 */
esp_err_t goose_atmosphere_init(void);

/**
 * @brief Process atmospheric arcing (TX/RX of geometric state).
 */
esp_err_t goose_atmosphere_process(void);

// --- Stats API (Instrumentation) ---

/**
 * @brief Get the current stats for a specific field.
 */
goose_stats_t goose_field_get_stats(goose_field_t *field);

// --- DMA API (Geometric Flow) ---

/**
 * @brief Initialize the GDMA substrate.
 */
esp_err_t goose_dma_init(void);

/**
 * @brief Manifest a geometric DMA route (Authority-led flow).
 */
esp_err_t goose_dma_apply_route(goose_route_t *route);

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
 * @brief Find a radio route that expects a specific source coordinate.
 */
goose_route_t* goose_fabric_find_radio_route_by_source_coord(reflex_tryte9_t coord);

/**
 * @brief Allocate a new spatial unit in the LP Loom.
 * @param is_system_weaving If true, bypasses manual registration lockout (for shadow paging/boot).
 */
goose_cell_t* goose_fabric_alloc_cell(const char *name, reflex_tryte9_t coord, bool is_system_weaving);

/**
 * @brief Securely map a cell to physical agency (Sanctuary Guarded).
 */
esp_err_t goose_fabric_set_agency(goose_cell_t *cell, uint32_t hardware_addr, goose_cell_type_t type);

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

// --- LoomScript Binary Fragment Format (.loom) ---

#define LOOM_MAGIC 0x4D4F4F4C // "LOOM"

#pragma pack(push, 1)
typedef struct {
    uint32_t magic;
    uint32_t version;
    uint32_t name_hash;
    uint16_t cell_count;
    uint16_t route_count;
    uint16_t trans_count;
} loom_header_t;

typedef struct {
    char name[24];
} loom_cell_entry_t;

typedef struct {
    uint16_t src_idx;
    uint16_t snk_idx;
    int8_t orientation;
    uint8_t coupling;
} loom_route_entry_t;

typedef struct {
    uint16_t target_idx;
    uint16_t interval_ms;
    // TASM code snippet would go here in a full impl
} loom_trans_entry_t;
#pragma pack(pop)

/**
 * @brief Weave a compiled LoomScript fragment into the Tapestry.
 */
esp_err_t goose_weave_loom(const uint8_t *buffer, size_t size);

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

// --- G.O.O.N.I.E.S. API (Geometric Object Oriented Name Identification Execution Service) ---

/**
 * @brief Register a human-readable name for a geometric coordinate.
 */
esp_err_t goonies_register(const char *name, reflex_tryte9_t coord, bool is_system_weaving);

/**
 * @brief Resolve a name to its 9-trit coordinate.
 */
esp_err_t goonies_resolve(const char *name, reflex_tryte9_t *out_coord);

/**
 * @brief Resolve a name. If not in the active Loom, attempt to pull from the Shadow Atlas.
 */
goose_cell_t* goonies_resolve_cell(const char *name);

/**
 * @brief Manually page-in a hardware node from the Shadow Atlas.
 */
esp_err_t goonies_page_in(const char *name);

// --- Supervisor API ---

/**
 * @brief Initialize the Harmonic Supervisor.
 */
esp_err_t goose_supervisor_init(void);

/**
 * @brief Register a field for harmonic regulation.
 */
esp_err_t goose_supervisor_register_field(goose_field_t *field);

/**
 * @brief Perform a single regulation pulse.
 */
esp_err_t goose_supervisor_pulse(void);

// --- Atmospheric API (Distributed Mesh) ---

/**
 * @brief Initialize the distributed atmospheric substrate.
 */
esp_err_t goose_atmosphere_init(void);

/**
 * @brief Emit a geometric discovery query to the mesh.
 */
esp_err_t goose_atmosphere_query(const char *name);

/**
 * @brief Emit a swarm-wide postural consensus arc.
 */
esp_err_t goose_atmosphere_emit_posture(int8_t state, uint8_t weight);

/**
 * @brief Provision the Aura HMAC key into NVS. 16-byte key.
 */
esp_err_t goose_atmosphere_set_key(const uint8_t key[16]);

#endif

