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
 * The IEEE 802.15.4 backend discards it and always
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

/** @brief The 802.15.4 MAC's registers, read through Reflex's own SoC constants.
 *
 * Groundwork for Tier D, and a measurement rather than a feature. Reflex does
 * not drive this peripheral — ESP-IDF's ieee802154 driver does — so every field
 * here was written by that driver. Reading them back through
 * `reflex_soc_esp32c6.h` proves on silicon what `make soc-bridge` proves at
 * compile time: that Reflex's register map points at the real peripheral. A
 * wrong address does not fail to build, and it does not fail to read either —
 * it returns whatever else lives there.
 *
 * @param out Filled in; `valid` is false on backends that have no such MAC.
 */
typedef struct {
    bool valid;    /**< false when this backend is not the 802.15.4 MAC */
    uint32_t base; /**< the peripheral base Reflex believes in */
    uint32_t channel;
    uint32_t panid;
    uint32_t short_addr;
    uint32_t ctrl_cfg;
    uint32_t coex_pti; /**< COEX_PTI: load-bearing for receive, see the ledger */
    uint32_t event_en;
    uint32_t event_status;
    uint32_t rx_status;
    uint32_t tx_status;
    uint32_t txdma_addr;
    uint32_t rxdma_addr;
} reflex_radio_reg_snapshot_t;

void reflex_radio_reg_snapshot(reflex_radio_reg_snapshot_t *out);

/** @brief Read @p words consecutive words from the 802.15.4 peripheral base.
 *
 * Ground truth for a Reflex-owned MAC. Reads only; zero-filled on backends
 * that have no such peripheral.
 */
void reflex_radio_reg_dump(uint32_t *out, int words);

/** @brief Write the 802.15.4 coexistence PTI register. Diagnostic.
 *
 * The one register this file will write, and it exists because of a
 * measurement: building with CONFIG_ESP_COEX_SW_COEXIST_ENABLE=n removes 27
 * blob symbols and 10 KB, and stops the radio receiving entirely — twice, with
 * transmission unaffected. The only configuration register that differs between
 * the two builds is this one (0x83 with coex, 0x11 without). This exists to
 * find out whether that value is the whole difference.
 *
 * Refused, rather than performed, in two cases. On a backend with no such
 * MAC there is no register to write. And in a build where coexistence *is*
 * compiled in, the blob owns this register and rewrites it on every transmit
 * and receive scene change — a write from here would be overwritten at an
 * unpredictable moment, which is worse than not writing at all because it looks
 * like it worked. Bits outside the three defined fields are refused too.
 *
 * @return REFLEX_OK, REFLEX_ERR_NOT_SUPPORTED, or REFLEX_ERR_INVALID_STATE /
 *         REFLEX_ERR_INVALID_ARG.
 */
reflex_err_t reflex_radio_set_coex_pti(uint32_t value);

#ifdef __cplusplus
}
#endif

#endif /* REFLEX_RADIO_H */
