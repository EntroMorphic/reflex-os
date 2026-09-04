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

/**
 * @brief Queue a frame for transmission.
 *
 * **`REFLEX_OK` means queued, not delivered.** This maps onto `esp_now_send`,
 * which is asynchronous: the return value reports whether the frame was
 * accepted for transmission, and nothing here surfaces the completion
 * callback. Nothing above this layer can distinguish a delivered arc from one
 * dropped on the air, which is why the mesh counts what it *receives*
 * (`mesh stat`) rather than trusting send counts.
 *
 * **`dest_mac` is advisory, and one backend ignores it.** ESP-NOW honours it.
 * The IEEE 802.15.4 backend — the blob-free mode — discards it and always
 * emits a broadcast frame (`reflex_radio_802154.c`, `build_broadcast_frame`),
 * because nothing in this mesh has needed a directed frame yet and the shim
 * does not implement short-address unicast. A caller that passes a specific
 * MAC therefore gets a broadcast in 802.15.4 mode and a unicast under ESP-NOW.
 * Do not build a confidentiality or addressing assumption on this parameter;
 * every arc the substrate sends today is a deliberate broadcast, and the Aura
 * MAC — not the destination address — is what restricts who can act on one.
 *
 * @param dest_mac Six-byte destination, or the all-`0xFF` broadcast address.
 *                 Advisory: see above.
 * @param data     Frame payload.
 * @param len      Payload length, bounded by the backend's MTU (250 bytes for
 *                 ESP-NOW).
 */
reflex_err_t reflex_radio_send(const uint8_t *dest_mac, const uint8_t *data, size_t len);

/**
 * @brief Install the receive callback, replacing any previous one.
 *
 * There is one callback for the whole system, invoked from the radio driver's
 * task rather than the caller's, so it must be cheap and must not block.
 */
reflex_err_t reflex_radio_register_recv(reflex_radio_recv_cb_t cb);

/** @brief Register a unicast peer. Broadcast needs no registration. */
reflex_err_t reflex_radio_add_peer(const uint8_t mac[6]);

#ifdef __cplusplus
}
#endif

#endif /* REFLEX_RADIO_H */
