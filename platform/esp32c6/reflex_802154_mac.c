/**
 * @file reflex_802154_mac.c
 * @brief Reflex's own IEEE 802.15.4 MAC. See reflex_802154_mac.h for why.
 */

#include "reflex_802154_mac.h"
#include "reflex_hal.h"
#include "reflex_regops.h"
#include "reflex_soc_esp32c6.h"
#include "reflex_rom_esp32c6.h"
#include <string.h>

#if CONFIG_IDF_TARGET_ESP32C6

/* The bring-up Reflex does not own.
 *
 * Three ESP-IDF entry points, and the independence ledger records the tier
 * increase they cause rather than hiding it. They are one-shot silicon
 * bring-up, not an ongoing state machine:
 *
 *   modem_clock_module_enable   refcounted clock gating across four domains
 *                               (802154 MAC, the BT/154 common baseband, ETM,
 *                               coexistence), shared with Wi-Fi and Bluetooth.
 *   esp_phy_enable              sequences libphy.a for RF calibration.
 *   esp_btbb_enable             sequences libbtbb.a for the baseband.
 *
 * ieee802154_txon_delay_set is declared locally: it is a libbtbb symbol with no
 * public header, called once here exactly as ESP-IDF's mac_init calls it.
 */
#include "esp_private/phy.h"

/* Bedrock, reached directly rather than through ESP-IDF's wrapper.
 *
 * Both are defined only inside a vendor binary — check_independence.py
 * classifies them by asking nm, not by being told — so they are the far side of
 * the dotted line and there is nothing to reimplement.
 *
 * esp_btbb_enable() was the wrapper around the second. Reading it, everything
 * else it does is refcounting Reflex does not need and sleep-retention
 * registration behind SOC_PM_MODEM_RETENTION_BY_REGDMA && FREERTOS_USE_TICKLESS_IDLE,
 * which this configuration does not have. What remains is one call with a
 * constant argument, and keeping a header for that would be borrowing a
 * dependency for the sake of not typing a declaration. */
extern void ieee802154_txon_delay_set(void);
extern void bt_bb_v2_init_cmplx(int print_version);
#define REFLEX_BTBB_ENABLE_VERSION_PRINT 1

/* Modem clock gating, taken from ESP-IDF rather than called.
 *
 * This used to be modem_clock_module_enable(PERIPH_IEEE802154_MODULE) and
 * modem_clock_module_mac_reset(...). Both are ESP-IDF *source* — a refcounted
 * layer that tracks four clock domains across Wi-Fi, Bluetooth and 802.15.4 so
 * that enabling one does not disable another's shared clocks.
 *
 * Reflex runs neither Wi-Fi nor Bluetooth in this configuration, so there is
 * nothing to refcount against: what that layer does for 802.15.4 reduces to six
 * bits and a reset pulse. The dotted line is drawn around the vendor binaries,
 * and this is not one of them.
 *
 * The four domains ESP-IDF enables for PERIPH_IEEE802154_MODULE are
 * MODEM_CLOCK_802154_MAC, MODEM_CLOCK_BT_I154_COMMON_BB, MODEM_CLOCK_ETM and
 * MODEM_CLOCK_COEXIST, and each `configure` function is one or two bit writes:
 *
 *   802154_MAC          clk_zb_apb_en, clk_zb_mac_en   (MODEM_SYSCON.CLK_CONF)
 *   BT_I154_COMMON_BB   clk_bt_apb_en, clk_bt_en       (MODEM_SYSCON.CLK_CONF1)
 *   ETM                 clk_etm_en                     (MODEM_SYSCON.CLK_CONF)
 *   COEXIST             clk_coex_en                    (MODEM_LPCON.CLK_CONF)
 *
 * Read-modify-write throughout: these registers carry Wi-Fi and Bluetooth
 * clock enables in neighbouring bits, and a whole-word write would switch off
 * things this file knows nothing about. Not that anything else is running here
 * — but a clock-gating routine that only works because the rest of the chip is
 * idle is a trap for whoever enables Wi-Fi next.
 *
 * What is deliberately not reproduced: modem_clock_module_icg_map_init_all(),
 * which programs the PMU's clock-gating map for modem sleep states. Reflex does
 * not use modem sleep. Skipping it is safe here for a reason worth stating
 * rather than assuming: esp_phy_enable still calls modem_clock_module_enable
 * for PERIPH_PHY_MODULE, and that runs the ICG map init anyway. If the PHY
 * bring-up is ever taken too, this is the piece that comes with it.
 *
 * Equivalence checked two ways rather than argued. The three clock registers
 * read *identically* under Reflex's enable and ESP-IDF's —
 * clk_conf=0x01e00000 clk_conf1=0x0007e7ff lpcon=0x00000007 — and receive
 * throughput against a live peer matches the reference within noise: 13 frames
 * in four minutes here against 14 for ESP-IDF's, with transmission and the
 * scheduler tick unaffected in both.
 */
