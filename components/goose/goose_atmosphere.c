/**
 * @file goose_atmosphere.c
 * @brief GOOSE Atmosphere: Distributed Geometric Mesh
 */

#include "goose.h"
#include "reflex_hal.h"
#include "reflex_kv.h"
#include "reflex_radio.h"
#include "reflex_crypto.h"
#include "reflex_task.h"
#include "reflex_tuning.h"
#include <string.h>

#define TAG "GOOSE_ATMOSPHERE"

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
#define ARC_OP_MMIO_SYNC 0x10

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

static uint32_t goose_name_hash(const char *name) { return goose_fnv1a(name); }

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

    uint8_t digest[32];
    if (reflex_hmac_sha256(goose_aura_key, sizeof(goose_aura_key),
                           (const uint8_t *)&in, sizeof(in), digest) != REFLEX_OK) {
        REFLEX_LOGE(TAG, "hmac failed, fail-closed");
        return AURA_ERROR_SENTINEL;
    }
    return ((uint32_t)digest[0]) | ((uint32_t)digest[1] << 8)
         | ((uint32_t)digest[2] << 16) | ((uint32_t)digest[3] << 24);
}

reflex_err_t goose_atmosphere_advertise(uint32_t name_hash, goose_cell_t *cell, const uint8_t *dest_mac);

static uint64_t last_query_processed_us = 0;
static reflex_mutex_t swarm_mux;
static bool swarm_mux_init = false;
int32_t swarm_accumulator = 0;

static goose_mesh_stats_t mesh_stats;

goose_mesh_stats_t goose_atmosphere_get_stats(void) {
    if (swarm_mux_init) reflex_critical_enter(&swarm_mux);
    goose_mesh_stats_t copy = mesh_stats;
    if (swarm_mux_init) reflex_critical_exit(&swarm_mux);
    return copy;
}
/* Swarm constants now live in reflex_tuning.h:
 * REFLEX_SWARM_THRESHOLD, REFLEX_SWARM_ACCUM_MAX, REFLEX_SWARM_WEIGHT_MAX
 * Cap per-packet posture weight so a single rogue peer cannot cross
 * REFLEX_SWARM_THRESHOLD alone. threshold=10, weight_max=4 -> minimum
 * 3 packets from cooperating peers to flip posture. */

/* Replay cache: reject packets whose (src_mac, nonce) pair has been seen
 * within REFLEX_REPLAY_WINDOW_US. 64-slot direct-mapped ring hashed over nonce
 * and the trailing MAC bytes so two peers with colliding nonce low bits
 * land in different slots. Entries older than the window are treated as
 * empty and overwritten; entries within the window match only on exact
 * (mac, nonce) equality. */
/* Replay constants now live in reflex_tuning.h:
 * REFLEX_REPLAY_CACHE_SLOTS, REFLEX_REPLAY_WINDOW_US */

typedef struct {
    uint8_t mac[6];
    uint32_t nonce;
    uint64_t last_us;
} replay_entry_t;
static replay_entry_t replay_cache[REFLEX_REPLAY_CACHE_SLOTS];

static inline uint32_t replay_slot_index(uint32_t nonce, const uint8_t *mac) {
    /* Blend nonce bits with the last two MAC bytes (least likely to be
     * shared across peers on a local mesh) so cross-peer slot collisions
     * are rare even when nonce low bits align. */
    uint32_t h = nonce ^ ((uint32_t)mac[4] << 16) ^ ((uint32_t)mac[5] << 8);
    return h & (REFLEX_REPLAY_CACHE_SLOTS - 1);
}

