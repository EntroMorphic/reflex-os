/**
 * @file reflex_radio_802154.c
 * @brief Reflex Radio — IEEE 802.15.4 backend (no Wi-Fi blob).
 *
 * Uses the ESP32-C6's dedicated 802.15.4 MAC hardware through the
 * open-source ieee802154 driver.
 *
 * This file was headed "blob-free", and that was wrong. Measured with
 * make blob-check, this backend still links libphy.a (42,385 bytes) and
 * libbtbb.a (6,181) across 14 symbols, for RF calibration and analog bring-up.
 * What it avoids is the Wi-Fi blob: the ESP-NOW image carries 804,754 bytes
 * across 139 symbols, of which libpp.a is 188,257 on its own. Sixteen times
 * less unreadable code is the true claim and a better one than the false.
 *
 * Build with CONFIG_REFLEX_RADIO_802154=1 to use this instead of
 * the ESP-NOW backend. Both implement the same reflex_radio.h API.
 */

#include "reflex_radio.h"
#include "reflex_hal.h"
#include "reflex_soc_esp32c6.h"
#include "reflex_regops.h"
#include "esp_ieee802154.h"
#include <string.h>

/* Coexistence may only be compiled out when nothing else uses the radio, and
 * that condition is checked in platform/esp32c6/CMakeLists.txt rather than here.
 *
 * The first version of this guard was `#if !COEX && defined(CONFIG_ESP_WIFI_ENABLED)`
 * and it refused every 802.15.4 build. Measured: CONFIG_ESP_WIFI_ENABLED=y is
 * ESP-IDF's default for any chip whose silicon has Wi-Fi, and says nothing about
 * whether Wi-Fi runs. What decides that here is net/CMakeLists.txt, which
 * excludes wifi.c when CONFIG_REFLEX_RADIO_802154 is set — compiled 0 times in
 * both 802.15.4 builds and once in the default one. The real invariant is a
 * build-system fact and is enforced where it lives. */

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

/* TX completion callbacks.
 *
 * These must not call esp_ieee802154_receive(). Both used to, to put the radio
 * back into RX after a transmit, and that recursed until the stack was gone:
 *
 *   esp_ieee802154_transmit_failed        (this file)
 *     -> esp_ieee802154_receive
 *       -> ieee802154_receive
 *         -> rx_init
 *           -> stop_current_operation     (aborts the in-flight TX)
 *             -> ieee802154_ll_clear_events
 *               -> ieee802154_inner_transmit_failed
 *                 -> esp_ieee802154_transmit_failed   <- back here
 *
 * The driver signals transmit-failed from inside its own event dispatch, and
 * re-entering the driver from that callback makes it signal again. Read off a
 * board as a repeating seven-frame pattern in the stack dump, ending in a
 * stack-protection panic and then Boot0's loop protection halting the device.
 * It cost nothing at compile time, which is why CI built this configuration
 * green for its entire existence without ever running it.
 *
 * The driver already returns to RX on its own: next_operation() calls
 * enable_rx() when the rx_when_idle PIB flag is set. That flag defaults to
 * false, which is what the manual re-arm was compensating for. Setting it once
 * in reflex_radio_init is both the idiomatic answer and the one that removes
 * the re-entrancy rather than guarding it.
 *
 * The callbacks stay, because the driver declares them weak and calls them
 * unconditionally; they simply have nothing to do now. */
void esp_ieee802154_transmit_done(const uint8_t *frame, const uint8_t *ack,
                                  esp_ieee802154_frame_info_t *ack_frame_info) {
    (void)frame;
    (void)ack;
    (void)ack_frame_info;
}

void esp_ieee802154_transmit_failed(const uint8_t *frame, esp_ieee802154_tx_error_t error) {
    (void)frame;
    (void)error;
}