static void reflex_802154_clock_enable(void) {
    uint32_t c = REFLEX_REG_READ(REFLEX_MODEM_SYSCON_CLK_CONF_REG);
    c |= REFLEX_MODEM_CLK_ZB_APB_EN | REFLEX_MODEM_CLK_ZB_MAC_EN | REFLEX_MODEM_CLK_ETM_EN;
    REFLEX_REG_WRITE(REFLEX_MODEM_SYSCON_CLK_CONF_REG, c);

    uint32_t c1 = REFLEX_REG_READ(REFLEX_MODEM_SYSCON_CLK_CONF1_REG);
    c1 |= REFLEX_MODEM_CLK_BT_APB_EN | REFLEX_MODEM_CLK_BT_EN;
    REFLEX_REG_WRITE(REFLEX_MODEM_SYSCON_CLK_CONF1_REG, c1);

    uint32_t lp = REFLEX_REG_READ(REFLEX_MODEM_LPCON_CLK_CONF_REG);
    lp |= REFLEX_MODEM_LPCON_CLK_COEX_EN;
    REFLEX_REG_WRITE(REFLEX_MODEM_LPCON_CLK_CONF_REG, lp);
}

/* Reset the MAC: assert then release, which is what
 * modem_syscon_ll_reset_zbmac does. The ESP32-C6 has no separate APB reset for
 * this block — ESP-IDF's _zbmac_apb counterpart is an empty function on this
 * target — so one bit is the whole operation. */
static void reflex_802154_mac_reset(void) {
    uint32_t r = REFLEX_REG_READ(REFLEX_MODEM_SYSCON_RST_CONF_REG);
    REFLEX_REG_WRITE(REFLEX_MODEM_SYSCON_RST_CONF_REG, r | REFLEX_MODEM_RST_ZBMAC);
    REFLEX_REG_WRITE(REFLEX_MODEM_SYSCON_RST_CONF_REG, r & ~REFLEX_MODEM_RST_ZBMAC);
}

/* Receive buffer, written by DMA.
 *
 * Layout, measured from ESP-IDF's own reader: [0] is the PHY length byte, the
 * MAC payload follows, and the last two slots hold RSSI and LQI where the FCS
 * would be — the CRC is checked in hardware and never written here. So the
 * payload is buf[1 .. len-2], RSSI is buf[len-1] and LQI is buf[len].
 *
 * Static and aligned because the radio DMAs into it; 130 bytes covers the
 * 127-byte maximum PSDU plus the length byte and the two trailing slots.
 */
#define REFLEX_154_RX_BUF_LEN 130
static uint8_t s_rx_buf[REFLEX_154_RX_BUF_LEN] __attribute__((aligned(4)));

static reflex_154_rx_cb_t s_rx_cb;
static reflex_intr_handle_t s_intr;
static reflex_154_mac_stats_t s_stats;
static volatile bool s_tx_in_flight;

/* Events Reflex's ISR is prepared to act on. Enabling more would latch status
 * bits nothing clears, and EVENT_STATUS is a *gated* register — it reports the
 * enabled events only, which is why an earlier attempt that disabled events to
 * keep an ISR quiet saw nothing at all and timed out. */
#define REFLEX_154_EVENTS_HANDLED                                                                  \
    (REFLEX_154_EVENT_TX_DONE | REFLEX_154_EVENT_RX_DONE | REFLEX_154_EVENT_TX_ABORT)

static inline void mac_cmd(uint32_t cmd) {
    REFLEX_REG_WRITE(REFLEX_154_COMMAND_REG, cmd);
}

/* Arm the receiver: point the DMA at the buffer, then start.
 *
 * Order matters. RX_START with a stale or unset RXDMA_ADDR is how a peripheral
 * writes a frame somewhere that is nobody's. */
static inline void mac_rx_start(void) {
    REFLEX_REG_WRITE(REFLEX_154_RXDMA_ADDR_REG, (uint32_t)(uintptr_t)s_rx_buf);
    mac_cmd(REFLEX_154_CMD_RX_START);
}

