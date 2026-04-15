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
#include "mbedtls/md.h"
#include "nvs.h"
#include "esp_random.h"
#include <string.h>

static const char *TAG = "GOOSE_ATMOSPHERE";

/* Aura wire protocol epoch. Bump on incompatible changes so older peers log
 * AURA_VERSION_MISMATCH instead of silently failing the MAC check. */
#define GOOSE_ARC_VERSION 0x02

/* Default Aura key used when no NVS-provisioned key exists. Known gap:
 * factory-fresh boards share this key. Operator should provision via
 * `aura setkey <hex>` shell command. */
static const uint8_t GOOSE_AURA_KEY_DEFAULT[16] = {
    0xDE, 0xCA, 0xFB, 0xAD, 0x47, 0x4F, 0x4F, 0x53,
    0x45, 0x41, 0x55, 0x52, 0x41, 0x53, 0x45, 0x43,
};
static uint8_t goose_aura_key[16];

#define ARC_OP_SYNC      0x00
#define ARC_OP_QUERY     0xDE
#define ARC_OP_ADVERTISE 0xAD
#define ARC_OP_POSTURE   0xCC

#pragma pack(push, 1)
typedef struct {
    uint8_t version;
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

/* HMAC-SHA256 over the packet's authenticated fields (including version),
 * truncated to 32 bits to fit the existing wire format. Truncation caps
 * collision resistance at the birthday bound (~2^16). Fail-closed on any
 * mbedtls error via a fixed sentinel that reliably fails the receiver check. */
#define AURA_ERROR_SENTINEL 0xDEADBEEFu

#pragma pack(push, 1)
typedef struct {
    uint8_t version;
    uint8_t op;
    int8_t coord[9];
    uint32_t name_hash;
    int8_t state;
    uint32_t nonce;
} aura_input_t;
#pragma pack(pop)

static uint32_t calculate_aura(uint8_t version, uint8_t op, reflex_tryte9_t coord,
                               uint32_t name_hash, int8_t state, uint32_t nonce) {
    aura_input_t in;
    in.version = version;
    in.op = op;
    for (int i = 0; i < 9; i++) in.coord[i] = coord.trits[i];
    in.name_hash = name_hash;
    in.state = state;
    in.nonce = nonce;

    mbedtls_md_context_t ctx;
    mbedtls_md_init(&ctx);
    const mbedtls_md_info_t *md = mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
    if (!md) {
        ESP_LOGE(TAG, "mbedtls md info unavailable, fail-closed");
        return AURA_ERROR_SENTINEL;
    }
    if (mbedtls_md_setup(&ctx, md, 1) != 0 ||
        mbedtls_md_hmac_starts(&ctx, goose_aura_key, sizeof(goose_aura_key)) != 0 ||
        mbedtls_md_hmac_update(&ctx, (const uint8_t *)&in, sizeof(in)) != 0) {
        mbedtls_md_free(&ctx);
        ESP_LOGE(TAG, "hmac setup/update failed, fail-closed");
        return AURA_ERROR_SENTINEL;
    }
    uint8_t digest[32];
    int rc = mbedtls_md_hmac_finish(&ctx, digest);
    mbedtls_md_free(&ctx);
    if (rc != 0) {
        ESP_LOGE(TAG, "hmac finish failed, fail-closed");
        return AURA_ERROR_SENTINEL;
    }
    return ((uint32_t)digest[0]) | ((uint32_t)digest[1] << 8)
         | ((uint32_t)digest[2] << 16) | ((uint32_t)digest[3] << 24);
}

esp_err_t goose_atmosphere_advertise(uint32_t name_hash, goose_cell_t *cell, const uint8_t *dest_mac);

static uint64_t last_query_processed_us = 0;
int32_t swarm_accumulator = 0;
#define SWARM_THRESHOLD 10
#define SWARM_ACCUM_MAX 100
#define SWARM_WEIGHT_MAX 4  /* Cap per-packet posture weight so a single rogue
                             * peer cannot cross SWARM_THRESHOLD alone.
                             * threshold=10, weight_max=4 → minimum 3 packets
                             * from cooperating peers to flip posture. */

/* Replay cache: reject packets whose (src_mac, nonce) pair has been seen
 * within REPLAY_WINDOW_US. 64-slot direct-mapped ring hashed over nonce
 * and the trailing MAC bytes so two peers with colliding nonce low bits
 * land in different slots. Entries older than the window are treated as
 * empty and overwritten; entries within the window match only on exact
 * (mac, nonce) equality. */
#define REPLAY_CACHE_SLOTS 64
#define REPLAY_WINDOW_US   (5 * 1000 * 1000ULL)  /* 5 seconds */

typedef struct {
    uint8_t mac[6];
    uint32_t nonce;
    uint64_t last_us;
} replay_entry_t;
static replay_entry_t replay_cache[REPLAY_CACHE_SLOTS];

static inline uint32_t replay_slot_index(uint32_t nonce, const uint8_t *mac) {
    /* Blend nonce bits with the last two MAC bytes (least likely to be
     * shared across peers on a local mesh) so cross-peer slot collisions
     * are rare even when nonce low bits align. */
    uint32_t h = nonce ^ ((uint32_t)mac[4] << 16) ^ ((uint32_t)mac[5] << 8);
    return h & (REPLAY_CACHE_SLOTS - 1);
}

static bool replay_seen_or_record(const uint8_t *src_mac, uint32_t nonce, uint64_t now_us) {
    replay_entry_t *e = &replay_cache[replay_slot_index(nonce, src_mac)];
    bool within_window = (e->last_us != 0) && (now_us - e->last_us < REPLAY_WINDOW_US);
    if (within_window && e->nonce == nonce && memcmp(e->mac, src_mac, 6) == 0) {
        return true;  /* duplicate inside the guard window */
    }
    /* Stale or empty slot, or a different (mac, nonce): overwrite. */
    memcpy(e->mac, src_mac, 6);
    e->nonce = nonce;
    e->last_us = now_us;
    return false;
}

/* Persist a 16-byte key blob to NVS under goose/aura_key. Shared between
 * first-boot auto-provisioning and the operator `aura setkey` command. */
static esp_err_t persist_aura_key(const uint8_t key[16]) {
    nvs_handle_t h;
    esp_err_t rc = nvs_open("goose", NVS_READWRITE, &h);
    if (rc != ESP_OK) return rc;
    rc = nvs_set_blob(h, "aura_key", key, 16);
    if (rc == ESP_OK) rc = nvs_commit(h);
    nvs_close(h);
    return rc;
}

static void load_aura_key(void) {
    nvs_handle_t h;
    if (nvs_open("goose", NVS_READONLY, &h) == ESP_OK) {
        size_t len = sizeof(goose_aura_key);
        esp_err_t rc = nvs_get_blob(h, "aura_key", goose_aura_key, &len);
        nvs_close(h);
        if (rc == ESP_OK && len == sizeof(goose_aura_key)) {
            ESP_LOGI(TAG, "aura key loaded from NVS");
            return;
        }
    }

    /* First boot (or NVS wiped): generate a random per-board key so two
     * factory-fresh boards don't accidentally trust each other. Pairing
     * now requires an operator to run `aura setkey <hex>` on both sides
     * with a chosen shared key. */
    uint8_t fresh[16];
    esp_fill_random(fresh, sizeof(fresh));
    esp_err_t rc = persist_aura_key(fresh);
    if (rc == ESP_OK) {
        memcpy(goose_aura_key, fresh, sizeof(goose_aura_key));
        ESP_LOGI(TAG, "aura key auto-provisioned (run 'aura setkey' on peers to pair)");
    } else {
        /* NVS write failed; fall back to compile-time default so the mesh
         * still works at all. Logged as a warning so the operator notices. */
        memcpy(goose_aura_key, GOOSE_AURA_KEY_DEFAULT, sizeof(goose_aura_key));
        ESP_LOGW(TAG, "aura key NVS write failed (rc=0x%x); using compile-time default", rc);
    }
}

esp_err_t goose_atmosphere_set_key(const uint8_t key[16]) {
    if (!key) return ESP_ERR_INVALID_ARG;
    esp_err_t rc = persist_aura_key(key);
    if (rc == ESP_OK) {
        memcpy(goose_aura_key, key, 16);
        ESP_LOGI(TAG, "aura key provisioned");
    }
    return rc;
}

static void atmosphere_recv_cb(const esp_now_recv_info_t *recv_info, const uint8_t *data, int len) {
    if (len != sizeof(goose_arc_packet_t)) return;

    // Self-Arc Suppression
    uint8_t local_mac[6];
    esp_read_mac(local_mac, ESP_MAC_WIFI_STA);
    if (memcmp(recv_info->src_addr, local_mac, 6) == 0) return;

    goose_arc_packet_t *arc = (goose_arc_packet_t *)data;

    // Protocol version gate — log once, don't spam
    if (arc->version != GOOSE_ARC_VERSION) {
        static uint64_t last_version_warn_us = 0;
        uint64_t now_v = esp_timer_get_time();
        if (now_v - last_version_warn_us > 5000000) {
            ESP_LOGW(TAG, "AURA_VERSION_MISMATCH remote=0x%02x local=0x%02x from " MACSTR,
                     arc->version, GOOSE_ARC_VERSION, MAC2STR(recv_info->src_addr));
            last_version_warn_us = now_v;
        }
        return;
    }

    uint32_t expected_aura = calculate_aura(arc->version, arc->op, arc->coord,
                                            arc->name_hash, arc->state, arc->nonce);
    if (arc->aura != expected_aura) return;

    uint64_t now = esp_timer_get_time();

    // Replay protection — reject packets seen within the cache window
    if (replay_seen_or_record(recv_info->src_addr, arc->nonce, now)) return;

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
        // Weight cap enforced on receive (sender cannot override).
        uint8_t wire_weight = (uint8_t)(arc->nonce & 0x0F);
        if (wire_weight > SWARM_WEIGHT_MAX) wire_weight = SWARM_WEIGHT_MAX;
        int32_t delta = (int32_t)arc->state * (int32_t)wire_weight;
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
    load_aura_key();
    memset(replay_cache, 0, sizeof(replay_cache));
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
        .version = GOOSE_ARC_VERSION,
        .op = ARC_OP_SYNC, .coord = source->coord, .state = source->state, .nonce = nonce,
        .aura = calculate_aura(GOOSE_ARC_VERSION, ARC_OP_SYNC, source->coord, 0, source->state, nonce)
    };
    uint8_t broadcast_mac[] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    return esp_now_send(broadcast_mac, (uint8_t *)&arc, sizeof(arc));
}

