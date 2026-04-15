#include "reflex_fabric.h"
#include "goose.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>
#include <stdio.h>

static const char *TAG = "GOOSE_GATEWAY";

/**
 * @brief GOOSE Gateway Task
 * Bridges legacy messaging to the GOOSE Loom.
 */
static void goose_gateway_task(void *arg) {
    ESP_LOGI(TAG, "Gateway active. Listening for MMIO projections...");
    
    while(1) {
        reflex_message_t msg;
        if (reflex_fabric_recv(REFLEX_NODE_GATEWAY, &msg) == ESP_OK) {
            // Op 0x10: Project MMIO Cell
            if (msg.op == 0x10) {
                char name[16];
                snprintf(name, 16, "mmio_%04x", (unsigned int)msg.correlation_id);
                
                // Extract coordinate from payload (Tryte 9)
                reflex_tryte9_t coord;
                memcpy(coord.trits, msg.payload.trits, 9);
                
                // Extract Address from msg context (using correlation_id for addr)
                uint32_t addr = msg.correlation_id; 

                /**
                 * Security & Safety Guard
                 * 1. 32-bit Alignment (Avoid Load/Store exceptions)
                 * 2. Range Validation (Restricted to Peripheral MMIO space)
                 */
                if ((addr % 4) != 0) {
                    ESP_LOGE(TAG, "Projection rejected: Addr 0x%lx is not 32-bit aligned!", (unsigned long)addr);
                    continue;
                }

                if (addr < 0x60000000 || addr > 0x600F0000) { // C6 Peripheral Space
                    ESP_LOGE(TAG, "Projection rejected: Addr 0x%lx is out of safe peripheral range!", (unsigned long)addr);
                    continue;
                }

                ESP_LOGI(TAG, "Projecting MMIO: %s @ (F:%d, R:%d, C:%d) -> Addr 0x%lx", 
                         name, coord.trits[0], coord.trits[3], coord.trits[6], (unsigned long)addr);

                // Note: is_system_weaving = false for external projections
                // They MUST pass G.O.O.N.I.E.S. name checks.
                goose_cell_t *c = goose_fabric_alloc_cell(name, coord, false);
                if (c) {
                    goose_fabric_set_agency(c, addr, GOOSE_CELL_HARDWARE_OUT);
                }
            }
        }
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

esp_err_t goose_gateway_init(void) {
    xTaskCreate(goose_gateway_task, "goose_gateway", 4096, NULL, 5, NULL);
    return ESP_OK;
}
