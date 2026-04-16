#ifndef REFLEX_RADIO_H
#define REFLEX_RADIO_H

/**
 * @file reflex_radio.h
 * @brief Reflex OS portable broadcast mesh transport.
 *
 * Abstracts the radio layer used by the GOOSE atmospheric mesh.
 * ESP-IDF backend uses ESP-NOW over Wi-Fi STA mode.
 */

#include "reflex_types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    const uint8_t *src_addr;
} reflex_radio_recv_info_t;

typedef void (*reflex_radio_recv_cb_t)(const reflex_radio_recv_info_t *info,
                                       const uint8_t *data, int len);

reflex_err_t reflex_radio_init(void);
reflex_err_t reflex_radio_send(const uint8_t *dest_mac, const uint8_t *data, size_t len);
reflex_err_t reflex_radio_register_recv(reflex_radio_recv_cb_t cb);
reflex_err_t reflex_radio_add_peer(const uint8_t mac[6]);

#ifdef __cplusplus
}
#endif

#endif /* REFLEX_RADIO_H */
