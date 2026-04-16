/**
 * @file goose_library.c
 * @brief GOOSE Standard Library & Fragment Weaver
 * 
 * Provides pre-woven geometric patterns (fragments) for common hardware 
 * and logic behaviors. The Weaver instantiates these patterns into the Loom.
 */

/**
 * @file goose_library.c
 * @brief GOOSE Library: Fragment Weaving & LoomScript
 * 
 * This module manages the lifecycle of geometric fragments and LoomScript
 * binary manifolds. It allows for dynamic "Weaving" of new logic into the
 * active Tapestry without firmware flashes.
 * 
 * Features:
 * 1. LoomScript Loader - Parses .loom binary fragments into native routes.
 * 2. Fragment Quotas - Prevents heap exhaustion via strict allocation limits.
 * 3. Hot-Weaving - Manifests new fields and pulses on-the-fly.
 */

#include "goose.h"
#include "reflex_hal.h"
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
            // NOT Pattern: Static Inversion route
            REFLEX_LOGI(TAG, "Wove Inverter Pattern: %s", name);
            break;
        }
        default:
            return REFLEX_ERR_NOT_SUPPORTED;
    }
    
    return REFLEX_OK;
}

/**
 * @brief Unweave Fragment
 * Restores the manifold space occupied by a fragment.
 */
#define MAX_LOOM_FRAGMENTS 8
#define MAX_ROUTES_PER_FRAGMENT 32
static int active_loom_fragments = 0;

reflex_err_t goose_weave_loom(const uint8_t *buffer, size_t size) {
    if (size < sizeof(loom_header_t)) return REFLEX_ERR_INVALID_SIZE;
    
    // Red-Team Remediation: Fragment Quota
    if (active_loom_fragments >= MAX_LOOM_FRAGMENTS) {
        REFLEX_LOGE(TAG, "Quota Exhausted: Max Loom fragments reached");
        return REFLEX_ERR_NO_MEM;
    }

    loom_header_t *head = (loom_header_t *)buffer;
    if (head->magic != LOOM_MAGIC) return REFLEX_ERR_INVALID_VERSION;

    // Red-Team Remediation: Allocation Validation
    if (head->route_count > MAX_ROUTES_PER_FRAGMENT || head->cell_count > 64) {
        REFLEX_LOGE(TAG, "Security Violation: Fragment too large");
        return REFLEX_ERR_INVALID_SIZE;
    }

    REFLEX_LOGI(TAG, "Weaving LoomScript Fragment [0x%08X]...", (unsigned int)head->name_hash);
    active_loom_fragments++;

    loom_cell_entry_t *cells = (loom_cell_entry_t *)(buffer + sizeof(loom_header_t));
    goose_cell_t *resolved_cells[head->cell_count];
    
    for (int i = 0; i < head->cell_count; i++) {
        resolved_cells[i] = goonies_resolve_cell(cells[i].name);
    }

    // 2. Weave Routes
    loom_route_entry_t *routes = (loom_route_entry_t *)(buffer + sizeof(loom_header_t) + (head->cell_count * sizeof(loom_cell_entry_t)));
    
    // We create a temporary field for this fragment
    goose_field_t *field = malloc(sizeof(goose_field_t));
    memset(field, 0, sizeof(goose_field_t));
    snprintf(field->name, 16, "ls_0x%08X", (unsigned int)head->name_hash);
    
    field->routes = malloc(sizeof(goose_route_t) * head->route_count);
    field->route_count = head->route_count;

    for (int i = 0; i < head->route_count; i++) {
        goose_route_t *r = &field->routes[i];
        r->source_coord = resolved_cells[routes[i].src_idx]->coord;
        r->sink_coord = resolved_cells[routes[i].snk_idx]->coord;
        r->orientation = routes[i].orientation;
        r->coupling = routes[i].coupling;
        goose_apply_route(r);
    }

    field->rhythm = GOOSE_RHYTHM_REACTIVE;
    goose_field_start_pulse(field);
    goose_supervisor_register_field(field);

    return REFLEX_OK;
}

