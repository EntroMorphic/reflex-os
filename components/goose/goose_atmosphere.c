#include "goose.h"
#include "esp_now.h"
#include "esp_wifi.h"
#include "esp_log.h"
#include "esp_mac.h"
#include <string.h>

static const char *TAG = "GOOSE_ATMOSPHERE";

#pragma pack(push, 1)
/**
 * @brief Atmospheric Trit Vector
 * The serialized form of a geometric state jumping the gap.
 */
typedef struct {
    reflex_tryte9_t coord;   ///< Source coordinate
    int8_t state;            ///< Source state
    uint32_t nonce;          ///< Freshness/Auth
} goose_arc_packet_t;
#pragma pack(pop)

static void atmosphere_recv_cb(const esp_now_recv_info_t *recv_info, const uint8_t *data, int len) {
    if (len != sizeof(goose_arc_packet_t)) return;

    goose_arc_packet_t *arc = (goose_arc_packet_t *)data;

    /**
     * Lawful Route Check (Mutual Weaving)
     * We only accept state updates for coordinates that are part of an 
     * established RADIO route in our local Loom.
     */
    extern goose_route_t* goose_fabric_find_radio_route_by_source_coord(reflex_tryte9_t coord);
    goose_route_t *route = goose_fabric_find_radio_route_by_source_coord(arc->coord);

    if (route) {
        // Atomic Posture Update
        route->sink->state = arc->state;
        ESP_LOGI(TAG, "Lawful Arc Received: %s -> %d", route->sink->name, arc->state);
    } else {
        ESP_LOGW(TAG, "Unlawful Arc Grounded: Field[%d] Region[%d] Cell[%d]", 
                 arc->coord.trits[0], arc->coord.trits[3], arc->coord.trits[6]);
    }
}

esp_err_t goose_atmosphere_init(void) {
    ESP_LOGI(TAG, "Initializing Atmospheric Substrate (ESP-NOW)...");

    // 1. Initialize WiFi in Station mode for ESP-NOW
    ESP_ERROR_CHECK(esp_netif_init());
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_start());

    // 2. Initialize ESP-NOW
    ESP_ERROR_CHECK(esp_now_init());
    ESP_ERROR_CHECK(esp_now_register_recv_cb(atmosphere_recv_cb));

    // 3. Register Broadcast Peer
    esp_now_peer_info_t peer_info = {0};
    uint8_t broadcast_mac[] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    memcpy(peer_info.peer_addr, broadcast_mac, 6);
    peer_info.channel = 0;
    peer_info.encrypt = false;
    ESP_ERROR_CHECK(esp_now_add_peer(&peer_info));

    return ESP_OK;
}

esp_err_t goose_atmosphere_process(void) {
    // This will be called by the DISTRIBUTED rhythm pulse to sweep and transmit
    return ESP_OK;
}

/**
 * @brief Emit an Arc
 * Transmits a local cell's state to the atmosphere.
 */
esp_err_t goose_atmosphere_emit_arc(goose_cell_t *source) {
    if (!source) return ESP_ERR_INVALID_ARG;

    goose_arc_packet_t arc = {
        .coord = source->coord,
        .state = source->state,
        .nonce = (uint32_t)esp_timer_get_time()
    };

    uint8_t broadcast_mac[] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    return esp_now_send(broadcast_mac, (uint8_t *)&arc, sizeof(arc));
}
