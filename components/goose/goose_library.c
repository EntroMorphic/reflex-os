/**
 * @file goose_library.c
 * @brief GOOSE Library: Fragment Weaving & LoomScript
 *
 * Provides pre-woven geometric patterns (fragments) for common hardware and
 * logic behaviours, and loads compiled LoomScript manifolds, so new logic can
 * be woven into the active Tapestry without a firmware flash.
 *
 * Features:
 * 1. LoomScript Loader - Parses .loom binary fragments into native routes.
 * 2. Fragment Quotas - Prevents heap exhaustion via strict allocation limits.
 * 3. Hot-Weaving - Manifests new fields and pulses on-the-fly.
 *
 * @note goose_weave_loom takes untrusted input. See the validation notes on
 *       that function before changing the parsing order.
 */

#include "goose.h"
#include "goose_policy.h"
#include "reflex_hal.h"
#include <stdlib.h> /* malloc/free — reached transitively under ESP-IDF; declared
                       * here so the file also compiles standalone. An implicit
                       * declaration of malloc returns int, which truncates the
                       * pointer on any 64-bit build. */
#include <string.h>
#include <stdio.h>

#define TAG "GOOSE_LIB"

/**
 * @brief Weave Geometric Fragment
 * Instantiates a trusted geometric pattern at the provided base coordinate.
 */
reflex_err_t goose_weave_fragment(goose_fragment_type_t type, const char *name, reflex_tryte9_t base_coord, goose_fragment_handle_t *out_handle) {
    REFLEX_LOGI(TAG, "Weaving Fragment [%s] type %d at manifold coord", name, type);
    
    if (out_handle) {
        out_handle->cell_start = 0; 
        out_handle->cell_count = 0;
        out_handle->route_start = 0;
        out_handle->route_count = 0;
    }

    switch(type) {
        case GOOSE_FRAGMENT_HEARTBEAT: {
            /**
             * Heartbeat Fragment: Autonomous Rhythm
             * 1 Virtual Cell + 1 Evolutionary Transition.
             */
            goose_cell_t *pulse_cell = goose_fabric_alloc_cell(name, base_coord, true);
            if (!pulse_cell) return REFLEX_ERR_NO_MEM;
            
            pulse_cell->type = GOOSE_CELL_VIRTUAL;
            if (out_handle) {
                out_handle->cell_count = 1;
            }
            REFLEX_LOGI(TAG, "Wove Heartbeat Pattern: %s", name);
            break;
        }
        case GOOSE_FRAGMENT_GATE: {
            /**
             * Gate Fragment: Routed Inhibition
             * 1 Intent Cell (Control) + 1 Software Route.
             */
            goose_cell_t *ctrl = goose_fabric_alloc_cell(name, base_coord, true);
            if (!ctrl) return REFLEX_ERR_NO_MEM;
            
            ctrl->type = GOOSE_CELL_INTENT;
            if (out_handle) {
                out_handle->cell_count = 1;
            }
            REFLEX_LOGI(TAG, "Wove Gate Pattern: %s", name);
            break;
        }
        case GOOSE_FRAGMENT_NOT: {
            /* Not implemented: this allocated no cell and wove no route, then
             * fell through to return REFLEX_OK — so a caller asking for an
             * inverter got "Wove Inverter Pattern" in the log, a success code,
             * and nothing woven. Unreachable today (the only caller is the
             * shell self-test, which uses HEARTBEAT and GATE), which is why it
             * has gone unnoticed rather than why it is acceptable. */
            REFLEX_LOGW(TAG, "Inverter Pattern not implemented: %s", name);
            return REFLEX_ERR_NOT_SUPPORTED;
        }
        default:
            return REFLEX_ERR_NOT_SUPPORTED;
    }
    
    return REFLEX_OK;
}

#define MAX_LOOM_FRAGMENTS 8
#define MAX_ROUTES_PER_FRAGMENT 32
#define MAX_CELLS_PER_FRAGMENT  64

