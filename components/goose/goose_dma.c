#include "goose.h"
#include "reflex_hal.h"

#define TAG "GOOSE_DMA"

reflex_err_t goose_dma_init(void) {
    REFLEX_LOGI(TAG, "DMA substrate: stub (not implemented)");
    return REFLEX_OK;
}

reflex_err_t goose_dma_apply_route(goose_route_t *route) {
    if (!route || route->coupling != GOOSE_COUPLING_DMA) return REFLEX_ERR_INVALID_ARG;
    return REFLEX_ERR_NOT_SUPPORTED;
}
