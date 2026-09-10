/**
 * @file reflex_radio_esp32c6.c
 * @brief Reflex Radio — ESP-NOW backend.
 */

#include "reflex_radio.h"
#include "esp_now.h"
#include <string.h>

static reflex_radio_recv_cb_t s_user_cb = NULL;

static void esp_now_recv_adapter(const esp_now_recv_info_t *info,
                                 const uint8_t *data, int len) {
    if (s_user_cb) {
        reflex_radio_recv_info_t ri = { .src_addr = info->src_addr };
        s_user_cb(&ri, data, len);
    }
}

reflex_err_t reflex_radio_init(void) {
    return (reflex_err_t)esp_now_init();
}

reflex_err_t reflex_radio_send(const uint8_t *dest_mac, const uint8_t *data, size_t len) {
    return (reflex_err_t)esp_now_send(dest_mac, data, len);
}

reflex_err_t reflex_radio_register_recv(reflex_radio_recv_cb_t cb) {
    s_user_cb = cb;
    return (reflex_err_t)esp_now_register_recv_cb(esp_now_recv_adapter);
}

reflex_err_t reflex_radio_add_peer(const uint8_t mac[6]) {
    esp_now_peer_info_t peer_info = {0};
    memcpy(peer_info.peer_addr, mac, 6);
    return (reflex_err_t)esp_now_add_peer(&peer_info);
}

/* The classic ESP32 has no 802.15.4 MAC at all.
 *
 * Reported as invalid rather than as zeros, so a caller reads "this backend has
 * no such peripheral" instead of a plausible-looking all-zero register dump.
 * Implemented here and not only on the 802.15.4 backend because reflex_radio.h
 * is the contract: declaring a function for every backend and defining it for
 * one is a link error waiting for the first caller that is not fenced off, and
 * that mistake has already been made twice in this tree. */
void reflex_radio_reg_snapshot(reflex_radio_reg_snapshot_t *out) {
    if (!out) return;
    memset(out, 0, sizeof(*out));
    out->valid = false;
}
