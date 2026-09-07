/**
 * @file goose_policy.h
 * @brief Pure substrate decisions, free of platform dependencies.
 *
 * These are the rules the substrate reasons with — what may be reclaimed, what
 * stance to take toward a task, whether the mesh is alive — separated from the
 * machinery that applies them. Nothing here touches hardware, ESP-IDF, RTC
 * memory or locks, so the firmware and the host test suite compile the very
 * same code rather than a test-side re-implementation of the spec.
 *
 * That distinction matters for this file in particular. Every rule below was
 * wrong at least once, and in each case the error was a state that could never
 * occur or a decision made on the wrong signal — the kind of bug a compiler and
 * a spec-mirroring test both wave through. Testing the real function is the
 * only version of coverage worth having here.
 */
#ifndef GOOSE_POLICY_H
#define GOOSE_POLICY_H

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

/* ---- Cell types ---------------------------------------------------------
 *
 * Mirrored from goose_cell_type_t. goose_runtime.c carries _Static_asserts
 * binding each of these to the enum, so the two cannot drift silently — the
 * same reciprocal-sync discipline used between tools/goose_scraper.py and the
 * shadow_node_t layout. */
#define GOOSE_POLICY_TYPE_VIRTUAL       0
#define GOOSE_POLICY_TYPE_HARDWARE_IN   1
#define GOOSE_POLICY_TYPE_HARDWARE_OUT  2
#define GOOSE_POLICY_TYPE_INTENT        3
#define GOOSE_POLICY_TYPE_SYSTEM_ONLY   4
#define GOOSE_POLICY_TYPE_PINNED        5
#define GOOSE_POLICY_TYPE_FIELD_PROXY   6
#define GOOSE_POLICY_TYPE_NEURON        7
#define GOOSE_POLICY_TYPE_NEED          8
#define GOOSE_POLICY_TYPE_PURPOSE       9

/* ---- Coordinate namespaces (trits[8]) -----------------------------------
 *
 * The namespace records *why* a cell exists, and therefore how long it lives.
 * Cell type describes access control and provenance, which is a different
 * question — conflating the two is what pinned the Loom at 256 cells. */
#define GOOSE_POLICY_NS_IDENTITY   0   /* seeds and the boot atlas weave */
#define GOOSE_POLICY_NS_SHADOW     1   /* on-demand paged atlas entries   */
#define GOOSE_POLICY_NS_PEER     (-1)  /* transient peer phantoms         */

/**
 * @brief May a cell be reclaimed when the Loom is full?
 *
 * @param cell_type       one of GOOSE_POLICY_TYPE_*
 * @param namespace_trit  coord.trits[8], one of GOOSE_POLICY_NS_*
 */
bool goose_policy_cell_evictable(int cell_type, int namespace_trit);

/* ---- Ternary scheduling disposition -------------------------------------- */
#define GOOSE_POLICY_ENGAGED    1   /* serves the declared purpose            */
#define GOOSE_POLICY_LATENT     0   /* undecided — NOT the same as suppressed */
#define GOOSE_POLICY_WITHHELD (-1)  /* deliberately held back                 */

/**
 * @brief The substrate's stance toward one supervised field.
 *
 * Decided before any scheduler priority exists; the integer handed to the host
 * RTOS is derived from this, never the reverse.
 */
int goose_policy_disposition(bool purpose_active, bool has_domain_route,
                             bool in_any_holon, bool in_active_holon,
                             bool pained);

/**
 * @brief Aggregate stance across all fields, by precedence.
 *
 * Not unanimity: requiring every field to be withheld made -1 unreachable.
 */
int goose_policy_aggregate(int engaged_count, int withheld_count);

/* ---- Mesh health windowing ----------------------------------------------- */

/** Rolling arc counts. Zero-initialise before first use. */
typedef struct {
    uint32_t idle_ticks;    /**< consecutive scans with no new arcs        */
    uint32_t window_arcs;   /**< arcs seen in the window in progress       */
    uint32_t window_ticks;  /**< scans elapsed in the window in progress   */
    uint32_t window_last;   /**< arcs counted in the last complete window  */
} goose_mesh_window_t;

/**
 * @brief Advance the mesh-health window by one vital scan.
 *
 * @param delta             new arcs since the previous scan
 * @param isolated_ticks    scans of silence before declaring isolation; also
 *                          the window length. Callers must pass >= 2 — see
 *                          REFLEX_METABOLIC_MESH_ISOLATED_TICKS, which floors
 *                          it because the derived value can otherwise reach 0.
 * @param sparse_threshold  arcs across the observed span required for +1
 * @return +1 connected, 0 sparse, -1 isolated
 */
int goose_policy_mesh_state(goose_mesh_window_t *w, uint32_t delta,
                            uint32_t isolated_ticks, uint32_t sparse_threshold);

/* ---- Snapshot serialisation ---------------------------------------------- */

/**
 * @brief How many route entries a snapshot should declare and write.
 *
 * The header must record entries actually written, never the field's declared
 * route_count; recording the latter described a payload that was not there.
 */
uint16_t goose_policy_snap_entry_count(size_t route_count, size_t max_routes);

/* ---- Mesh replay-cache slot ----------------------------------------------- */

/**
 * @brief Direct-mapped replay-cache slot for a (sender MAC, nonce) pair.
 *
 * The slot must depend on the *whole* pair. The previous in-place version
 * blended the trailing MAC bytes in at bit positions 8-23 and then masked the
 * low 6 bits, so every sender hashed to exactly `nonce & 63` and the MAC had
 * no effect at all. Nonces are derived from a monotonic microsecond clock, so
 * a second peer whose uptime happens to lead by a multiple of the slot count —
 * or any key-holder choosing a nonce — evicts a victim's entry and reopens the
 * replay window that guard exists to close.
 *
 * Every input bit is therefore mixed down into the low bits the caller uses.
 *
 * @param nonce       packet nonce
 * @param mac         six-byte sender address
 * @param slot_count  cache size; any value, need not be a power of two
 * @return slot index in [0, slot_count), or 0 if slot_count is 0
 */
uint32_t goose_policy_replay_slot(uint32_t nonce, const uint8_t *mac, uint32_t slot_count);

/* ---- LoomScript fragment acceptance -------------------------------------- */

/* Mirrored from goose_coupling_t. Only SOFTWARE is reachable from the wire;
 * the rest stay available to C callers. */
#define GOOSE_POLICY_COUPLING_HARDWARE 0
#define GOOSE_POLICY_COUPLING_SOFTWARE 1
#define GOOSE_POLICY_COUPLING_RADIO 3

/**
 * @brief May a wire-supplied route carry this orientation and coupling?
 *
 * goose_weave_loom hardened its *structural* fields — counts, indices, buffer
 * extents — in the 2026-08-12 pass, but copied these two payload fields
 * straight through. Both are effects, not descriptions:
 *
 * - orientation lands in a reflex_trit_t and is used as a multiplicand
 *   (sink = source->state * control), so a value outside {-1,0,+1} produces
 *   cell states outside the ternary set and carries them downstream.
 * - coupling selects a manifestation mode. RADIO makes every pulse of a 100 Hz
 *   REACTIVE field emit a mesh arc — a permanent transmit loop woven by one
 *   upload. HARDWARE routes silicon through the GPIO matrix. Neither is
 *   something a fragment should be able to ask for.
 *
 * @param orientation  wire value, any int8_t
 * @param coupling     wire value, any uint8_t
 */
bool goose_policy_loom_route_acceptable(int orientation, unsigned coupling);

#endif /* GOOSE_POLICY_H */
