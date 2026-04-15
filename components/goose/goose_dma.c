#include "goose.h"
#include "esp_gdma.h"
#include "esp_log.h"
#include <string.h>

static const char *TAG = "GOOSE_DMA";

typedef struct {
    gdma_channel_handle_t tx_chan;
    gdma_channel_handle_t rx_chan;
    bool active;
} goose_dma_context_t;

static goose_dma_context_t dma_ctx;

esp_err_t goose_dma_init(void) {
    ESP_LOGI(TAG, "Initializing Geometric DMA Substrate...");

    gdma_channel_alloc_config_t tx_alloc_config = {
        .flags.reserve_sibling = 1,
        .direction = GDMA_CHANNEL_DIRECTION_TX,
    };
    ESP_ERROR_CHECK(gdma_new_ahb_channel(&tx_alloc_config, &dma_ctx.tx_chan));

    gdma_channel_alloc_config_t rx_alloc_config = {
        .direction = GDMA_CHANNEL_DIRECTION_RX,
        .sibling_chan = dma_ctx.tx_chan,
    };
    ESP_ERROR_CHECK(gdma_new_ahb_channel(&rx_alloc_config, &dma_ctx.rx_chan));

    dma_ctx.active = true;
    return ESP_OK;
}

esp_err_t goose_dma_apply_route(goose_route_t *route) {
    if (!route || route->coupling != GOOSE_COUPLING_DMA) return ESP_ERR_INVALID_ARG;

    /**
     * Geometric DMA Logic:
     * - source_coord -> Data Source (SRAM address)
     * - sink_coord -> Data Sink (SRAM address or Peripheral)
     * - orientation -> Authority (1 = Flow, 0 = Inhibit)
     */
    
    // In a real implementation, we would weave descriptors here.
    // For the proof-of-concept, we log the "Authority to Flow".
    
    if (route->cached_source && route->cached_sink) {
        bool flow_enabled = (route->orientation == 1);
        
        ESP_LOGI(TAG, "DMA Route [%s]: %s -> %s [%s]", 
                 route->name, 
                 route->cached_source->hardware_addr >= 0x40000000 ? "SRAM" : "PERIPH",
                 route->cached_sink->hardware_addr >= 0x40000000 ? "SRAM" : "PERIPH",
                 flow_enabled ? "FLOWING" : "INHIBITED");
                 
        // Here we would call gdma_start() or gdma_stop()
    }

    return ESP_OK;
}