static bool replay_seen_or_record(const uint8_t *src_mac, uint32_t nonce, uint64_t now_us) {
    replay_entry_t *e = &replay_cache[replay_slot_index(nonce, src_mac)];
    bool within_window = (e->last_us != 0) && (now_us - e->last_us < REFLEX_REPLAY_WINDOW_US);
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
static reflex_err_t persist_aura_key(const uint8_t key[16]) {
    reflex_kv_handle_t h;
    reflex_err_t rc = reflex_kv_open("goose", false, &h);
    if (rc != REFLEX_OK) return rc;
    rc = reflex_kv_set_blob(h, "aura_key", key, 16);
    if (rc == REFLEX_OK) rc = reflex_kv_commit(h);
    reflex_kv_close(h);
    return rc;
}

static void load_aura_key(void) {
    reflex_kv_handle_t h;
    if (reflex_kv_open("goose", true, &h) == REFLEX_OK) {
        size_t len = sizeof(goose_aura_key);
        reflex_err_t rc = reflex_kv_get_blob(h, "aura_key", goose_aura_key, &len);
        reflex_kv_close(h);
        if (rc == REFLEX_OK && len == sizeof(goose_aura_key)) {
            REFLEX_LOGI(TAG, "aura key loaded from NVS");
            return;
        }
    }

    /* First boot (or NVS wiped): generate a random per-board key so two
     * factory-fresh boards don't accidentally trust each other. Pairing
     * now requires an operator to run `aura setkey <hex>` on both sides
     * with a chosen shared key. */
    uint8_t fresh[16];
    reflex_hal_random_fill(fresh, sizeof(fresh));
    reflex_err_t rc = persist_aura_key(fresh);
    if (rc == REFLEX_OK) {
        memcpy(goose_aura_key, fresh, sizeof(goose_aura_key));
        REFLEX_LOGI(TAG, "aura key auto-provisioned (run 'aura setkey' on peers to pair)");
    } else {
        /* NVS write failed; fall back to compile-time default so the mesh
         * still works at all. Logged as a warning so the operator notices. */
        memcpy(goose_aura_key, GOOSE_AURA_KEY_DEFAULT, sizeof(goose_aura_key));
        REFLEX_LOGW(TAG, "aura key NVS write failed (rc=0x%x); using compile-time default", rc);
    }
}

reflex_err_t goose_atmosphere_set_key(const uint8_t key[16]) {
    if (!key) return REFLEX_ERR_INVALID_ARG;
    reflex_err_t rc = persist_aura_key(key);
    if (rc == REFLEX_OK) {
        memcpy(goose_aura_key, key, 16);
        REFLEX_LOGI(TAG, "aura key provisioned");
    }
    return rc;
}

static void atmosphere_recv_cb(const reflex_radio_recv_info_t *recv_info, const uint8_t *data, int len) {
    if (len != sizeof(goose_arc_packet_t)) return;

    // Self-Arc Suppression
    uint8_t local_mac[6];
    reflex_hal_mac_read(local_mac);
    if (memcmp(recv_info->src_addr, local_mac, 6) == 0) {
        mesh_stats.rx_self_drop++;
        return;
    }

    goose_arc_packet_t *arc = (goose_arc_packet_t *)data;

    /* Protocol version gate — rate-limited per (sender mac, version) so
     * two simultaneously-mismatched peers each get one log entry per
     * window instead of the louder one starving the quieter one. 8-slot
     * direct-mapped ring keyed on the low-order MAC bytes. */
    if (arc->version != GOOSE_ARC_VERSION) {
        mesh_stats.rx_version_mismatch++;
        typedef struct { uint8_t mac[6]; uint8_t version; uint64_t last_us; } version_warn_entry_t;
        static version_warn_entry_t version_warn_ring[8];
        uint32_t slot = ((uint32_t)recv_info->src_addr[4] << 8 |
                          recv_info->src_addr[5]) & 0x7;
        version_warn_entry_t *w = &version_warn_ring[slot];
        uint64_t now_v = reflex_hal_time_us();
        bool same = (memcmp(w->mac, recv_info->src_addr, 6) == 0 && w->version == arc->version);
        if (!same || (now_v - w->last_us > 5000000)) {
            REFLEX_LOGW(TAG, "AURA_VERSION_MISMATCH remote=0x%02x local=0x%02x from %02x:%02x:%02x:%02x:%02x:%02x",
                     arc->version, GOOSE_ARC_VERSION,
                     recv_info->src_addr[0], recv_info->src_addr[1], recv_info->src_addr[2],
                     recv_info->src_addr[3], recv_info->src_addr[4], recv_info->src_addr[5]);
            memcpy(w->mac, recv_info->src_addr, 6);
            w->version = arc->version;
            w->last_us = now_v;
        }
        return;
    }

    uint32_t expected_aura = calculate_aura(arc->version, arc->op, arc->coord,
                                            arc->name_hash, arc->state, arc->nonce);
    if (arc->aura != expected_aura) {
        mesh_stats.rx_aura_fail++;
        return;
    }

    uint64_t now = reflex_hal_time_us();

    // Replay protection — reject packets seen within the cache window
    if (replay_seen_or_record(recv_info->src_addr, arc->nonce, now)) {
        mesh_stats.rx_replay_drop++;
        return;
    }

    if (arc->op == ARC_OP_SYNC) {
        mesh_stats.rx_sync++;
        extern goose_route_t* goose_fabric_find_radio_route_by_source_coord(reflex_tryte9_t coord);
        goose_route_t *route = goose_fabric_find_radio_route_by_source_coord(arc->coord);
        if (route && route->cached_sink) { route->cached_sink->state = arc->state; }
    }
    else if (arc->op == ARC_OP_QUERY) {
        mesh_stats.rx_query++;
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
        mesh_stats.rx_advertise++;
        REFLEX_LOGI(TAG, "Ghost Solidified for hash [0x%08lX] at %02x:%02x:%02x:%02x:%02x:%02x",
                     (unsigned long)arc->name_hash,
                     recv_info->src_addr[0], recv_info->src_addr[1], recv_info->src_addr[2],
                     recv_info->src_addr[3], recv_info->src_addr[4], recv_info->src_addr[5]);
    }
    else if (arc->op == ARC_OP_POSTURE) {
        mesh_stats.rx_posture++;
        uint8_t wire_weight = (uint8_t)(arc->nonce & 0x0F);
        if (wire_weight > REFLEX_SWARM_WEIGHT_MAX) wire_weight = REFLEX_SWARM_WEIGHT_MAX;
        int32_t delta = (int32_t)arc->state * (int32_t)wire_weight;
        if (swarm_mux_init) reflex_critical_enter(&swarm_mux);
        swarm_accumulator += delta;
        if (swarm_accumulator > REFLEX_SWARM_ACCUM_MAX) swarm_accumulator = REFLEX_SWARM_ACCUM_MAX;
        if (swarm_accumulator < -REFLEX_SWARM_ACCUM_MAX) swarm_accumulator = -REFLEX_SWARM_ACCUM_MAX;
        int32_t accum = swarm_accumulator;
        if (swarm_mux_init) reflex_critical_exit(&swarm_mux);

        goose_cell_t *posture_cell = goonies_resolve_cell("sys.swarm.posture");
        if (posture_cell) {
            if (accum >= REFLEX_SWARM_THRESHOLD) posture_cell->state = 1;
            else if (accum <= -REFLEX_SWARM_THRESHOLD) posture_cell->state = -1;
            else posture_cell->state = 0;
        }
    }
    else if (arc->op == ARC_OP_MMIO_SYNC) {
        mesh_stats.rx_mmio_sync++;
        goose_mmio_sync_recv(recv_info->src_addr, arc->name_hash, arc->state);
    }
}

reflex_err_t goose_atmosphere_init(void) {
    swarm_mux = reflex_mutex_init();
    swarm_mux_init = true;
    load_aura_key();
    memset(replay_cache, 0, sizeof(replay_cache));
    reflex_radio_init();
    reflex_radio_register_recv(atmosphere_recv_cb);
    uint8_t broadcast_mac[] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    reflex_radio_add_peer(broadcast_mac);
    return REFLEX_OK;
}

reflex_err_t goose_atmosphere_emit_arc(goose_cell_t *source) {
    if (!source) return REFLEX_ERR_INVALID_ARG;
    uint32_t nonce = (uint32_t)reflex_hal_time_us();
    goose_arc_packet_t arc = {
        .version = GOOSE_ARC_VERSION,
        .op = ARC_OP_SYNC, .coord = source->coord, .state = source->state, .nonce = nonce,
        .aura = calculate_aura(GOOSE_ARC_VERSION, ARC_OP_SYNC, source->coord, 0, source->state, nonce)
    };
    uint8_t broadcast_mac[] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    return reflex_radio_send(broadcast_mac, (uint8_t *)&arc, sizeof(arc));
}

reflex_err_t goose_atmosphere_query(const char *name) {
    uint32_t nonce = (uint32_t)reflex_hal_time_us();
    uint32_t h = goose_name_hash(name);
    goose_arc_packet_t arc = {
        .version = GOOSE_ARC_VERSION,
        .op = ARC_OP_QUERY, .name_hash = h, .nonce = nonce
    };
    arc.aura = calculate_aura(GOOSE_ARC_VERSION, ARC_OP_QUERY, (reflex_tryte9_t){{0}}, h, 0, nonce);
    uint8_t broadcast_mac[] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    return reflex_radio_send(broadcast_mac, (uint8_t *)&arc, sizeof(arc));
}

reflex_err_t goose_atmosphere_advertise(uint32_t name_hash, goose_cell_t *cell, const uint8_t *dest_mac) {
    uint32_t nonce = (uint32_t)reflex_hal_time_us();
    goose_arc_packet_t arc = {
        .version = GOOSE_ARC_VERSION,
        .op = ARC_OP_ADVERTISE, .coord = cell->coord, .state = cell->state, .name_hash = name_hash, .nonce = nonce
    };
    arc.aura = calculate_aura(GOOSE_ARC_VERSION, ARC_OP_ADVERTISE, arc.coord, name_hash, arc.state, nonce);
    uint8_t broadcast_mac[] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    return reflex_radio_send(broadcast_mac, (uint8_t *)&arc, sizeof(arc));
}

reflex_err_t goose_atmosphere_emit_sync_arc(uint32_t name_hash, int8_t state) {
    uint32_t nonce = (uint32_t)reflex_hal_time_us();
    goose_arc_packet_t arc = {
        .version = GOOSE_ARC_VERSION,
        .op = ARC_OP_MMIO_SYNC, .coord = {{0}}, .state = state,
        .name_hash = name_hash, .nonce = nonce,
        .aura = calculate_aura(GOOSE_ARC_VERSION, ARC_OP_MMIO_SYNC, (reflex_tryte9_t){{0}}, name_hash, state, nonce)
    };
    uint8_t broadcast_mac[] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    mesh_stats.tx_mmio_sync++;
    return reflex_radio_send(broadcast_mac, (uint8_t *)&arc, sizeof(arc));
}

reflex_err_t goose_atmosphere_emit_posture(int8_t state, uint8_t weight) {
    /* Emit-side cap (defense in depth; receive-side also clamps). */
    if (weight > REFLEX_SWARM_WEIGHT_MAX) weight = REFLEX_SWARM_WEIGHT_MAX;
    uint32_t nonce = (uint32_t)reflex_hal_time_us() & 0xFFFFFFF0;
    nonce |= (weight & 0x0F);
    goose_arc_packet_t arc = {
        .version = GOOSE_ARC_VERSION,
        .op = ARC_OP_POSTURE, .coord = {{0}}, .state = state, .nonce = nonce,
        .aura = calculate_aura(GOOSE_ARC_VERSION, ARC_OP_POSTURE, (reflex_tryte9_t){{0}}, 0, state, nonce)
    };
    uint8_t broadcast_mac[] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    return reflex_radio_send(broadcast_mac, (uint8_t *)&arc, sizeof(arc));
}
