/**
 * @file goose_atlas.c
 * @brief GOOSE Atlas: Hardware Ontological Mapping
 * 
 * This module defines the "Root Zone" of the machine territory. It weaves
 * primary peripherals into the Loom at boot and provides the semantic
 * framework used by the Shadow Paging resolver to unmask the full MMIO.
 */

#include "goose.h"
#include "reflex_hal.h"
#include <stdio.h>
#include <string.h>

#define TAG "GOOSE_ATLAS"

typedef struct {
    const char *zone;
    const char *subzone;
    uint32_t base_addr;
} atlas_node_t;

/**
 * @brief The True Atlas: Full MMIO Projection of ESP32-C6
 */
static const atlas_node_t c6_atlas[] = {
    // --- PERCEPTION (-1) ---
    {"perception", "gpio_in",  0x60091000},
    {"perception", "adc",      0x6000E000},
    {"perception", "pcnt",     0x6000C000},
    {"perception", "temp",     0x600B2000},
    {"perception", "systimer", 0x60005000},

    // --- SYSTEM (0) ---
    {"sys", "pcr",     0x60005000}, // Clock/Reset
    {"sys", "pmu",     0x600B1000}, // Power Management
    {"sys", "mmu",     0x600C5000}, // Memory Management
    {"sys", "entropy", 0x600B2110}, 
    {"sys", "lp_sys",  0x600B2000},

    // --- AGENCY (1) ---
    {"agency", "gpio_out", 0x60091000},
    {"agency", "ledc",     0x60007000},
    {"agency", "rmt",      0x60006000},
    {"agency", "mcpwm",    0x60009000},
    {"agency", "twai",     0x6000B000}, // CAN bus

    // --- COMMUNICATION (2) ---
    {"comm", "uart0", 0x60000000},
    {"comm", "uart1", 0x60001000},
    {"comm", "i2c0",  0x60004000},
    {"comm", "spi2",  0x60003000},
    {"comm", "usb",   0x60008000},

    // --- LOGIC (3) ---
    {"logic", "gdma",  0x60081000},
    {"logic", "aes",   0x6008A000},
    {"logic", "sha",   0x6008B000},
    {"logic", "intr",  0x60010000}, // Interrupt Matrix

    // --- RADIO (4) ---
    {"radio", "wifi", 0x600B0000},
    {"radio", "ble",  0x600B1000}
};

reflex_err_t goose_atlas_manifest_weave(void) {
    REFLEX_LOGI(TAG, "Weaving True Atlas into the Loom...");
    
    size_t node_count = sizeof(c6_atlas) / sizeof(atlas_node_t);
    const char *reg_names[] = {"ctrl", "stat", "data", "conf"};

    for (size_t i = 0; i < node_count; i++) {
        const atlas_node_t *node = &c6_atlas[i];
        
        for (int r = 0; r < 4; r++) {
            char gname[32];
            // Format: zone.peripheral.register (e.g. agency.rmt.conf)
            snprintf(gname, sizeof(gname), "%s.%s.%s", node->zone, node->subzone, reg_names[r]);
            
            // Generate a deterministic coordinate based on index.
            // Offset field by +5 to avoid collision with seed cells at
            // (0,0,0) sys.origin, (0,0,1) agency.led.intent, (0,0,2) sys.purpose.
            reflex_tryte9_t coord = goose_make_coord((int8_t)(i + 5), (int8_t)r, 0);
            
            goose_cell_t *c = goose_fabric_alloc_cell(gname, coord, true);
            if (c) {
                goose_cell_type_t type = GOOSE_CELL_VIRTUAL;
                
                // Determine ontological type from zone
                if (strcmp(node->zone, "perception") == 0) type = GOOSE_CELL_HARDWARE_IN;
                else if (strcmp(node->zone, "agency") == 0) type = GOOSE_CELL_HARDWARE_OUT;
                else if (strcmp(node->zone, "sys") == 0 || strcmp(node->zone, "logic") == 0) {
                    type = GOOSE_CELL_SYSTEM_ONLY;
                } else if (strcmp(node->zone, "comm") == 0) {
                    type = GOOSE_CELL_HARDWARE_OUT;  // bidirectional peripherals (UART, I2C, SPI, USB)
                } else if (strcmp(node->zone, "radio") == 0) {
                    type = GOOSE_CELL_SYSTEM_ONLY;   // protected radio hardware (WiFi, BLE)
                }

                // Map to physical agency (Sanctuary Guarded)
                goose_fabric_set_agency(c, node->base_addr + (r * 4), type);
            }
        }
    }
    
    REFLEX_LOGI(TAG, "True Atlas Weaving Complete. [%zu] nodes projected.", node_count * 4);
    return REFLEX_OK;
}
