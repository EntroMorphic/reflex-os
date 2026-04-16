#include "goose.h"
#include "reflex_hal.h"

#define TAG "GOOSE_ETM"

reflex_err_t goose_etm_init(void) {
    REFLEX_LOGI(TAG, "ETM substrate: stub (not implemented)");
    return REFLEX_OK;
}

reflex_err_t goose_etm_apply_route(goose_route_t *route) {
    if (!route || route->coupling != GOOSE_COUPLING_SILICON) return REFLEX_ERR_INVALID_ARG;
    return REFLEX_ERR_NOT_SUPPORTED;
}

reflex_err_t goose_etm_unweave_route(goose_route_t *route) {
    if (!route) return REFLEX_ERR_INVALID_ARG;
    return REFLEX_ERR_NOT_FOUND;
}