/* Fragments are reboot-scoped. There is no unweave path: releasing one would
 * have to stop its pulse task, deregister it from the supervisor, and free the
 * field while other pulses may still hold pointers into it. Until that exists,
 * the counter only ever climbs, and it is incremented on success rather than
 * on entry so a rejected fragment does not consume a slot. */
static int active_loom_fragments = 0;

int goose_loom_fragment_count(void) { return active_loom_fragments; }

/**
 * @brief Weave a compiled LoomScript fragment into the Tapestry.
 *
 * `buffer` is untrusted input: it arrives from a host upload, so every
 * length, count and index in it is treated as hostile until proven otherwise.
 * The validation order matters — the whole fragment is checked before any
 * allocation happens, so a malformed fragment cannot leave a half-built field
 * registered with the supervisor.
 */
/* The policy header mirrors goose_coupling_t by value, the same reciprocal-sync
 * discipline goose_runtime.c uses for the cell types. A reordered enum would
 * otherwise silently turn the wire whitelist into a different mode. */
_Static_assert(GOOSE_COUPLING_HARDWARE == GOOSE_POLICY_COUPLING_HARDWARE, "policy/coupling drift");
_Static_assert(GOOSE_COUPLING_SOFTWARE == GOOSE_POLICY_COUPLING_SOFTWARE, "policy/coupling drift");
_Static_assert(GOOSE_COUPLING_RADIO == GOOSE_POLICY_COUPLING_RADIO, "policy/coupling drift");
_Static_assert(REFLEX_TRIT_NEG == -1 && REFLEX_TRIT_POS == 1, "policy/trit drift");