static void __attribute__((section(".iram1"))) mac_isr(void *arg) {
    (void)arg;
    uint32_t ev = REFLEX_REG_READ(REFLEX_154_EVENT_STATUS_REG);
    s_stats.last_events = ev;

    /* Acknowledge first. The register is write-1-to-clear, and clearing only
     * the bits just read avoids dropping an event that arrives while this
     * handler runs. */
    REFLEX_REG_WRITE(REFLEX_154_EVENT_STATUS_REG, ev & REFLEX_154_EVENTS_HANDLED);

    if (ev & REFLEX_154_EVENT_RX_DONE) {
        s_stats.rx_done++;
        uint8_t len = s_rx_buf[0];
        /* len counts the PSDU including the two FCS bytes the hardware
         * checked and did not write, so the payload is len - 2. A length
         * outside that range means the DMA wrote something this code does not
         * understand, and passing it on would be worse than counting it. */
        if (len >= 3 && len <= 127 && s_rx_cb) {
            s_rx_cb(&s_rx_buf[1], (uint8_t)(len - 2), (int8_t)s_rx_buf[len - 1], s_rx_buf[len]);
        } else if (!s_rx_cb) {
            s_stats.rx_dropped++;
        }
        mac_rx_start();
    }

    if (ev & REFLEX_154_EVENT_TX_DONE) {
        s_stats.tx_done++;
        s_tx_in_flight = false;
        mac_rx_start(); /* back to listening, which is this node's resting state */
    }

    if (ev & REFLEX_154_EVENT_TX_ABORT) {
        s_stats.tx_abort++;
        s_tx_in_flight = false;
        mac_rx_start();
    }

    if (!(ev & REFLEX_154_EVENTS_HANDLED)) {
        s_stats.spurious++;
    }
}

