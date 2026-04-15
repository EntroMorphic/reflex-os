#include "goose.h"
#include "esp_now.h"
#include "esp_wifi.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_timer.h"
#include <string.h>

static const char *TAG = "GOOSE_ATMOSPHERE";

// Red-Team Remediation: Atmospheric Aura (Shared Secret)
// This prevents unauthorized nodes from injecting state into the Loom.
#define GOOSE_AURA_SECRET 0xDECAFBAD

#pragma pack(push, 1)
typedef struct {
    reflex_tryte9_t coord;   ///< Source coordinate
    int8_t state;            ///< Source state
    uint32_t nonce;          ///< Freshness
    uint32_t aura;           ///< Verification Hash
} goose_arc_packet_t;
#pragma pack(pop)

static uint32_t calculate_aura(reflex_tryte9_t coord, int8_t state, uint32_t nonce) {
    uint32_t h = GOOSE_AURA_SECRET ^ nonce;
    for(int i=0; i<9; i++) h = (h * 31) + coord.trits[i];
    h ^= (uint32_t)state;
    return h;
}

static void atmosphere_recv_cb(const esp_now_recv_info_t *recv_info, const uint8_t *data, int len) {
    if (len != sizeof(goose_arc_packet_t)) return;

    goose_arc_packet_t *arc = (goose_arc_packet_t *)data;

    // 1. Verify Aura (Geometric Authentication)
    uint32_t expected_aura = calculate_aura(arc->coord, arc->state, arc->nonce);
    if (arc->aura != expected_aura) {
        ESP_LOGE(TAG, "Malicious Arc Grounded: Aura mismatch from " MACSTR, MAC2STR(recv_info->src_addr));
        return;
    }

    // 2. Lawful Route Check
    extern goose_route_t* goose_fabric_find_radio_route_by_source_coord(reflex_tryte9_t coord);
    goose_route_t *route = goose_fabric_find_radio_route_by_source_coord(arc->coord);

    if (route && route->cached_sink) {
        route->cached_sink->state = arc->state;
        ESP_LOGI(TAG, "Secure Lawful Arc received for route [%s] -> %d", route->name, arc->state);
    }
}

esp_err_t goose_atmosphere_init(void) {
    ESP_LOGI(TAG, "Initializing Secure Atmospheric Substrate...");
    // WiFi and ESP-NOW init (assumes WiFi already init by host or does minimal init)
    esp_now_init();
    esp_now_register_recv_cb(atmosphere_recv_cb);

    esp_now_peer_info_t peer_info = {0};
    uint8_t broadcast_mac[] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    memcpy(peer_info.peer_addr, broadcast_mac, 6);
    esp_now_add_peer(&peer_info);

    return ESP_OK;
}

esp_err_t goose_atmosphere_emit_arc(goose_cell_t *source) {
    if (!source) return ESP_ERR_INVALID_ARG;

    uint32_t nonce = (uint32_t)esp_timer_get_time();
    goose_arc_packet_t arc = {
        .coord = source->coord,
        .state = source->state,
        .nonce = nonce,
        .aura = calculate_aura(source->coord, source->state, nonce)
    };

    uint8_t broadcast_mac[] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    return esp_now_send(broadcast_mac, (uint8_t *)&arc, sizeof(arc));
}