reflex_err_t goose_weave_loom(const uint8_t *buffer, size_t size) {
    if (!buffer) return REFLEX_ERR_INVALID_ARG;
    if (size < sizeof(loom_header_t)) return REFLEX_ERR_INVALID_SIZE;

    if (active_loom_fragments >= MAX_LOOM_FRAGMENTS) {
        REFLEX_LOGE(TAG, "Quota Exhausted: Max Loom fragments reached");
        return REFLEX_ERR_NO_MEM;
    }

    /* Copy the header out rather than casting the buffer: the caller gives us
     * a uint8_t*, which carries no alignment guarantee, and RISC-V will fault
     * on an unaligned word load. */
    loom_header_t head;
    memcpy(&head, buffer, sizeof(head));
    if (head.magic != LOOM_MAGIC) return REFLEX_ERR_INVALID_VERSION;

    if (head.cell_count == 0 || head.cell_count > MAX_CELLS_PER_FRAGMENT ||
        head.route_count == 0 || head.route_count > MAX_ROUTES_PER_FRAGMENT) {
        REFLEX_LOGE(TAG, "Security Violation: fragment counts out of range (cells=%u routes=%u)",
                    (unsigned)head.cell_count, (unsigned)head.route_count);
        return REFLEX_ERR_INVALID_SIZE;
    }

    /* trans_count is in the wire header and there is no transition format to
     * parse it with. Accepting the fragment anyway meant a producer could
     * declare transitions, watch the weave succeed, and never learn that the
     * behaviour it asked for was dropped on the floor. Refusing is the honest
     * answer until the format exists. */
    if (head.trans_count != 0) {
        REFLEX_LOGE(TAG, "fragment declares %u transition(s); the format is not implemented",
                    (unsigned)head.trans_count);
        return REFLEX_ERR_NOT_SUPPORTED;
    }

    /* The declared arrays must actually be present. Checking only
     * size >= sizeof(header) let a short buffer with large declared counts
     * read past the end of the caller's allocation. */
    size_t need = sizeof(loom_header_t)
                + (size_t)head.cell_count  * sizeof(loom_cell_entry_t)
                + (size_t)head.route_count * sizeof(loom_route_entry_t);
    if (size < need) {
        REFLEX_LOGE(TAG, "Security Violation: buffer %u byte(s) < declared %u",
                    (unsigned)size, (unsigned)need);
        return REFLEX_ERR_INVALID_SIZE;
    }

    const uint8_t *cell_base  = buffer + sizeof(loom_header_t);
    const uint8_t *route_base = cell_base + (size_t)head.cell_count * sizeof(loom_cell_entry_t);

    REFLEX_LOGI(TAG, "Weaving LoomScript Fragment [0x%08X]...", (unsigned int)head.name_hash);

    /* Fixed-size, not a VLA keyed on wire data. */
    goose_cell_t *resolved_cells[MAX_CELLS_PER_FRAGMENT];
    for (uint16_t i = 0; i < head.cell_count; i++) {
        loom_cell_entry_t ce;
        memcpy(&ce, cell_base + (size_t)i * sizeof(ce), sizeof(ce));
        ce.name[sizeof(ce.name) - 1] = '\0';  /* the wire name need not be terminated */
        resolved_cells[i] = goonies_resolve_cell(ce.name);
    }

    /* Validate every route up front. src_idx/snk_idx are uint16_t and arrive
     * unvalidated, so without this an index of 65535 would read far past
     * resolved_cells[] and then dereference whatever it found. */
    for (uint16_t i = 0; i < head.route_count; i++) {
        loom_route_entry_t re;
        memcpy(&re, route_base + (size_t)i * sizeof(re), sizeof(re));
        if (re.src_idx >= head.cell_count || re.snk_idx >= head.cell_count) {
            REFLEX_LOGE(TAG, "Security Violation: route %u index out of range (src=%u snk=%u cells=%u)",
                        (unsigned)i, (unsigned)re.src_idx, (unsigned)re.snk_idx,
                        (unsigned)head.cell_count);
            return REFLEX_ERR_INVALID_ARG;
        }
        if (!resolved_cells[re.src_idx] || !resolved_cells[re.snk_idx]) {
            REFLEX_LOGE(TAG, "route %u references a cell that did not resolve", (unsigned)i);
            return REFLEX_ERR_NOT_FOUND;
        }
        /* The structural fields were hardened in the 2026-08-12 pass; these two
         * payload fields were still copied through verbatim. The rule lives in
         * goose_policy.c so the host suite exercises the firmware's own
         * predicate — see goose_policy_loom_route_acceptable for why each field
         * is an effect rather than a description. */
        if (!goose_policy_loom_route_acceptable(re.orientation, re.coupling)) {
            REFLEX_LOGE(TAG, "Security Violation: route %u rejected (orientation=%d coupling=%u)",
                        (unsigned)i, (int)re.orientation, (unsigned)re.coupling);
            return REFLEX_ERR_INVALID_ARG;
        }
    }

    goose_field_t *field = malloc(sizeof(goose_field_t));
    if (!field) return REFLEX_ERR_NO_MEM;
    memset(field, 0, sizeof(goose_field_t));
    snprintf(field->name, sizeof(field->name), "ls_0x%08X", (unsigned int)head.name_hash);

    field->routes = malloc(sizeof(goose_route_t) * head.route_count);
    if (!field->routes) { free(field); return REFLEX_ERR_NO_MEM; }
    /* malloc, not calloc, previously left cached_source/cached_version/name
     * and the plasticity counters as garbage — a non-NULL cached_source with a
     * coincidentally matching cached_version is a wild pointer the pulse path
     * would happily dereference. */
    memset(field->routes, 0, sizeof(goose_route_t) * head.route_count);
    field->route_count = head.route_count;

    for (uint16_t i = 0; i < head.route_count; i++) {
        loom_route_entry_t re;
        memcpy(&re, route_base + (size_t)i * sizeof(re), sizeof(re));
        goose_route_t *r = &field->routes[i];
        snprintf(r->name, sizeof(r->name), "ls%u_%u", (unsigned)(head.name_hash & 0xFFF), (unsigned)i);
        r->source_coord = resolved_cells[re.src_idx]->coord;
        r->sink_coord   = resolved_cells[re.snk_idx]->coord;
        r->orientation = (reflex_trit_t)re.orientation; /* range-checked above */
        r->coupling = (goose_coupling_t)re.coupling;    /* whitelisted above  */
        goose_apply_route(r);
    }

    field->rhythm = GOOSE_RHYTHM_REACTIVE;
    goose_field_start_pulse(field);
    goose_supervisor_register_field(field);

    active_loom_fragments++;
    return REFLEX_OK;
}