reflex_err_t reflex_802154_mac_init(uint8_t channel, uint16_t panid, uint16_t short_addr,
                                    bool promiscuous) {
    if (channel < 11 || channel > 26) return REFLEX_ERR_INVALID_ARG;

    /* --- bring-up Reflex does not own --- */
    reflex_802154_clock_enable();
    esp_phy_enable(PHY_MODEM_IEEE802154);
    bt_bb_v2_init_cmplx(REFLEX_BTBB_ENABLE_VERSION_PRINT);
    reflex_802154_mac_reset();

    /* --- from here down it is Reflex's --- */

    /* Coexistence priorities, and this is the one value that is a measurement
     * rather than a field with a meaning anyone documented. With ESP-IDF's coex
     * blob these read pti=3, hw_ack=8; the blob's own "disable" path writes 1/1
     * and the radio then receives nothing at all. Reflex is not linking that
     * blob, so it writes what the blob would have. */
    REFLEX_REG_WRITE(REFLEX_154_COEX_PTI_REG,
                     (3u << REFLEX_154_COEX_PTI_S) | (8u << REFLEX_154_COEX_ACK_PTI_S));

    /* Which aborts raise an event at all. Left at none: Reflex's ISR acts on
     * TX_ABORT through EVENT_STATUS and does not need the per-reason detail,
     * and enabling reasons nothing reads only latches bits nothing clears. */
    REFLEX_REG_WRITE(REFLEX_154_RX_ABORT_INTR_CTRL_REG, 0);
    REFLEX_REG_WRITE(REFLEX_154_TX_ABORT_INTR_CTRL_REG, 0);

    /* Energy-detection sampling: averaged, matching what ESP-IDF's mac_init
     * selects. Only the field is written; the rest of the register holds CCA
     * configuration this MAC does not change. */
    uint32_t ed = REFLEX_REG_READ(REFLEX_154_ED_SCAN_CFG_REG);
    ed &= ~(REFLEX_154_ED_SAMPLE_MODE_MASK << REFLEX_154_ED_SAMPLE_MODE_S);
    ed |= (1u << REFLEX_154_ED_SAMPLE_MODE_S); /* 1 = average */
    REFLEX_REG_WRITE(REFLEX_154_ED_SCAN_CFG_REG, ed);

    /* Control configuration, read-modify-write.
     *
     * Bit 19 of this register reads as set on a working radio and is named by
     * neither the SVD nor ESP-IDF's headers. Reconstructing the word from the
     * fields that *are* named would quietly clear it. Only the named bits are
     * touched. */
    uint32_t cfg = REFLEX_REG_READ(REFLEX_154_CTRL_CFG_REG);
    cfg |= REFLEX_154_CFG_MAC_INF0_ENABLE; /* use address-filter bank 0 */
    cfg |= REFLEX_154_CFG_NO_RSS_TRK;
    cfg &= ~REFLEX_154_CFG_AUTO_ACK_TX; /* this mesh broadcasts; nothing ACKs */
    cfg &= ~REFLEX_154_CFG_AUTO_ACK_RX;
    /* Enhanced-ACK transmit comes up set in this register's reset state, and
     * read-modify-write preserved it on the first run — measured, ctrl_cfg read
     * back 0x12080082 where ESP-IDF's driver produces 0x12080080. Reflex
     * implements no ACK of any kind, so a hardware ACK generator armed behind
     * its back is a frame on the air nobody asked for. Cleared explicitly,
     * which is the point of naming every bit this MAC has an opinion about. */
    cfg &= ~REFLEX_154_CFG_ENHANCE_ACK_TX;
    cfg &= ~REFLEX_154_CFG_PAN_COORDINATOR;
    cfg &= ~REFLEX_154_CFG_RX_DONE_IDLE; /* stay in receive after a frame */
    if (promiscuous) {
        cfg |= REFLEX_154_CFG_PROMISCUOUS;
    } else {
        cfg &= ~REFLEX_154_CFG_PROMISCUOUS;
    }
    REFLEX_REG_WRITE(REFLEX_154_CTRL_CFG_REG, cfg);

    /* Addressing, into bank 0. */
    REFLEX_REG_WRITE(REFLEX_154_INF0_PAN_ID_REG, panid);
    REFLEX_REG_WRITE(REFLEX_154_INF0_SHORT_ADDR_REG, short_addr);

    /* Channel. The register holds a frequency index, not a channel number:
     * freq = (channel - 11) * 5 + 3. Writing the channel number here tunes the
     * radio between two channels and fails nothing, which is why this
     * conversion is spelled out rather than assumed. */
    uint32_t freq = (uint32_t)(channel - 11) * 5u + 3u;
    uint32_t ch = REFLEX_REG_READ(REFLEX_154_CHANNEL_REG);
    ch &= ~(REFLEX_154_CHANNEL_HOP_MASK << REFLEX_154_CHANNEL_HOP_S);
    ch |= (freq & REFLEX_154_CHANNEL_HOP_MASK) << REFLEX_154_CHANNEL_HOP_S;
    REFLEX_REG_WRITE(REFLEX_154_CHANNEL_REG, ch);

    /* Transmit power: the reset value is left alone deliberately. The mapping
     * from this field to dBm is in the PHY blob's calibration, not in any
     * register description, so a number chosen here would be a guess. */

    ieee802154_txon_delay_set();

    /* Events, then the interrupt, then receive. In that order: enabling the
     * line before the handler exists is how a level-triggered source arrives
     * with nobody to acknowledge it. */
    REFLEX_REG_WRITE(REFLEX_154_EVENT_STATUS_REG, REFLEX_REG_READ(REFLEX_154_EVENT_STATUS_REG));
    REFLEX_REG_WRITE(REFLEX_154_EVENT_EN_REG, REFLEX_154_EVENTS_HANDLED);

    reflex_err_t rc = reflex_hal_intr_alloc(REFLEX_INTR_SRC_ZB_MAC, 0, mac_isr, NULL, &s_intr);
    if (rc != REFLEX_OK) {
        REFLEX_REG_WRITE(REFLEX_154_EVENT_EN_REG, 0);
        return rc;
    }

    mac_rx_start();
    return REFLEX_OK;
}

reflex_err_t reflex_802154_mac_transmit(const uint8_t *frame) {
    if (!frame) return REFLEX_ERR_INVALID_ARG;
    if (frame[0] < 3 || frame[0] > 127) return REFLEX_ERR_INVALID_SIZE;
    if (s_tx_in_flight) return REFLEX_ERR_INVALID_STATE;

    s_tx_in_flight = true;
    /* Stop the receiver before retargeting the DMA. The peripheral has one
     * command register and one state machine; issuing TX_START while a receive
     * is in progress is what ESP-IDF's own transmit guards against. */
    mac_cmd(REFLEX_154_CMD_STOP);
    REFLEX_REG_WRITE(REFLEX_154_TXDMA_ADDR_REG, (uint32_t)(uintptr_t)frame);
    mac_cmd(REFLEX_154_CMD_TX_START);
    return REFLEX_OK;
}

void reflex_802154_mac_set_rx_cb(reflex_154_rx_cb_t cb) {
    s_rx_cb = cb;
}

void reflex_802154_mac_get_stats(reflex_154_mac_stats_t *out) {
    if (out) *out = s_stats;
}

#endif /* CONFIG_IDF_TARGET_ESP32C6 */