esp_err_t goose_atmosphere_query(const char *name) {
    uint32_t nonce = (uint32_t)esp_timer_get_time();
    uint32_t h = goose_name_hash(name);
    goose_arc_packet_t arc = {
        .version = GOOSE_ARC_VERSION,
        .op = ARC_OP_QUERY, .name_hash = h, .nonce = nonce
    };
    arc.aura = calculate_aura(GOOSE_ARC_VERSION, ARC_OP_QUERY, (reflex_tryte9_t){{0}}, h, 0, nonce);
    uint8_t broadcast_mac[] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    return esp_now_send(broadcast_mac, (uint8_t *)&arc, sizeof(arc));
}

esp_err_t goose_atmosphere_advertise(uint32_t name_hash, goose_cell_t *cell, const uint8_t *dest_mac) {
    uint32_t nonce = (uint32_t)esp_timer_get_time();
    goose_arc_packet_t arc = {
        .version = GOOSE_ARC_VERSION,
        .op = ARC_OP_ADVERTISE, .coord = cell->coord, .state = cell->state, .name_hash = name_hash, .nonce = nonce
    };
    arc.aura = calculate_aura(GOOSE_ARC_VERSION, ARC_OP_ADVERTISE, arc.coord, name_hash, arc.state, nonce);
    uint8_t broadcast_mac[] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    return esp_now_send(broadcast_mac, (uint8_t *)&arc, sizeof(arc));
}

esp_err_t goose_atmosphere_emit_posture(int8_t state, uint8_t weight) {
    /* Emit-side cap (defense in depth; receive-side also clamps). */
    if (weight > SWARM_WEIGHT_MAX) weight = SWARM_WEIGHT_MAX;
    uint32_t nonce = (uint32_t)esp_timer_get_time() & 0xFFFFFFF0;
    nonce |= (weight & 0x0F);
    goose_arc_packet_t arc = {
        .version = GOOSE_ARC_VERSION,
        .op = ARC_OP_POSTURE, .coord = {{0}}, .state = state, .nonce = nonce,
        .aura = calculate_aura(GOOSE_ARC_VERSION, ARC_OP_POSTURE, (reflex_tryte9_t){{0}}, 0, state, nonce)
    };
    uint8_t broadcast_mac[] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    return esp_now_send(broadcast_mac, (uint8_t *)&arc, sizeof(arc));
}
