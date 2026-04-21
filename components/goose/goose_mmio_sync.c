/**
 * @file goose_mmio_sync.c
 * @brief MMIO Sync Layer: Distributed Hardware Surface
 *
 * Synchronizes cell state across meshed boards via sync arcs.
 * Peer registry, tiered broadcast, staleness timeout, security enforcement.
 */

#include "goose.h"
#include "reflex_hal.h"
#include "reflex_kv.h"
#include "reflex_radio.h"
#include <string.h>

#define TAG "GOOSE_MMIO_SYNC"

static reflex_peer_t s_peers[MAX_PEERS];
static size_t s_peer_count = 0;

static uint32_t fnv1a(const char *s) { return goose_fnv1a(s); }

static void peers_save_nvs(void) {
    reflex_kv_handle_t h;
    if (reflex_kv_open("goose", false, &h) != REFLEX_OK) return;
    uint8_t count = (uint8_t)s_peer_count;
    reflex_err_t rc = reflex_kv_set_blob(h, "peer_n", &count, 1);
    if (rc == REFLEX_OK && count > 0)
        rc = reflex_kv_set_blob(h, "peers", s_peers, count * sizeof(reflex_peer_t));
    if (rc == REFLEX_OK)
        rc = reflex_kv_commit(h);
    if (rc != REFLEX_OK)
        REFLEX_LOGW(TAG, "peer NVS save failed rc=0x%x", rc);
    reflex_kv_close(h);
}

static void peers_load_nvs(void) {
    reflex_kv_handle_t h;
    if (reflex_kv_open("goose", true, &h) != REFLEX_OK) return;
    uint8_t count = 0;
    size_t len = 1;
    if (reflex_kv_get_blob(h, "peer_n", &count, &len) != REFLEX_OK || count == 0) {
        reflex_kv_close(h);
        return;
    }
    if (count > MAX_PEERS) count = MAX_PEERS;
    len = count * sizeof(reflex_peer_t);
    if (reflex_kv_get_blob(h, "peers", s_peers, &len) == REFLEX_OK) {
        s_peer_count = count;
        for (size_t i = 0; i < s_peer_count; i++) {
            s_peers[i].active = false;
            s_peers[i].last_seen_us = 0;
            reflex_radio_add_peer(s_peers[i].mac);
        }
        REFLEX_LOGI(TAG, "restored %u peers from NVS", (unsigned)s_peer_count);
    }
    reflex_kv_close(h);
}

reflex_err_t goose_mmio_sync_init(void) {
    memset(s_peers, 0, sizeof(s_peers));
    s_peer_count = 0;
    peers_load_nvs();
    return REFLEX_OK;
}

reflex_err_t goose_mmio_sync_add_peer(const char *name, const uint8_t mac[6]) {
    if (!name || !mac) return REFLEX_ERR_INVALID_ARG;

    for (size_t i = 0; i < s_peer_count; i++) {
        if (memcmp(s_peers[i].mac, mac, 6) == 0) {
            strncpy(s_peers[i].name, name, sizeof(s_peers[i].name) - 1);
            s_peers[i].active = true;
            return REFLEX_OK;
        }
    }

    if (s_peer_count >= MAX_PEERS) return REFLEX_ERR_NO_MEM;

    reflex_peer_t *p = &s_peers[s_peer_count];
    memset(p, 0, sizeof(*p));
    strncpy(p->name, name, sizeof(p->name) - 1);
    memcpy(p->mac, mac, 6);
    p->active = true;
    p->last_seen_us = reflex_hal_time_us();
    s_peer_count++;

    reflex_radio_add_peer(mac);
    peers_save_nvs();

    return REFLEX_OK;
}

size_t goose_mmio_sync_peer_count(void) { return s_peer_count; }

const reflex_peer_t *goose_mmio_sync_get_peer(size_t idx) {
    return (idx < s_peer_count) ? &s_peers[idx] : NULL;
}

uint8_t goose_mmio_sync_find_peer_by_mac(const uint8_t mac[6]) {
    for (size_t i = 0; i < s_peer_count; i++) {
        if (memcmp(s_peers[i].mac, mac, 6) == 0) return (uint8_t)(i + 1);
    }
    return 0;
}

reflex_err_t goose_mmio_sync_emit(goose_cell_t *cell, const char *cell_name) {
    if (!cell || !cell_name) return REFLEX_ERR_INVALID_ARG;

    return goose_atmosphere_emit_sync_arc(fnv1a(cell_name), cell->state);
}

void goose_mmio_sync_recv(const uint8_t *src_mac, uint32_t name_hash, int8_t state) {
    uint8_t peer_idx = goose_mmio_sync_find_peer_by_mac(src_mac);

    if (peer_idx == 0) {
        char auto_name[12];
        snprintf(auto_name, sizeof(auto_name), "auto_%02x%02x", src_mac[4], src_mac[5]);
        if (goose_mmio_sync_add_peer(auto_name, src_mac) == REFLEX_OK) {
            peer_idx = (uint8_t)s_peer_count;
        } else {
            return;
        }
    }

    s_peers[peer_idx - 1].last_seen_us = reflex_hal_time_us();
    s_peers[peer_idx - 1].active = true;

    uint32_t count = goonies_get_count();
    const char *peer_name = s_peers[peer_idx - 1].name;
    size_t prefix_len = GOOSE_NS_PEER_LEN + strlen(peer_name) + 1; // "peer." + name + "."

    for (uint32_t i = 0; i < count; i++) {
        const char *entry_name = goonies_get_name_by_idx(i);
        if (!entry_name) continue;
        if (strncmp(entry_name, GOOSE_NS_PEER, GOOSE_NS_PEER_LEN) != 0) continue;
        if (strncmp(entry_name + GOOSE_NS_PEER_LEN, peer_name, strlen(peer_name)) != 0) continue;
        if (entry_name[GOOSE_NS_PEER_LEN + strlen(peer_name)] != '.') continue;

        const char *remote_suffix = entry_name + prefix_len;
        if (fnv1a(remote_suffix) == name_hash) {
            reflex_tryte9_t coord;
            if (goonies_resolve(entry_name, &coord) == REFLEX_OK) {
                goose_cell_t *cell = goose_fabric_get_cell_by_coord(coord);
                if (cell && cell->peer_id == peer_idx) {
                    /* Security: reject writes to sys.* namespace */
                    if (strncmp(remote_suffix, GOOSE_NS_SYS, GOOSE_NS_SYS_LEN) == 0) return;
                    cell->state = state;
                }
            }
            return;
        }
    }
}

void goose_mmio_sync_staleness_check(void) {
    uint64_t now = reflex_hal_time_us();

    for (size_t i = 0; i < s_peer_count; i++) {
        if (!s_peers[i].active) continue;
        if (now - s_peers[i].last_seen_us < MMIO_SYNC_STALENESS_US) continue;

        s_peers[i].active = false;

        uint8_t pid = (uint8_t)(i + 1);
        uint32_t count = goonies_get_count();
        for (uint32_t j = 0; j < count; j++) {
            const char *name = goonies_get_name_by_idx(j);
            if (!name || strncmp(name, GOOSE_NS_PEER, GOOSE_NS_PEER_LEN) != 0) continue;

            reflex_tryte9_t coord;
            if (goonies_resolve(name, &coord) == REFLEX_OK) {
                goose_cell_t *cell = goose_fabric_get_cell_by_coord(coord);
                if (cell && cell->peer_id == pid) {
                    cell->state = 0;
                }
            }
        }

        REFLEX_LOGW(TAG, "peer %s stale, phantom cells reset", s_peers[i].name);
    }
}
