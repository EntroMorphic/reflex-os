#include "goose.h"
#include "esp_log.h"

static const char *TAG = "GOOSE_ATLAS";

typedef struct {
    const char *name;
    int8_t field;
    int8_t region;
    uint32_t base_addr;
} atlas_entry_t;

static const atlas_entry_t c6_full_map[] = {
    // Perception (-1)
    {"gpio_in",  -1, -1, 0x60091000},
    {"adc",      -1, -2, 0x6000E000},
    {"pcnt",     -1, -4, 0x6000C000},
    {"temp",     -1, -6, 0x600B2000},

    // System (0)
    {"rhythm",    0,  1, 0x60005000},
    {"entropy",   0,  4, 0x600B2110},
    {"pmu",       0,  5, 0x600B1000},

    // Agency (1)
    {"gpio_out",  1,  1, 0x60091000},
    {"ledc",      1,  2, 0x60007000},
    {"rmt",       1,  3, 0x60006000},

    // Communication (2)
    {"uart0",     2,  1, 0x60000000},
    {"uart1",     2,  2, 0x60001000},
    {"i2c0",      2,  3, 0x60004000},
    {"spi2",      2,  4, 0x60003000},

    // Logic (3)
    {"gdma",      3,  1, 0x60081000},
    {"aes",       3,  2, 0x6008A000},
    {"sha",       3,  3, 0x6008B000},

    // Radio (4)
    {"wifi_6",    4,  1, 0x600B0000},
    {"ble_5",     4,  2, 0x600B1000}
};

esp_err_t goose_atlas_manifest_weave(void) {
    ESP_LOGI(TAG, "Weaving Full Peripheral Atlas (100/100)...");
    
    size_t count = sizeof(c6_full_map) / sizeof(atlas_entry_t);
    for (size_t i = 0; i < count; i++) {
        const atlas_entry_t *entry = &c6_full_map[i];
        
        // Expansion Rule: Project the 4 core registers of each peripheral
        const char *suffixes[] = {"ctrl", "stat", "data", "conf"};
        for (int j = 0; j < 4; j++) {
            char name[16];
            snprintf(name, 16, "%s_%s", entry->name, suffixes[j]);
            
            reflex_tryte9_t coord = goose_make_coord(entry->field, entry->region, (int8_t)j);
            
            goose_cell_t *c = goose_fabric_alloc_cell(name, coord);
            if (c) {
                c->hardware_addr = entry->base_addr + (j * 4); // Standard 32-bit offsets
                c->type = (entry->field == -1) ? GOOSE_CELL_HARDWARE_IN : GOOSE_CELL_HARDWARE_OUT;
                
                // Safety: Mark Logic and Power regions as System-Only
                if (entry->field == 0 || entry->field == 3) {
                    c->type = GOOSE_CELL_SYSTEM_ONLY;
                }
            }
        }
    }
    
    return ESP_OK;
}
