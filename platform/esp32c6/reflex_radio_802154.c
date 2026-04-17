/**
 * @file reflex_radio_802154.c
 * @brief Reflex Radio — IEEE 802.15.4 backend (blob-free).
 *
 * Uses the ESP32-C6's dedicated 802.15.4 MAC hardware through the
 * open-source ieee802154 driver. No Wi-Fi binary blob required.
 *
 * Build with CONFIG_REFLEX_RADIO_802154=1 to use this instead of
 * the ESP-NOW backend. Both implement the same reflex_radio.h API.
 */

#include "reflex_radio.h"
#include "reflex_hal.h"
#include "esp_ieee802154.h"
#include <string.h>

#define TAG "reflex.radio.154"
#define REFLEX_154_CHANNEL   15
#define REFLEX_154_PANID     0x4F52   /* "RO" — Reflex OS */
#define REFLEX_154_MAX_FRAME 127
#define REFLEX_154_FCS_LEN   2

/* 802.15.4 broadcast frame with PAN ID compression:
 * [1 len] [2 frame_ctrl] [1 seq] [2 dst_panid] [2 dst_addr] [2 src_addr] [N payload]
 * FCS (2 bytes) is appended by hardware on TX, included in len on RX.
 * Header: 10 bytes (after the length byte). */
#define FRAME_HDR_LEN 10

static reflex_radio_recv_cb_t s_user_cb = NULL;
static uint8_t s_seq_num = 0;
static uint16_t s_local_addr = 0;

static void build_broadcast_frame(uint8_t *frame, const uint8_t *payload, size_t payload_len) {
    /* Length byte = MAC header + payload + FCS (hardware appends FCS
     * but the length field must account for it). */
    frame[0] = (uint8_t)(FRAME_HDR_LEN + payload_len + REFLEX_154_FCS_LEN);
    /* Frame control: data frame, PAN ID compression, short addresses */
    frame[1] = 0x41;  /* frame type=data, panid_comp=1 */
    frame[2] = 0x88;  /* dst=short, src=short */
    frame[3] = s_seq_num++;
    /* Dst PAN ID + broadcast address */
    frame[4] = (uint8_t)(REFLEX_154_PANID & 0xFF);
    frame[5] = (uint8_t)(REFLEX_154_PANID >> 8);
    frame[6] = 0xFF;
    frame[7] = 0xFF;
    /* Src short address (PAN ID compressed — same as dst) */
    frame[8] = (uint8_t)(s_local_addr & 0xFF);
    frame[9] = (uint8_t)(s_local_addr >> 8);
    /* Payload (starts at byte 10, after the length byte) */
    memcpy(&frame[1 + FRAME_HDR_LEN], payload, payload_len);
}

/* Called by the 802.15.4 driver when a frame is received. */
void esp_ieee802154_receive_done(uint8_t *frame, esp_ieee802154_frame_info_t *frame_info) {
    (void)frame_info;
    if (!frame) return;

    uint8_t frame_len = frame[0];
    if (frame_len <= FRAME_HDR_LEN + REFLEX_154_FCS_LEN) {
        esp_ieee802154_receive_handle_done(frame);
        return;
    }

    /* Self-arc suppression: check if the source short address is ours.
     * Done here in the radio layer so the atmosphere protocol never
     * sees its own packets — regardless of MAC format. */
    uint16_t src_short = ((uint16_t)frame[9] << 8) | frame[8];
    if (src_short == s_local_addr) {
        esp_ieee802154_receive_handle_done(frame);
        return;
    }

    /* Copy payload to stack before releasing the driver's RX buffer.
     * The atmosphere callback does HMAC + replay cache + goonies
     * resolution — too slow to hold the hardware buffer. */
    int payload_len = frame_len - FRAME_HDR_LEN - REFLEX_154_FCS_LEN;
    if (payload_len <= 0 || payload_len > 114) {
        esp_ieee802154_receive_handle_done(frame);
        return;
    }
    uint8_t payload_copy[114];
    memcpy(payload_copy, &frame[1 + FRAME_HDR_LEN], payload_len);

    /* Build src_addr in the same 6-byte format the atmosphere expects.
     * Bytes 0-3 = 0, bytes 4-5 = short address. */
    uint8_t src_addr[6] = {0};
    src_addr[4] = frame[8];
    src_addr[5] = frame[9];

    /* Release the driver buffer before calling the user callback. */
    esp_ieee802154_receive_handle_done(frame);

    if (s_user_cb) {
        reflex_radio_recv_info_t info = { .src_addr = src_addr };
        s_user_cb(&info, payload_copy, payload_len);
    }
}

/* Called by the driver on TX complete — re-enter RX mode. */
void esp_ieee802154_transmit_done(const uint8_t *frame, const uint8_t *ack,
                                  esp_ieee802154_frame_info_t *ack_frame_info) {
    (void)frame; (void)ack; (void)ack_frame_info;
    esp_ieee802154_receive();
}

void esp_ieee802154_transmit_failed(const uint8_t *frame, esp_ieee802154_tx_error_t error) {
    (void)frame; (void)error;
    esp_ieee802154_receive();
}

reflex_err_t reflex_radio_init(void) {
    uint8_t mac[6];
    reflex_hal_mac_read(mac);
    s_local_addr = ((uint16_t)mac[4] << 8) | mac[5];

    esp_ieee802154_enable();
    esp_ieee802154_set_panid(REFLEX_154_PANID);
    esp_ieee802154_set_short_address(s_local_addr);
    esp_ieee802154_set_channel(REFLEX_154_CHANNEL);
    esp_ieee802154_set_promiscuous(true);
    esp_ieee802154_receive();

    REFLEX_LOGI(TAG, "802.15.4 radio: ch=%d panid=0x%04x addr=0x%04x",
                REFLEX_154_CHANNEL, REFLEX_154_PANID, s_local_addr);
    return REFLEX_OK;
}

reflex_err_t reflex_radio_send(const uint8_t *dest_mac, const uint8_t *data, size_t len) {
    (void)dest_mac;
    if (1 + FRAME_HDR_LEN + len + REFLEX_154_FCS_LEN > REFLEX_154_MAX_FRAME)
        return REFLEX_ERR_INVALID_SIZE;

    uint8_t frame[REFLEX_154_MAX_FRAME];
    build_broadcast_frame(frame, data, len);
    /* CCA=false: transmit without channel-busy check. Acceptable for
     * our low-rate mesh (~5 Hz). Receive mode re-entered via
     * transmit_done/transmit_failed callbacks. */
    esp_ieee802154_transmit(frame, false);
    return REFLEX_OK;
}

reflex_err_t reflex_radio_register_recv(reflex_radio_recv_cb_t cb) {
    s_user_cb = cb;
    return REFLEX_OK;
}

reflex_err_t reflex_radio_add_peer(const uint8_t mac[6]) {
    (void)mac;
    return REFLEX_OK;
}
