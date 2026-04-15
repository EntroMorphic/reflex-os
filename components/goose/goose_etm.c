#include "goose.h"
#include "esp_etm.h"
#include "esp_log.h"
#include "driver/gpio_etm.h"

static const char *TAG = "GOOSE_ETM";

#define GOOSE_ETM_MAX_LANES 50

typedef struct {
    esp_etm_channel_handle_t chan;
    goose_route_t *route;
    bool in_use;
} goose_etm_lane_t;

static goose_etm_lane_t etm_pool[GOOSE_ETM_MAX_LANES];

esp_err_t goose_etm_init(void) {
    ESP_LOGI(TAG, "Initializing Silicon Agency (ETM Matrix)...");
    for (int i = 0; i < GOOSE_ETM_MAX_LANES; i++) {
        etm_pool[i].in_use = false;
    }
    return ESP_OK;
}

esp_err_t goose_etm_apply_route(goose_route_t *route) {
    if (!route || route->coupling != GOOSE_COUPLING_SILICON) return ESP_ERR_INVALID_ARG;

    /**
     * Red-Team Guard: Ontologic Integrity
     * ETM is binary (Trigger). GOOSE is ternary.
     * We only allow Silicon Agency for positive reinforcement (Orientation +1).
     * Inhibition (0) or Inversion (-1) must fall back to Software.
     */
    if (route->orientation != 1) {
        ESP_LOGW(TAG, "Silicon Agency does not support orientation %d. Falling back to Software.", route->orientation);
        route->coupling = GOOSE_COUPLING_SOFTWARE;
        return ESP_OK;
    }

    ESP_LOGI(TAG, "Weaving Silicon Route: %s", route->name);
    
    // Check if route already has a lane (prevent double-allocation)
    for (int i = 0; i < GOOSE_ETM_MAX_LANES; i++) {
        if (etm_pool[i].in_use && etm_pool[i].route == route) return ESP_OK;
    }

    // 1. Find a free lane
    int lane_idx = -1;
    for (int i = 0; i < GOOSE_ETM_MAX_LANES; i++) {
        if (!etm_pool[i].in_use) {
            lane_idx = i;
            break;
        }
    }

    if (lane_idx == -1) {
        ESP_LOGW(TAG, "ETM Matrix Full. Falling back to Software coupling for %s", route->name);
        route->coupling = GOOSE_COUPLING_SOFTWARE;
        return ESP_OK;
    }

    etm_pool[lane_idx].in_use = true;
    etm_pool[lane_idx].route = route;

    ESP_LOGI(TAG, "Silicon Route Established on Lane %d", lane_idx);

    return ESP_OK;
}

esp_err_t goose_etm_unweave_route(goose_route_t *route) {
    if (!route) return ESP_ERR_INVALID_ARG;
    
    for (int i = 0; i < GOOSE_ETM_MAX_LANES; i++) {
        if (etm_pool[i].in_use && etm_pool[i].route == route) {
            // In a real implementation, we would call esp_etm_del_channel()
            etm_pool[i].in_use = false;
            etm_pool[i].route = NULL;
            ESP_LOGI(TAG, "Silicon Route Released: %s (Lane %d)", route->name, i);
            return ESP_OK;
        }
    }
    return ESP_ERR_NOT_FOUND;
}