reflex_err_t reflex_radio_init(void) {
    uint8_t mac[6];
    reflex_hal_mac_read(mac);
    s_local_addr = ((uint16_t)mac[4] << 8) | mac[5];

    esp_ieee802154_enable();

#if !CONFIG_ESP_COEX_SW_COEXIST_ENABLE
    /* Restore the coexistence priorities the coex blob would have programmed.
     *
     * Without this the radio transmits and receives nothing, and it took a
     * bisection to find out why. Building with
     * CONFIG_ESP_COEX_SW_COEXIST_ENABLE=n drops libcoexist.a entirely —
     * 10,130 bytes and 5 of the image's 19 blob symbols — and the radio still
     * initialises, still reports the right channel and PAN ID, and still
     * transmits. It simply never receives a frame. Measured twice against a
     * peer board: 0 frames with coex off, 10 and 12 with it on, transmission
     * unaffected in every run.
     *
     * The whole difference is this register. `ieee802154_ll_disable_coex()`
     * writes pti=1, hw_ack_pti=1 (0x11); with the blob it holds pti=3,
     * hw_ack_pti=8 (0x83). Writing 0x83 by hand on the coex-free build brought
     * reception straight back — 11 frames in the next 45 seconds — which is
     * what turned "coex is required" into "one register value is required".
     *
     * The two values are not derivable from any public header. ESP-IDF passes
     * the blob an *event* (IEEE802154_MIDDLE and friends) and the blob decides
     * the number; 3 and 8 are what it decided here, read back out of the
     * register. They are measurements, and they are written as fields rather
     * than as 0x83 so that the layout is the bridge's business and only the
     * two numbers are ours.
     *
     * The caveat that makes this safe: coexistence arbitrates the shared radio
     * between 802.15.4, Wi-Fi and Bluetooth, and this configuration runs none
     * of the other two. A static priority is right precisely because there is
     * nothing to arbitrate against. A build that adds Wi-Fi must keep coex. */
    REFLEX_REG_WRITE(REFLEX_154_COEX_PTI_REG,
                     (3u << REFLEX_154_COEX_PTI_S) | (8u << REFLEX_154_COEX_ACK_PTI_S));
#endif
    esp_ieee802154_set_panid(REFLEX_154_PANID);
    esp_ieee802154_set_short_address(s_local_addr);
    esp_ieee802154_set_channel(REFLEX_154_CHANNEL);
    esp_ieee802154_set_promiscuous(true);
    /* Return to RX automatically after every TX. Without this the radio idles
     * (and sleeps) after transmitting, which is what the TX callbacks above
     * used to compensate for by re-entering the driver — see the comment there
     * for why that recursed. */
    esp_ieee802154_set_rx_when_idle(true);
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

/* Read the MAC's registers through Reflex's own constants.
 *
 * Nothing here writes. ESP-IDF's driver owns this peripheral and is actively
 * using it; programming a register behind that driver's back would corrupt a
 * state machine Reflex does not model yet. The point is narrower and comes
 * first: prove that Reflex's register map reaches the real peripheral.
 *
 * make soc-bridge proves each of these addresses equals ESP-IDF's macro at
 * compile time. That is a claim about headers. This is the claim about silicon,
 * and the two are not the same — a correct constant still has to be a correct
 * constant *for this chip*, reached through a peripheral whose clock is on. The
 * check that makes it evidence is that these values are not arbitrary: the
 * channel, PAN ID and short address were set by reflex_radio_init through
 * ESP-IDF's API, so reading Reflex's own addresses must return exactly what
 * Reflex asked for. Anything else and the map is wrong.
 */
void reflex_radio_reg_snapshot(reflex_radio_reg_snapshot_t *out) {
    if (!out) return;
    out->valid = true;
    out->base = REFLEX_DR_REG_IEEE802154_BASE;
    out->channel = REFLEX_REG_READ(REFLEX_154_CHANNEL_REG);
    out->panid = REFLEX_REG_READ(REFLEX_154_INF0_PAN_ID_REG);
    out->short_addr = REFLEX_REG_READ(REFLEX_154_INF0_SHORT_ADDR_REG);
    out->ctrl_cfg = REFLEX_REG_READ(REFLEX_154_CTRL_CFG_REG);
    out->coex_pti = REFLEX_REG_READ(REFLEX_154_COEX_PTI_REG);
    out->event_en = REFLEX_REG_READ(REFLEX_154_EVENT_EN_REG);
    out->event_status = REFLEX_REG_READ(REFLEX_154_EVENT_STATUS_REG);
    out->rx_status = REFLEX_REG_READ(REFLEX_154_RX_STATUS_REG);
    out->tx_status = REFLEX_REG_READ(REFLEX_154_TX_STATUS_REG);
    out->txdma_addr = REFLEX_REG_READ(REFLEX_154_TXDMA_ADDR_REG);
    out->rxdma_addr = REFLEX_REG_READ(REFLEX_154_RXDMA_ADDR_REG);
}

/* The whole peripheral, 0x000..0x184, read through Reflex's own base.
 *
 * Ground truth for a Reflex-owned MAC: this is the register state ESP-IDF's
 * driver produces for the configuration Reflex asks for, and it is what a
 * replacement has to reproduce. Measured rather than derived — the
 * initialisation sequence in ieee802154_mac_init() is readable, but what the
 * registers actually end up holding after PHY bring-up, PIB defaults and the
 * first RX command is not something to infer from source.
 *
 * Reads only. Every register in this range is read-write in the SVD with none
 * marked write-only, and the driver's own event register is write-1-to-clear
 * rather than clear-on-read, so a read does not disturb it.
 */
void reflex_radio_reg_dump(uint32_t *out, int words) {
    if (!out) return;
    for (int i = 0; i < words; i++) {
        out[i] = REFLEX_REG_READ(REFLEX_DR_REG_IEEE802154_BASE + (uint32_t)(i * 4));
    }
}

reflex_err_t reflex_radio_set_coex_pti(uint32_t value) {
#if CONFIG_ESP_COEX_SW_COEXIST_ENABLE
    /* The blob owns this register here, and rewrites it on every scene change
     * (IEEE802154_SET_TXRX_PTI on transmit, receive and the timed variants).
     * A write from here would survive until the next one of those and then
     * vanish, which is a worse failure than refusing: it would look like it
     * took effect. */
    (void)value;
    return REFLEX_ERR_INVALID_STATE;
#else
    /* Only the three defined fields. The register is 32 bits wide and 23 of
     * them are reserved; writing into reserved bits of a peripheral nobody has
     * documentation for is not a diagnostic, it is a guess. */
    const uint32_t writable = (REFLEX_154_COEX_PTI_MASK << REFLEX_154_COEX_PTI_S) |
                              (REFLEX_154_COEX_ACK_PTI_MASK << REFLEX_154_COEX_ACK_PTI_S) |
                              REFLEX_154_CLOSE_RF_SEL;
    if (value & ~writable) {
        return REFLEX_ERR_INVALID_ARG;
    }
    REFLEX_REG_WRITE(REFLEX_154_COEX_PTI_REG, value);
    return REFLEX_OK;
#endif
}
