/**
 * @file reflex_802154_mac.h
 * @brief Reflex's own IEEE 802.15.4 MAC for the ESP32-C6.
 *
 * Replaces ESP-IDF's `ieee802154` component — roughly 3,240 lines across ten
 * objects — with register programming against constants `make soc-bridge`
 * proves identical to ESP-IDF's own macros.
 *
 * What it does not replace, and cannot: the RF bring-up. `esp_phy_enable` and
 * `esp_btbb_enable` sequence a binary blob for calibration and analog
 * front-end, and `modem_clock_module_enable` is a refcounted clock-gating
 * layer shared with Wi-Fi and Bluetooth. Those are Tier D's bedrock and are
 * called, not reimplemented. Everything above them — the initialisation
 * sequence, the transmit path, the receive path and the interrupt — is Reflex's.
 *
 * Why a whole MAC rather than a smaller step: two attempts to drive the
 * transmit registers while ESP-IDF's driver was still running failed, the
 * second wedging a board badly enough to need a power cycle. ESP-IDF's driver
 * owns this peripheral's state machine, and there is no careful way to reach
 * around a running owner. Owning it outright is the only version that can be
 * made safe. See docs/independence-dependency-map.md.
 */
#ifndef REFLEX_802154_MAC_H
#define REFLEX_802154_MAC_H

#include "reflex_types.h"

/** @brief A received frame: the MAC payload, without length byte or FCS. */
typedef void (*reflex_154_rx_cb_t)(const uint8_t *psdu, uint8_t len, int8_t rssi, uint8_t lqi);

/** @brief Bring the radio up and start receiving.
 *
 * @param channel     802.15.4 channel, 11..26.
 * @param panid       PAN identifier for the address filter.
 * @param short_addr  This node's 16-bit short address.
 * @param promiscuous Accept frames that fail address filtering.
 */
reflex_err_t reflex_802154_mac_init(uint8_t channel, uint16_t panid, uint16_t short_addr,
                                    bool promiscuous);

/** @brief Transmit one frame. `frame[0]` is the PHY length byte.
 *
 * The buffer must remain valid until the transmission completes: the radio
 * reads it by DMA after this returns. Blocks only long enough to issue the
 * command.
 */
reflex_err_t reflex_802154_mac_transmit(const uint8_t *frame);

/** @brief Register the receive callback. Called from interrupt context. */
void reflex_802154_mac_set_rx_cb(reflex_154_rx_cb_t cb);

/** @brief Counters, for the shell. */
typedef struct {
    uint32_t tx_done;
    uint32_t tx_abort;
    uint32_t rx_done;
    uint32_t rx_abort;
    uint32_t rx_dropped; /**< frame arrived with no callback registered */
    uint32_t spurious;   /**< interrupt with no event bit Reflex handles */
    uint32_t last_events;

    /* Signal quality, accumulated per received frame.
     *
     * Here because the PHY bring-up is the next thing to take, and the PHY is
     * where a wrong sequence does not fail visibly: a radio can pass every
     * frame and still have degraded sensitivity or the wrong transmit power.
     * "It works" is weak evidence for that change. A distribution of RSSI and
     * LQI over a fixed two-board setup, recorded under ESP-IDF's bring-up and
     * then under Reflex's, is a comparison against a reference instead.
     *
     * Integer only — accumulated in the interrupt handler. The mean is
     * sum/count at the point of display. */
    uint32_t sig_count;
    int32_t rssi_sum;
    int8_t rssi_min;
    int8_t rssi_max;
    uint32_t lqi_sum;
    uint8_t lqi_min;
    uint8_t lqi_max;
} reflex_154_mac_stats_t;

void reflex_802154_mac_get_stats(reflex_154_mac_stats_t *out);

#endif /* REFLEX_802154_MAC_H */
