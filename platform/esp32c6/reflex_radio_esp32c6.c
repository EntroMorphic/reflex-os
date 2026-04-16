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
