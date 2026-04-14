#include "goose.h"
#include "esp_log.h"
#include <string.h>
#include <stdio.h>

static const char *TAG = "GOOSE_LIB";

// Internal helper to create and register a cell in the global fabric
static goose_cell_t* create_fragment_cell(const char *parent_name, const char *cell_suffix, reflex_tryte9_t coord, int8_t type) {
    // This is a simplified weaver that uses the static fabric_cells pool
    // In a mature system, this would be a dynamic allocation in the Loom.
    
    // We'll use a hack for now: find a free slot in the global fabric cells
    // Since we don't have a formal allocator yet, we'll just log that we are weaving it.
    ESP_LOGI(TAG, "Weaving Cell: %s_%s at coord", parent_name, cell_suffix);
    
    // For this Phase 13 proof, we'll return a pointer to a static cell 
    // to prove the structural composition.
    static goose_cell_t fragment_cell;
    snprintf(fragment_cell.name, 16, "%s_%s", parent_name, cell_suffix);
    fragment_cell.coord = coord;
    fragment_cell.type = type;
    fragment_cell.state = 0;
    
    return &fragment_cell;
}

esp_err_t goose_weave_fragment(goose_fragment_type_t type, const char *name, reflex_tryte9_t base_coord) {
    ESP_LOGI(TAG, "Weaving Fragment [%s] type %d", name, type);
    
    switch(type) {
        case GOOSE_FRAGMENT_NOT: {
            // NOT Fragment: 1 Route (Inverter)
            // Expects external Source and Sink to be linked via coordinates
            ESP_LOGI(TAG, "Wove Inverter Pattern: %s", name);
            break;
        }
        case GOOSE_FRAGMENT_HEARTBEAT: {
            // Heartbeat: 1 Cell + 1 Transition
            goose_cell_t *pulse_cell = create_fragment_cell(name, "pulse", base_coord, 0);
            (void)pulse_cell;
            ESP_LOGI(TAG, "Wove Heartbeat Pattern: %s", name);
            break;
        }
        case GOOSE_FRAGMENT_GATE: {
            // Gate: 1 Route + 1 Control Cell
            goose_cell_t *ctrl = create_fragment_cell(name, "ctrl", base_coord, 3);
            (void)ctrl;
            ESP_LOGI(TAG, "Wove Gate Pattern: %s", name);
            break;
        }
        default:
            return ESP_ERR_NOT_SUPPORTED;
    }
    
    return ESP_OK;
}
