/**
 * @file goose_atmosphere.c
 * @brief GOOSE Atmosphere: Distributed Geometric Mesh
 */

#include "goose.h"
#include "esp_now.h"
#include "esp_wifi.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_timer.h"
#include <string.h>

static const char *TAG = "GOOSE_ATMOSPHERE";

#define GOOSE_AURA_SECRET 0xDECAFBAD

#define ARC_OP_SYNC      0x00 
#define ARC_OP_QUERY     0xDE 
#define ARC_OP_ADVERTISE 0xAD 
#define ARC_OP_POSTURE   0xCC

#pragma pack(push, 1)
typedef struct {
    uint8_t op;
    reflex_tryte9_t coord;
    int8_t state;
    uint32_t name_hash;
    uint32_t nonce;
    uint32_t aura;
} goose_arc_packet_t;
#pragma pack(pop)

static uint32_t goose_name_hash(const char *name) {
    uint32_t h = 0x811c9dc5;
    for (int i = 0; name[i]; i++) { h ^= (uint32_t)name[i]; h *= 0x01000193; }
    return h;
}

static uint32_t calculate_aura(uint8_t op, reflex_tryte9_t coord, uint32_t name_hash, int8_t state, uint32_t nonce) {
    uint32_t h = GOOSE_AURA_SECRET ^ nonce ^ (uint32_t)op;
    for(int i=0; i<9; i++) h = (h * 31) + coord.trits[i];
    h ^= name_hash; h ^= (uint32_t)state;
    return h;
}

esp_err_t goose_atmosphere_advertise(uint32_t name_hash, goose_cell_t *cell, const uint8_t *dest_mac);

static uint64_t last_query_processed_us = 0;
int32_t swarm_accumulator = 0;
#define SWARM_THRESHOLD 10
#define SWARM_ACCUM_MAX 100

static void atmosphere_recv_cb(const esp_now_recv_info_t *recv_info, const uint8_t *data, int len) {
    if (len != sizeof(goose_arc_packet_t)) return;
    
    // Red-Team Remediation: Self-Arc Suppression
    uint8_t local_mac[6];
    esp_read_mac(local_mac, ESP_MAC_WIFI_STA);
    if (memcmp(recv_info->src_addr, local_mac, 6) == 0) return;

    goose_arc_packet_t *arc = (goose_arc_packet_t *)data;
    uint32_t expected_aura = calculate_aura(arc->op, arc->coord, arc->name_hash, arc->state, arc->nonce);
    if (arc->aura != expected_aura) return; 

    uint64_t now = esp_timer_get_time();

    if (arc->op == ARC_OP_SYNC) {
        extern goose_route_t* goose_fabric_find_radio_route_by_source_coord(reflex_tryte9_t coord);
        goose_route_t *route = goose_fabric_find_radio_route_by_source_coord(arc->coord);
        if (route && route->cached_sink) { route->cached_sink->state = arc->state; }
    } 
    else if (arc->op == ARC_OP_QUERY) {
        if (now - last_query_processed_us < 100000) return; 
        last_query_processed_us = now;
        extern uint32_t goonies_get_count(void);
        extern const char* goonies_get_name_by_idx(uint32_t idx);
        uint32_t count = goonies_get_count();
        for (uint32_t i = 0; i < count; i++) {
            const char *name = goonies_get_name_by_idx(i);
            if (name && goose_name_hash(name) == arc->name_hash) {
                goose_cell_t *local_cell = goonies_resolve_cell(name);
                if (local_cell) { goose_atmosphere_advertise(arc->name_hash, local_cell, recv_info->src_addr); }
                break;
            }
        }
    }
    else if (arc->op == ARC_OP_ADVERTISE) {
        ESP_LOGI(TAG, "Ghost Solidified for hash [0x%08lX] at " MACSTR, (unsigned long)arc->name_hash, MAC2STR(recv_info->src_addr));
    }
    else if (arc->op == ARC_OP_POSTURE) {
        // Red-Team Remediation: Saturating Accumulator
        int32_t delta = (int32_t)arc->state * (int32_t)(arc->nonce & 0x0F);
        swarm_accumulator += delta;
        if (swarm_accumulator > SWARM_ACCUM_MAX) swarm_accumulator = SWARM_ACCUM_MAX;
        if (swarm_accumulator < -SWARM_ACCUM_MAX) swarm_accumulator = -SWARM_ACCUM_MAX;
        
        goose_cell_t *posture_cell = goonies_resolve_cell("sys.swarm.posture");
        if (posture_cell) {
            if (swarm_accumulator >= SWARM_THRESHOLD) posture_cell->state = 1;
            else if (swarm_accumulator <= -SWARM_THRESHOLD) posture_cell->state = -1;
            else posture_cell->state = 0;
        }
    }
}

esp_err_t goose_atmosphere_init(void) {
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
        .op = ARC_OP_SYNC, .coord = source->coord, .state = source->state, .nonce = nonce,
        .aura = calculate_aura(ARC_OP_SYNC, source->coord, 0, source->state, nonce)
    };
    uint8_t broadcast_mac[] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    return esp_now_send(broadcast_mac, (uint8_t *)&arc, sizeof(arc));
}

esp_err_t goose_atmosphere_query(const char *name) {
    uint32_t nonce = (uint32_t)esp_timer_get_time();
    uint32_t h = goose_name_hash(name);
    goose_arc_packet_t arc = { .op = ARC_OP_QUERY, .name_hash = h, .nonce = nonce };
    arc.aura = calculate_aura(ARC_OP_QUERY, (reflex_tryte9_t){{0}}, h, 0, nonce);
    uint8_t broadcast_mac[] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    return esp_now_send(broadcast_mac, (uint8_t *)&arc, sizeof(arc));
}

esp_err_t goose_atmosphere_advertise(uint32_t name_hash, goose_cell_t *cell, const uint8_t *dest_mac) {
    uint32_t nonce = (uint32_t)esp_timer_get_time();
    goose_arc_packet_t arc = {
        .op = ARC_OP_ADVERTISE, .coord = cell->coord, .state = cell->state, .name_hash = name_hash, .nonce = nonce
    };
    arc.aura = calculate_aura(ARC_OP_ADVERTISE, arc.coord, name_hash, arc.state, nonce);
    uint8_t broadcast_mac[] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    return esp_now_send(broadcast_mac, (uint8_t *)&arc, sizeof(arc));
}

esp_err_t goose_atmosphere_emit_posture(int8_t state, uint8_t weight) {
    uint32_t nonce = (uint32_t)esp_timer_get_time() & 0xFFFFFFF0;
    nonce |= (weight & 0x0F);
    goose_arc_packet_t arc = {
        .op = ARC_OP_POSTURE, .coord = {{0}}, .state = state, .nonce = nonce,
        .aura = calculate_aura(ARC_OP_POSTURE, (reflex_tryte9_t){{0}}, 0, state, nonce)
    };
    uint8_t broadcast_mac[] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    return esp_now_send(broadcast_mac, (uint8_t *)&arc, sizeof(arc));
}
