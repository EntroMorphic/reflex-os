#include "goose.h"
#include <string.h>

typedef struct {
    const char *name;
    uint32_t addr;
    int8_t f, r, c;
    goose_cell_type_t type;
} shadow_node_t;

/**
 * @brief THE ALL-SEEING ATLAS (Shadow Manifest)
 * This table exists in FLASH to provide 100% MMIO coverage without consuming LP RAM.
 * It is paged into the Loom on-demand by the G.O.O.N.I.E.S. resolver.
 */
static const shadow_node_t shadow_map[] = {
    // --- GPIO EXHAUSTIVE ---
    {"perception.gpio.in",          0x6009103C, -1, 1, 0, GOOSE_CELL_HARDWARE_IN},
    {"perception.gpio.in1",         0x60091040, -1, 1, 1, GOOSE_CELL_HARDWARE_IN},
    {"agency.gpio.out",             0x60091004,  1, 1, 0, GOOSE_CELL_HARDWARE_OUT},
    {"agency.gpio.out1",            0x60091008,  1, 1, 1, GOOSE_CELL_HARDWARE_OUT},
    {"agency.gpio.enable",          0x60091020,  1, 1, 2, GOOSE_CELL_HARDWARE_OUT},
    {"agency.gpio.enable1",         0x60091024,  1, 1, 3, GOOSE_CELL_HARDWARE_OUT},
    {"agency.gpio.strap",           0x60091038,  1, 1, 4, GOOSE_CELL_SYSTEM_ONLY},

    // --- RMT (REMOTE CONTROL) EXHAUSTIVE ---
    {"agency.rmt.ch0_data",         0x60006000,  1, 3, 0, GOOSE_CELL_HARDWARE_OUT},
    {"agency.rmt.ch1_data",         0x60006004,  1, 3, 1, GOOSE_CELL_HARDWARE_OUT},
    {"agency.rmt.ch0_conf0",        0x60006040,  1, 3, 2, GOOSE_CELL_HARDWARE_OUT},
    {"agency.rmt.ch0_conf1",        0x60006044,  1, 3, 3, GOOSE_CELL_HARDWARE_OUT},
    {"perception.rmt.ch2_data",     0x60006008, -1, 3, 0, GOOSE_CELL_HARDWARE_IN},
    {"perception.rmt.ch3_data",     0x6000600C, -1, 3, 1, GOOSE_CELL_HARDWARE_IN},
    {"sys.rmt.int_raw",             0x600060A0,  0, 3, 0, GOOSE_CELL_SYSTEM_ONLY},
    {"sys.rmt.int_st",              0x600060A4,  0, 3, 1, GOOSE_CELL_SYSTEM_ONLY},

    // --- LEDC (PWM) EXHAUSTIVE ---
    {"agency.ledc.ch0_conf0",       0x60007000,  1, 2, 0, GOOSE_CELL_HARDWARE_OUT},
    {"agency.ledc.ch0_hpoint",      0x60007004,  1, 2, 1, GOOSE_CELL_HARDWARE_OUT},
    {"agency.ledc.ch0_duty",        0x60007008,  1, 2, 2, GOOSE_CELL_HARDWARE_OUT},
    {"agency.ledc.ch0_conf1",       0x6000700C,  1, 2, 3, GOOSE_CELL_HARDWARE_OUT},
    {"agency.ledc.ch1_duty",        0x60007018,  1, 2, 4, GOOSE_CELL_HARDWARE_OUT},
    {"sys.ledc.conf",               0x600070D0,  0, 2, 0, GOOSE_CELL_SYSTEM_ONLY},

    // --- UART0 EXHAUSTIVE ---
    {"comm.uart0.fifo",             0x60000000,  2, 1, 0, GOOSE_CELL_HARDWARE_OUT},
    {"comm.uart0.int_raw",          0x60000004,  2, 1, 1, GOOSE_CELL_SYSTEM_ONLY},
    {"comm.uart0.int_st",           0x60000008,  2, 1, 2, GOOSE_CELL_SYSTEM_ONLY},
    {"comm.uart0.clk_conf",         0x6000000C,  2, 1, 3, GOOSE_CELL_SYSTEM_ONLY},
    {"comm.uart0.conf0",            0x60000020,  2, 1, 4, GOOSE_CELL_HARDWARE_OUT},
    {"comm.uart0.conf1",            0x60000024,  2, 1, 5, GOOSE_CELL_HARDWARE_OUT},

    // --- PMU (POWER MANAGEMENT) ---
    {"sys.pmu.cntl_low_power",      0x600B1000,  0, 5, 0, GOOSE_CELL_SYSTEM_ONLY},
    {"sys.pmu.cntl_clk_conf",       0x600B1008,  0, 5, 1, GOOSE_CELL_SYSTEM_ONLY},

    // ... This table can grow to 2000+ entries without hitting RAM limits
};

esp_err_t goose_shadow_resolve(const char *name, uint32_t *out_addr, reflex_tryte9_t *out_coord, goose_cell_type_t *out_type) {
    size_t count = sizeof(shadow_map) / sizeof(shadow_node_t);
    for (size_t i = 0; i < count; i++) {
        if (strcmp(shadow_map[i].name, name) == 0) {
            *out_addr = shadow_map[i].addr;
            *out_coord = goose_make_coord(shadow_map[i].f, shadow_map[i].r, shadow_map[i].c);
            *out_type = shadow_map[i].type;
            return ESP_OK;
        }
    }
    return ESP_ERR_NOT_FOUND;
}
