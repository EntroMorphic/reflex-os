/**
 * @file reflex_radio_802154.c
 * @brief Reflex Radio — IEEE 802.15.4 backend (blob-free).
 *
 * Uses the ESP32-C6's dedicated 802.15.4 MAC hardware through the
 * open-source ieee802154 driver. No Wi-Fi binary blob required.
 *
 * The atmospheric mesh protocol (GOOSE arcs) is agnostic to the
 * radio layer — it sends/receives fixed-size packets. This backend
 * wraps those packets in 802.15.4 broadcast frames.
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
#define REFLEX_154_ADDR      0xFFFF   /* broadcast by default */
#define REFLEX_154_MAX_FRAME 127

static reflex_radio_recv_cb_t s_user_cb = NULL;

/* 802.15.4 frame format for broadcast:
 * [1 len] [2 frame_ctrl] [1 seq] [2 dst_panid] [2 dst_addr] [2 src_addr] [N payload] [2 FCS]
 * With PAN ID compression: src PAN ID omitted (same as dst).
 * Total overhead: 11 bytes header + 2 FCS = 13 bytes
 * Max payload: 127 - 13 = 114 bytes (our arc packet is ~26 bytes) */

#define FRAME_HDR_SIZE 11

static uint8_t s_seq_num = 0;
static uint16_t s_local_addr = 0;

static void build_broadcast_frame(uint8_t *frame, const uint8_t *payload, size_t payload_len) {
    size_t total = FRAME_HDR_SIZE + payload_len;
    frame[0] = (uint8_t)total;  /* length byte (required by 802.15.4 driver) */
    /* Frame control: data frame, panid compression, short addresses */
    frame[1] = 0x41;  /* frame type=data(001), security=0, pending=0, ack_req=0, panid_comp=1 */
    frame[2] = 0x88;  /* dst addr mode=short(10), frame ver=0, src addr mode=short(10) */
    frame[3] = s_seq_num++;
    /* Dst PAN ID + broadcast address */
    frame[4] = (uint8_t)(REFLEX_154_PANID & 0xFF);
    frame[5] = (uint8_t)(REFLEX_154_PANID >> 8);
    frame[6] = 0xFF;  /* broadcast */
    frame[7] = 0xFF;
    /* Src short address (PAN ID compressed — same as dst) */
    frame[8] = (uint8_t)(s_local_addr & 0xFF);
    frame[9] = (uint8_t)(s_local_addr >> 8);
    /* Payload */
    memcpy(&frame[FRAME_HDR_SIZE], payload, payload_len);
}

/* Called by the 802.15.4 driver when a frame is received */
void esp_ieee802154_receive_done(uint8_t *frame, esp_ieee802154_frame_info_t *frame_info) {
    if (!s_user_cb || !frame) {
        esp_ieee802154_receive_handle_done(frame);
        return;
    }

    uint8_t frame_len = frame[0];
    if (frame_len <= FRAME_HDR_SIZE + 2) {  /* too short (header + FCS) */
        esp_ieee802154_receive_handle_done(frame);
        return;
    }

    /* Extract source address from the frame header */
    uint8_t src_addr[6] = {0};
    src_addr[4] = frame[8];   /* src short addr low (PAN ID compressed) */
    src_addr[5] = frame[9];   /* src short addr high */

    /* Payload starts after the MAC header */
    const uint8_t *payload = &frame[FRAME_HDR_SIZE];
    int payload_len = frame_len - FRAME_HDR_SIZE - 2;  /* subtract FCS */

    reflex_radio_recv_info_t info = { .src_addr = src_addr };
    s_user_cb(&info, payload, payload_len);

    esp_ieee802154_receive_handle_done(frame);
}

/* Called by the driver on TX complete — we don't need to do anything */
void esp_ieee802154_transmit_done(const uint8_t *frame, const uint8_t *ack, esp_ieee802154_frame_info_t *ack_frame_info) {
    (void)frame; (void)ack; (void)ack_frame_info;
}

void esp_ieee802154_transmit_failed(const uint8_t *frame, esp_ieee802154_tx_error_t error) {
    (void)frame; (void)error;
}

reflex_err_t reflex_radio_init(void) {
    /* Generate a local address from the chip's MAC */
    uint8_t mac[6];
    reflex_hal_mac_read(mac);
    s_local_addr = ((uint16_t)mac[4] << 8) | mac[5];

    esp_ieee802154_enable();
    esp_ieee802154_set_panid(REFLEX_154_PANID);
    esp_ieee802154_set_short_address(s_local_addr);
    esp_ieee802154_set_channel(REFLEX_154_CHANNEL);
    esp_ieee802154_set_promiscuous(true);  /* receive all frames on our PAN */

    /* Enter receive mode */
    esp_ieee802154_receive();

    REFLEX_LOGI(TAG, "802.15.4 radio: ch=%d panid=0x%04x addr=0x%04x",
                REFLEX_154_CHANNEL, REFLEX_154_PANID, s_local_addr);
    return REFLEX_OK;
}

reflex_err_t reflex_radio_send(const uint8_t *dest_mac, const uint8_t *data, size_t len) {
    (void)dest_mac;  /* always broadcast on 802.15.4 */
    if (len + FRAME_HDR_SIZE + 2 > REFLEX_154_MAX_FRAME) return REFLEX_ERR_INVALID_SIZE;

    uint8_t frame[REFLEX_154_MAX_FRAME];
    build_broadcast_frame(frame, data, len);
    /* CCA=false: transmit without checking channel busy. Acceptable for
     * our low-rate mesh (~5 Hz). Enable CCA if collision rate is high. */
    esp_ieee802154_transmit(frame, false);

    /* Re-enter receive mode after TX */
    esp_ieee802154_receive();
    return REFLEX_OK;
}

reflex_err_t reflex_radio_register_recv(reflex_radio_recv_cb_t cb) {
    s_user_cb = cb;
    return REFLEX_OK;
}

reflex_err_t reflex_radio_add_peer(const uint8_t mac[6]) {
    (void)mac;  /* 802.15.4 broadcast doesn't need peer registration */
    return REFLEX_OK;
}
