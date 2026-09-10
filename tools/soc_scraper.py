#!/usr/bin/env python3
"""Generate Reflex's own ESP32-C6 SoC register header from the vendor SVD.

Reflex reaches ESP-IDF's `soc/*_reg.h` headers for a bounded set of
constants (47 before the USB-serial-JTAG additions). They
are silicon facts, not ESP-IDF code, and every one of the register addresses
and bit positions is already present in `tools/esp32c6.svd` — the same file
`goose_scraper.py` reads to build the 12,738-node shadow atlas. Owning them is
therefore a matter of emitting what we already have, not of transcribing a
vendor header.

Provenance is the point. A mistyped register address is a silent hardware bug:
it writes somewhere real. So nothing here is hand-copied from IDF. Each entry
below names a peripheral, register and (where relevant) field in the SVD, and
the value is computed from that. The mapping table carries the one thing the
SVD cannot: that IDF's `L1_CACHE_SHUT_IBUS` is the SVD's `L1_CACHE_SHUT_BUS0`.

A handful of constants genuinely are not in the SVD — CPU memory-map bounds,
capability flags, a GPIO matrix signal index and a watchdog magic key. Those
are listed separately as literals, each with its origin, so the split between
"derived" and "asserted by hand" stays visible.

Correctness is not taken on trust: `--emit-bridge` writes a translation unit
that `_Static_assert`s every generated value against the ESP-IDF macro it
replaces. Compile that once inside an ESP-IDF build, and the equivalence is a
compile-time proof rather than a claim. See `make soc-bridge`.

Usage:
  tools/soc_scraper.py                      # regenerate the header
  tools/soc_scraper.py --emit-bridge        # regenerate the assert bridge too
  tools/soc_scraper.py --check              # fail if the header is out of date
"""

import argparse
import shutil
import subprocess
import sys
import xml.etree.ElementTree as ET
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
SVD = ROOT / "tools" / "esp32c6.svd"
HEADER = ROOT / "include" / "reflex_soc_esp32c6.h"
BRIDGE = ROOT / "tools" / "soc_assert_bridge.c"

# (reflex_name, idf_name, peripheral, register, field, note)
#   field None      -> register address       (base + addressOffset)
#   field given     -> bit mask               (1 << bitOffset), or the value
#                      mask (1<<bitWidth)-1 when `mask_of_width` is set.
REGS = [
    # --- Boot0: super-watchdog and RTC watchdog ---
    # The main low-power watchdog. Reflex arms this across a deep sleep so that
    # a sleep which never wakes resets the board instead of stranding it —
    # WDT_PAUSE_IN_SLP is the field that decides whether the net is even armed
    # while asleep, and it is the whole reason this is worth owning.
    ("REFLEX_LP_WDT_CONFIG0_REG",      "LP_WDT_CONFIG0_REG",      "LP_WDT", "WDTCONFIG0",  None, "stage actions, enable, pause-in-sleep"),
    ("REFLEX_LP_WDT_CONFIG1_REG",      "LP_WDT_CONFIG1_REG",      "LP_WDT", "CONFIG1",     None, "stage 0 timeout, in slow-clock ticks"),
    ("REFLEX_LP_WDT_FEED_REG",         "LP_WDT_FEED_REG",         "LP_WDT", "WDTFEED",     None, ""),
    ("REFLEX_LP_WDT_WPROTECT_REG",     "LP_WDT_WPROTECT_REG",     "LP_WDT", "WDTWPROTECT", None, "write the key here to unlock the rest"),
    ("REFLEX_LP_WDT_EN",               "LP_WDT_WDT_EN",           "LP_WDT", "WDTCONFIG0",  "WDT_EN", ""),
    ("REFLEX_LP_WDT_PAUSE_IN_SLP",     "LP_WDT_WDT_PAUSE_IN_SLP", "LP_WDT", "WDTCONFIG0",  "WDT_PAUSE_IN_SLP", "set means the watchdog stops while asleep; the net depends on it being clear"),
    ("REFLEX_LP_WDT_PROCPU_RESET_EN",  "LP_WDT_WDT_PROCPU_RESET_EN", "LP_WDT", "WDTCONFIG0", "WDT_PROCPU_RESET_EN", ""),
    ("REFLEX_LP_WDT_CHIP_RESET_EN",    "LP_WDT_WDT_CHIP_RESET_EN",   "LP_WDT", "WDTCONFIG0", "WDT_CHIP_RESET_EN", ""),
    ("REFLEX_LP_WDT_SWD_CONFIG_REG",   "LP_WDT_SWD_CONFIG_REG",   "LP_WDT", "SWD_CONF",    None, "super-watchdog config"),
    ("REFLEX_LP_WDT_SWD_WPROTECT_REG", "LP_WDT_SWD_WPROTECT_REG", "LP_WDT", "SWD_WPROTECT", None, "super-watchdog write protect"),
    ("REFLEX_LP_WDT_SWD_AUTO_FEED_EN", "LP_WDT_SWD_AUTO_FEED_EN", "LP_WDT", "SWD_CONF",    "SWD_AUTO_FEED_EN", ""),
    ("REFLEX_LP_WDT_INT_ENA_REG",      "LP_WDT_INT_ENA_REG",      "LP_WDT", "INT_ENA_RTC", None, "IDF drops the _RTC suffix"),
    ("REFLEX_LP_WDT_INT_CLR_REG",      "LP_WDT_INT_CLR_REG",      "LP_WDT", "INT_CLR_RTC", None, "IDF drops the _RTC suffix"),
    ("REFLEX_LP_WDT_LP_WDT_INT_ENA",   "LP_WDT_LP_WDT_INT_ENA",   "LP_WDT", "INT_ENA_RTC", "WDT_INT_ENA", ""),
    ("REFLEX_LP_WDT_LP_WDT_INT_CLR",   "LP_WDT_LP_WDT_INT_CLR",   "LP_WDT", "INT_CLR_RTC", "WDT_INT_CLR", ""),
    ("REFLEX_LP_WDT_SUPER_WDT_INT_ENA","LP_WDT_SUPER_WDT_INT_ENA","LP_WDT", "INT_ENA_RTC", "SUPER_WDT_INT_ENA", ""),
    ("REFLEX_LP_WDT_SUPER_WDT_INT_CLR","LP_WDT_SUPER_WDT_INT_CLR","LP_WDT", "INT_CLR_RTC", "SUPER_WDT_INT_CLR", ""),

    # --- Boot0: timer-group watchdog ---
    ("REFLEX_TIMG0_WDTFEED_REG",       "TIMG_WDTFEED_REG(0)",     "TIMG0",  "WDTFEED",     None, "IDF indexes by group; Boot0 only uses group 0"),
    ("REFLEX_TIMG0_WDTWPROTECT_REG",   "TIMG_WDTWPROTECT_REG(0)", "TIMG0",  "WDTWPROTECT", None, ""),
    # Both timer-group watchdogs, so the mtvec hand-off can stand them down.
    # ESP-IDF arms these — the interrupt watchdog on one group, the task
    # watchdog on the other — and once FreeRTOS is quiesced nothing feeds them.
    # Observed as `rst:0x8 (TG1_WDT_HPSYS)` a few seconds after the hand-off.
    ("REFLEX_TIMG0_WDTCONFIG0_REG",    "TIMG_WDTCONFIG0_REG(0)",  "TIMG0",  "WDTCONFIG0",  None, ""),
    ("REFLEX_TIMG1_WDTCONFIG0_REG",    "TIMG_WDTCONFIG0_REG(1)",  "TIMG1",  "WDTCONFIG0",  None, ""),
    ("REFLEX_TIMG1_WDTWPROTECT_REG",   "TIMG_WDTWPROTECT_REG(1)", "TIMG1",  "WDTWPROTECT", None, ""),
    ("REFLEX_TIMG_WDT_EN",             "TIMG_WDT_EN",             "TIMG0",  "WDTCONFIG0",  "WDT_EN", "same bit in both groups"),

    # --- Boot0: always-on domain ---
    ("REFLEX_LP_AON_STORE0_REG",       "LP_AON_STORE0_REG",       "LP_AON", "STORE0",      None, "scratch, survives deep sleep"),
    ("REFLEX_LP_AON_SYS_CFG_REG",      "LP_AON_SYS_CFG_REG",      "LP_AON", "SYS_CFG",     None, ""),
    ("REFLEX_LP_AON_HPSYS_SW_RESET",   "LP_AON_HPSYS_SW_RESET",   "LP_AON", "SYS_CFG",     "HPSYS_SW_RESET", ""),

    # --- Boot0: brown-out detector interrupts ---
    ("REFLEX_LP_ANA_INT_ENA_REG",      "LP_ANALOG_PERI_LP_ANA_LP_INT_ENA_REG", "LP_ANA", "LP_INT_ENA", None, "SVD peripheral is LP_ANA"),
    ("REFLEX_LP_ANA_INT_CLR_REG",      "LP_ANALOG_PERI_LP_ANA_LP_INT_CLR_REG", "LP_ANA", "LP_INT_CLR", None, ""),
    ("REFLEX_LP_ANA_BOD_MODE0_INT_ENA","LP_ANALOG_PERI_LP_ANA_BOD_MODE0_LP_INT_ENA", "LP_ANA", "LP_INT_ENA", "BOD_MODE0_LP_INT_ENA", ""),
    ("REFLEX_LP_ANA_BOD_MODE0_INT_CLR","LP_ANALOG_PERI_LP_ANA_BOD_MODE0_LP_INT_CLR", "LP_ANA", "LP_INT_CLR", "BOD_MODE0_LP_INT_CLR", ""),

    # --- Boot0 / HAL: power management unit ---
    ("REFLEX_PMU_HP_INT_ENA_REG",         "PMU_HP_INT_ENA_REG",         "PMU", "HP_INT_ENA", None, ""),
    ("REFLEX_PMU_SOC_WAKEUP_INT_ENA",     "PMU_SOC_WAKEUP_INT_ENA",     "PMU", "HP_INT_ENA", "SOC_WAKEUP_INT_ENA", ""),
    ("REFLEX_PMU_SOC_SLEEP_REJECT_INT_ENA","PMU_SOC_SLEEP_REJECT_INT_ENA","PMU","HP_INT_ENA", "SOC_SLEEP_REJECT_INT_ENA", ""),

    # --- Boot0: CPU debug-assist (stack guard) ---
    ("REFLEX_ASSIST_DEBUG_CORE_0_RCD_EN_REG",  "ASSIST_DEBUG_CORE_0_RCD_EN_REG",  "ASSIST_DEBUG", "CORE_0_RCD_EN", None, ""),
    ("REFLEX_ASSIST_DEBUG_CORE_0_RCD_RECORDEN","ASSIST_DEBUG_CORE_0_RCD_RECORDEN","ASSIST_DEBUG", "CORE_0_RCD_EN", "CORE_0_RCD_RECORDEN", ""),
    ("REFLEX_ASSIST_DEBUG_CORE_0_RCD_PDEBUGEN","ASSIST_DEBUG_CORE_0_RCD_PDEBUGEN","ASSIST_DEBUG", "CORE_0_RCD_EN", "CORE_0_RCD_PDEBUGEN", ""),
    # The stack-pointer watchpoint. ESP-IDF arms it with each FreeRTOS task's
    # bounds, so a scheduler that switches to its own stack trips it: observed
    # as "Guru Meditation Error: Core 0 panic'ed (Stack protection fault)" with
    # the SP outside the bounds of the task the switch started from. Reflex has
    # to be able to stand it down before taking over scheduling.
    ("REFLEX_ASSIST_DEBUG_MONTR_ENA_REG", "ASSIST_DEBUG_CORE_0_INTR_ENA_REG", "ASSIST_DEBUG", "CORE_0_MONTR_ENA", None, "stack and area watchpoint enables; IDF calls this INTR_ENA where the SVD says MONTR_ENA"),
    ("REFLEX_ASSIST_DEBUG_SP_SPILL_MIN_ENA", "ASSIST_DEBUG_CORE_0_SP_SPILL_MIN_ENA", "ASSIST_DEBUG", "CORE_0_MONTR_ENA", "CORE_0_SP_SPILL_MIN_ENA", ""),
    ("REFLEX_ASSIST_DEBUG_SP_SPILL_MAX_ENA", "ASSIST_DEBUG_CORE_0_SP_SPILL_MAX_ENA", "ASSIST_DEBUG", "CORE_0_MONTR_ENA", "CORE_0_SP_SPILL_MAX_ENA", ""),
    ("REFLEX_PCR_ASSIST_CONF_REG",             "PCR_ASSIST_CONF_REG",             "PCR", "ASSIST_CONF", None, ""),
    ("REFLEX_PCR_ASSIST_CLK_EN",               "PCR_ASSIST_CLK_EN",               "PCR", "ASSIST_CONF", "ASSIST_CLK_EN", ""),
    ("REFLEX_PCR_ASSIST_RST_EN",               "PCR_ASSIST_RST_EN",               "PCR", "ASSIST_CONF", "ASSIST_RST_EN", ""),

    # --- Boot0: cache control ---
    ("REFLEX_EXTMEM_L1_CACHE_CTRL_REG", "EXTMEM_L1_CACHE_CTRL_REG", "EXTMEM", "L1_CACHE_CTRL", None, ""),
    ("REFLEX_EXTMEM_L1_CACHE_SHUT_IBUS","EXTMEM_L1_CACHE_SHUT_IBUS","EXTMEM", "L1_CACHE_CTRL", "L1_CACHE_SHUT_BUS0", "IDF calls bus0 the IBUS"),
    ("REFLEX_EXTMEM_L1_CACHE_SHUT_DBUS","EXTMEM_L1_CACHE_SHUT_DBUS","EXTMEM", "L1_CACHE_CTRL", "L1_CACHE_SHUT_BUS1", "IDF calls bus1 the DBUS"),

    # --- Boot0: flash MMU ---
    ("REFLEX_SPI_MEM_MMU_ITEM_CONTENT_REG", "SPI_MEM_MMU_ITEM_CONTENT_REG(0)", "SPI0", "SPI_MEM_MMU_ITEM_CONTENT", None, "IDF indexes by SPI unit; Boot0 uses 0"),
    ("REFLEX_SPI_MEM_MMU_ITEM_INDEX_REG",   "SPI_MEM_MMU_ITEM_INDEX_REG(0)",   "SPI0", "SPI_MEM_MMU_ITEM_INDEX",   None, ""),
    ("REFLEX_SPI_MEM_MMU_POWER_CTRL_REG",   "SPI_MEM_MMU_POWER_CTRL_REG(0)",   "SPI0", "SPI_MEM_MMU_POWER_CTRL",   None, ""),

    # --- deep sleep / wakeup path ---
    ("REFLEX_PMU_SLP_WAKEUP_STATUS0_REG", "PMU_SLP_WAKEUP_STATUS0_REG", "PMU", "SLP_WAKEUP_STATUS0", None, "wakeup cause after deep sleep"),
    ("REFLEX_LP_AON_STORE1_REG",          "LP_AON_STORE1_REG",          "LP_AON", "STORE1", None, "scratch shared with Boot0 across a sleep-as-reset"),

    # --- systimer base (kernel scheduler + HAL time source) ---
    ("REFLEX_DR_REG_SYSTIMER_BASE",     "DR_REG_SYSTIMER_BASE",     "SYSTIMER", None, None, "peripheral base, not a register"),
    # The counter itself, read through the value-latch handshake. Verified here
    # rather than defined by hand in the HAL: the transmit timeout first used
    # the standard RISC-V cycle CSR, which reads back as zero on this part
    # because SOC_CPU_HAS_CSR_PC moves the counter to a vendor CSR. A zero
    # clock never expires a deadline, so the console spun forever and the board
    # stopped enumerating. Constants that decide whether a timeout can fire
    # belong behind the bridge.
    ("REFLEX_SYSTIMER_UNIT0_OP_REG",       "SYSTIMER_UNIT0_OP_REG",       "SYSTIMER", "UNIT0_OP",       None, "value-latch handshake"),
    ("REFLEX_SYSTIMER_UNIT0_VALUE_LO_REG", "SYSTIMER_UNIT0_VALUE_LO_REG", "SYSTIMER", "UNIT0_VALUE_LO", None, "low half of the latched count"),
    ("REFLEX_SYSTIMER_UNIT0_UPDATE",       "SYSTIMER_TIMER_UNIT0_UPDATE",      "SYSTIMER", "UNIT0_OP", "TIMER_UNIT0_UPDATE",      "write to latch"),
    ("REFLEX_SYSTIMER_UNIT0_VALUE_VALID",  "SYSTIMER_TIMER_UNIT0_VALUE_VALID", "SYSTIMER", "UNIT0_OP", "TIMER_UNIT0_VALUE_VALID", "poll until latched"),

    # --- IEEE 802.15.4 MAC (Tier D: owning the radio) ---
    #
    # Reflex does not drive this peripheral yet: ESP-IDF's ieee802154 driver
    # does, and reflex_radio_802154.c calls it through esp_ieee802154.h, which
    # is the single on-path Tier D dependency. These constants are the
    # groundwork for changing that, and they are added ahead of the driver on
    # purpose — a register map is the part that cannot be debugged by staring at
    # it, and this bridge proves every address against the vendor's own macro at
    # compile time before a single line of MAC code depends on it.
    #
    # ESP-IDF has no _REG macros for this peripheral in the usual place; they
    # live in components/soc/esp32c6/register/soc/ieee802154_reg.h, and its
    # driver reaches the registers through a struct rather than through them.
    # The macros exist and are what the struct is generated from, so they are
    # still the right thing to assert against.
    #
    # Scoped to what a minimal MAC needs — command, configuration, addressing,
    # the two DMA pointers and the event/status registers. The other 70-odd
    # registers cover ACK handling, security, multi-PAN, scan and debug, none of
    # which Reflex's broadcast mesh uses.
    ("REFLEX_DR_REG_IEEE802154_BASE",   "IEEE802154_REG_BASE",              "IEEE802154", None,               None, "peripheral base, not a register"),
    ("REFLEX_154_COMMAND_REG",          "IEEE802154_COMMAND_REG",           "IEEE802154", "COMMAND",          None, "TX/RX/stop commands"),
    ("REFLEX_154_CTRL_CFG_REG",         "IEEE802154_CTRL_CFG_REG",          "IEEE802154", "CTRL_CFG",         None, "promiscuous, auto-ACK, coordinator"),
    # Holds a frequency index, NOT a channel number: freq = (channel - 11) * 5 + 3,
    # so 802.15.4 channel 15 reads back as 23. Measured on silicon before any
    # MAC code depended on it, and worth the note — the address is right and
    # writing a channel number here would tune the radio to the wrong frequency
    # without failing anything. That is the same class of error the bridge
    # exists to catch, one level up: a correct address with a wrong encoding.
    ("REFLEX_154_CHANNEL_REG",          "IEEE802154_CHANNEL_REG",           "IEEE802154", "CHANNEL",          None, "frequency index, not channel: (ch-11)*5+3"),
    ("REFLEX_154_TX_POWER_REG",         "IEEE802154_TX_POWER_REG",          "IEEE802154", "TX_POWER",         None, ""),
    ("REFLEX_154_INF0_SHORT_ADDR_REG",  "IEEE802154_INF0_SHORT_ADDR_REG",   "IEEE802154", "INF0_SHORT_ADDR",  None, "PAN info bank 0 is the one a single-PAN node uses"),
    ("REFLEX_154_INF0_PAN_ID_REG",      "IEEE802154_INF0_PAN_ID_REG",       "IEEE802154", "INF0_PAN_ID",      None, ""),
    ("REFLEX_154_EVENT_EN_REG",         "IEEE802154_EVENT_EN_REG",          "IEEE802154", "EVENT_EN",         None, "which events raise the interrupt"),
    ("REFLEX_154_EVENT_STATUS_REG",     "IEEE802154_EVENT_STATUS_REG",      "IEEE802154", "EVENT_STATUS",     None, "write-1-to-clear"),
    ("REFLEX_154_TXDMA_ADDR_REG",       "IEEE802154_TXDMA_ADDR_REG",        "IEEE802154", "TXDMA_ADDR",       None, "pointer to the frame to send"),
    ("REFLEX_154_RXDMA_ADDR_REG",       "IEEE802154_RXDMA_ADDR_REG",        "IEEE802154", "RXDMA_ADDR",       None, "pointer to the receive buffer"),
    ("REFLEX_154_RX_STATUS_REG",        "IEEE802154_RX_STATUS_REG",         "IEEE802154", "RX_STATUS",        None, ""),
    ("REFLEX_154_TX_STATUS_REG",        "IEEE802154_TX_STATUS_REG",         "IEEE802154", "TX_STATUS",        None, ""),
    # Measured, not guessed. Tracing ESP-IDF's driver from the nine API calls
    # reflex_radio_802154.c makes, plus ieee802154_isr and ieee802154_mac_init,
    # through the LL inlines to the register struct gives exactly these
    # registers. RX_LENGTH was in an earlier version of this list on the
    # assumption a receiver needs it; nothing in ESP-IDF's driver or LL touches
    # it, because the frame length is the first byte of the DMA buffer. Removed.
    ("REFLEX_154_ED_SCAN_DURATION_REG",  "IEEE802154_ED_SCAN_DURATION_REG",  "IEEE802154", "ED_SCAN_DURATION",  None, "written by mac_init"),
    ("REFLEX_154_ED_SCAN_CFG_REG",       "IEEE802154_ED_SCAN_CFG_REG",       "IEEE802154", "ED_SCAN_CFG",       None, "ED sample mode, set by mac_init"),
    ("REFLEX_154_RX_ABORT_INTR_CTRL_REG","IEEE802154_RX_ABORT_INTR_CTRL_REG","IEEE802154", "RX_ABORT_INTR_CTRL",None, "which rx aborts raise an event"),
    ("REFLEX_154_ACK_FRAME_PENDING_EN_REG","IEEE802154_ACK_FRAME_PENDING_EN_REG","IEEE802154","ACK_FRAME_PENDING_EN",None,""),
    # Not optional, and that is the measurement that cost the most to get:
    # a build with CONFIG_ESP_COEX_SW_COEXIST_ENABLE=n receives nothing at all.
    ("REFLEX_154_COEX_PTI_REG",          "IEEE802154_COEX_PTI_REG",          "IEEE802154", "COEX_PTI",          None, "coexistence traffic priority; RX dies without it"),
    # A bit mask, so it belongs here and not in WIDTH_MASKS: that list yields
    # (1 << width) - 1, which for a 1-bit field is 1, and ESP-IDF's macro is
    # BIT(8). The bridge caught exactly that divergence when it was put in the
    # wrong list — which is the whole reason the bridge exists.
    ("REFLEX_154_CLOSE_RF_SEL",          "IEEE802154_CLOSE_RF_SEL",          "IEEE802154", "COEX_PTI", "CLOSE_RF_SEL", "the only other defined bit of COEX_PTI"),
    ("REFLEX_154_TX_ABORT_INTR_CTRL_REG","IEEE802154_TX_ABORT_INTERRUPT_CONTROL_REG","IEEE802154","TX_ABORT_INTERRUPT_CONTROL",None,""),
    ("REFLEX_154_ENHANCE_ACK_CFG_REG",   "IEEE802154_ENHANCE_ACK_CFG_REG",   "IEEE802154", "ENHANCE_ACK_CFG",   None, ""),
    ("REFLEX_154_SEC_CTRL_REG",          "IEEE802154_SEC_CTRL_REG",          "IEEE802154", "SEC_CTRL",          None, "touched by the transmit path even unused"),

    # --- USB-serial-JTAG console (Tier E: owning console RX) ---
    # The SVD calls this peripheral USB_DEVICE; IDF calls it USB_SERIAL_JTAG.
    # Same base, different name, which is exactly what this table is for.
    ("REFLEX_DR_REG_USB_SERIAL_JTAG_BASE", "DR_REG_USB_SERIAL_JTAG_BASE", "USB_DEVICE", None, None, "peripheral base; SVD name is USB_DEVICE"),
    ("REFLEX_USJ_EP1_REG",              "USB_SERIAL_JTAG_EP1_REG",      "USB_DEVICE", "EP1",      None, "FIFO data register, both directions"),
    ("REFLEX_USJ_EP1_CONF_REG",         "USB_SERIAL_JTAG_EP1_CONF_REG", "USB_DEVICE", "EP1_CONF", None, ""),
    ("REFLEX_USJ_INT_RAW_REG",          "USB_SERIAL_JTAG_INT_RAW_REG",  "USB_DEVICE", "INT_RAW",  None, ""),
    ("REFLEX_USJ_INT_ST_REG",           "USB_SERIAL_JTAG_INT_ST_REG",   "USB_DEVICE", "INT_ST",   None, ""),
    ("REFLEX_USJ_INT_ENA_REG",          "USB_SERIAL_JTAG_INT_ENA_REG",  "USB_DEVICE", "INT_ENA",  None, ""),
    ("REFLEX_USJ_INT_CLR_REG",          "USB_SERIAL_JTAG_INT_CLR_REG",  "USB_DEVICE", "INT_CLR",  None, ""),
    ("REFLEX_USJ_OUT_EP_DATA_AVAIL",    "USB_SERIAL_JTAG_SERIAL_OUT_EP_DATA_AVAIL", "USB_DEVICE", "EP1_CONF", "SERIAL_OUT_EP_DATA_AVAIL", "host-to-device byte waiting"),
    ("REFLEX_USJ_OUT_RECV_PKT_INT",     "USB_SERIAL_JTAG_SERIAL_OUT_RECV_PKT_INT_ENA", "USB_DEVICE", "INT_ENA", "SERIAL_OUT_RECV_PKT_INT_ENA", "same bit across RAW/ST/ENA/CLR"),

    # --- LEDC, and the PCR gating that must be released before it responds ---
    # Tier E: replacing driver/ledc.h for the one channel `bonsai exp4` drives.
    # The SVD names these as register arrays (CH%s_CONF0), so channel 0 and
    # timer 0 resolve at the array's own offset — which the bridge then proves
    # equal to LEDC_CH0_CONF0_REG and friends.
    ("REFLEX_DR_REG_LEDC_BASE",      "DR_REG_LEDC_BASE",      "LEDC", None,           None, "peripheral base, not a register"),
    ("REFLEX_LEDC_CH0_CONF0_REG",    "LEDC_CH0_CONF0_REG",    "LEDC", "CH%s_CONF0",   None, "channel 0"),
    ("REFLEX_LEDC_CH0_HPOINT_REG",   "LEDC_CH0_HPOINT_REG",   "LEDC", "CH%s_HPOINT",  None, ""),
    ("REFLEX_LEDC_CH0_DUTY_REG",     "LEDC_CH0_DUTY_REG",     "LEDC", "CH%s_DUTY",    None, "duty is stored shifted left by 4"),
    ("REFLEX_LEDC_CH0_CONF1_REG",    "LEDC_CH0_CONF1_REG",    "LEDC", "CH%s_CONF1",   None, ""),
    ("REFLEX_LEDC_TIMER0_CONF_REG",  "LEDC_TIMER0_CONF_REG",  "LEDC", "TIMER%s_CONF", None, "timer 0"),
    ("REFLEX_LEDC_SIG_OUT_EN",       "LEDC_SIG_OUT_EN_CH0",   "LEDC", "CH%s_CONF0",   "SIG_OUT_EN", ""),
    ("REFLEX_LEDC_CH_PARA_UP",       "LEDC_PARA_UP_CH0",      "LEDC", "CH%s_CONF0",   "PARA_UP",    "latch channel config"),
    ("REFLEX_LEDC_DUTY_START",       "LEDC_DUTY_START_CH0",   "LEDC", "CH%s_CONF1",   "DUTY_START", ""),
    ("REFLEX_LEDC_TIMER_RST",        "LEDC_TIMER0_RST",       "LEDC", "TIMER%s_CONF", "RST",        "reads high out of reset"),
    ("REFLEX_LEDC_TIMER_PARA_UP",    "LEDC_TIMER0_PARA_UP",   "LEDC", "TIMER%s_CONF", "PARA_UP",    "latch timer config"),
    ("REFLEX_PCR_LEDC_CONF_REG",      "PCR_LEDC_CONF_REG",      "PCR", "LEDC_CONF",      None, "peripheral clock and reset"),
    ("REFLEX_PCR_LEDC_SCLK_CONF_REG", "PCR_LEDC_SCLK_CONF_REG", "PCR", "LEDC_SCLK_CONF", None, "source clock gate and select"),
    ("REFLEX_PCR_LEDC_CLK_EN",        "PCR_LEDC_CLK_EN",        "PCR", "LEDC_CONF",      "LEDC_CLK_EN",  ""),
    ("REFLEX_PCR_LEDC_RST_EN",        "PCR_LEDC_RST_EN",        "PCR", "LEDC_CONF",      "LEDC_RST_EN",  "asserted means held in reset"),
    ("REFLEX_PCR_LEDC_SCLK_EN",       "PCR_LEDC_SCLK_EN",       "PCR", "LEDC_SCLK_CONF", "LEDC_SCLK_EN", ""),

    # --- PCNT, for the edge counting `bonsai exp5` does ---
    # Tier E: replacing driver/pulse_cnt.h for unit 0, channel 0. IDF suffixes
    # its field macros with the unit (PCNT_FILTER_EN_U0) where the SVD names the
    # register as an array and the field plainly (U%s_CONF0.FILTER_EN); the
    # count and its reset diverge further still, IDF calling them PULSE_CNT
    # where the SVD says CNT. Recording those is what this table is for.
    ("REFLEX_DR_REG_PCNT_BASE",   "DR_REG_PCNT_BASE",   "PCNT", None,        None, "peripheral base, not a register"),
    ("REFLEX_PCNT_U0_CONF0_REG",  "PCNT_U0_CONF0_REG",  "PCNT", "U%s_CONF0", None, "unit 0: filter and per-channel modes"),
    ("REFLEX_PCNT_U0_CONF1_REG",  "PCNT_U0_CONF1_REG",  "PCNT", "U%s_CONF1", None, "watch thresholds"),
    ("REFLEX_PCNT_U0_CONF2_REG",  "PCNT_U0_CONF2_REG",  "PCNT", "U%s_CONF2", None, "count limits"),
    ("REFLEX_PCNT_U0_CNT_REG",    "PCNT_U0_CNT_REG",    "PCNT", "U%s_CNT",   None, "the count itself"),
    ("REFLEX_PCNT_CTRL_REG",      "PCNT_CTRL_REG",      "PCNT", "CTRL",      None, "reset and pause, all units"),
    ("REFLEX_PCNT_FILTER_EN",     "PCNT_FILTER_EN_U0",  "PCNT", "U%s_CONF0", "FILTER_EN",    "set out of reset"),
    ("REFLEX_PCNT_THR_ZERO_EN",   "PCNT_THR_ZERO_EN_U0",   "PCNT", "U%s_CONF0", "THR_ZERO_EN",   "watch events, all set out of reset"),
    ("REFLEX_PCNT_THR_H_LIM_EN",  "PCNT_THR_H_LIM_EN_U0",  "PCNT", "U%s_CONF0", "THR_H_LIM_EN",  ""),
    ("REFLEX_PCNT_THR_L_LIM_EN",  "PCNT_THR_L_LIM_EN_U0",  "PCNT", "U%s_CONF0", "THR_L_LIM_EN",  ""),
    ("REFLEX_PCNT_THR_THRES0_EN", "PCNT_THR_THRES0_EN_U0", "PCNT", "U%s_CONF0", "THR_THRES0_EN", ""),
    ("REFLEX_PCNT_THR_THRES1_EN", "PCNT_THR_THRES1_EN_U0", "PCNT", "U%s_CONF0", "THR_THRES1_EN", ""),
    ("REFLEX_PCNT_CNT_RST_U0",    "PCNT_PULSE_CNT_RST_U0", "PCNT", "CTRL",   "CNT_RST_U0",   "held asserted keeps the count at zero"),
    ("REFLEX_PCNT_CNT_PAUSE_U0",  "PCNT_CNT_PAUSE_U0",  "PCNT", "CTRL",      "CNT_PAUSE_U0", ""),
    ("REFLEX_PCR_PCNT_CONF_REG",  "PCR_PCNT_CONF_REG",  "PCR",  "PCNT_CONF", None, "peripheral clock and reset"),
    ("REFLEX_PCR_PCNT_CLK_EN",    "PCR_PCNT_CLK_EN",    "PCR",  "PCNT_CONF", "PCNT_CLK_EN",  ""),
    ("REFLEX_PCR_PCNT_RST_EN",    "PCR_PCNT_RST_EN",    "PCR",  "PCNT_CONF", "PCNT_RST_EN",  "asserted means held in reset"),

    # --- RMT, the last peripheral Tier E borrows ---
    # IDF names the transmit configuration RMT_CH0CONF0_REG where the SVD calls
    # it CH%s_TX_CONF0, and suffixes its fields with the channel; the bridge
    # proves the two describe the same register regardless.
    ("REFLEX_DR_REG_RMT_BASE",     "DR_REG_RMT_BASE",     "RMT", None,             None, "peripheral base, not a register"),
    ("REFLEX_RMT_CH0_TX_CONF0_REG","RMT_CH0CONF0_REG",    "RMT", "CH%s_TX_CONF0",  None, "channel 0 transmit config"),
    ("REFLEX_RMT_CH0DATA_REG",     "RMT_CH0DATA_REG",     "RMT", "CH%sDATA",       None, "the FIFO window onto channel memory"),
    ("REFLEX_RMT_CH0_TX_LIM_REG",  "RMT_CH0_TX_LIM_REG",  "RMT", "CH%s_TX_LIM",    None, ""),
    ("REFLEX_RMT_SYS_CONF_REG",    "RMT_SYS_CONF_REG",    "RMT", "SYS_CONF",       None, "clock source and the FIFO/memory access mode; IDF doubles the prefix on its SCLK fields (RMT_RMT_SCLK_*)"),
    ("REFLEX_RMT_INT_RAW_REG",     "RMT_INT_RAW_REG",     "RMT", "INT_RAW",        None, ""),
    ("REFLEX_RMT_INT_CLR_REG",     "RMT_INT_CLR_REG",     "RMT", "INT_CLR",        None, ""),
    ("REFLEX_RMT_TX_START",        "RMT_TX_START_CH0",    "RMT", "CH%s_TX_CONF0",  "TX_START",     ""),
    ("REFLEX_RMT_MEM_RD_RST",      "RMT_MEM_RD_RST_CH0",  "RMT", "CH%s_TX_CONF0",  "MEM_RD_RST",   "rewind the read pointer"),
    ("REFLEX_RMT_APB_MEM_RST",     "RMT_APB_MEM_RST_CH0", "RMT", "CH%s_TX_CONF0",  "APB_MEM_RST",  "rewind the write pointer"),
    ("REFLEX_RMT_TX_STOP",         "RMT_TX_STOP_CH0",     "RMT", "CH%s_TX_CONF0",  "TX_STOP",      ""),
    ("REFLEX_RMT_IDLE_OUT_EN",     "RMT_IDLE_OUT_EN_CH0", "RMT", "CH%s_TX_CONF0",  "IDLE_OUT_EN",  ""),
    ("REFLEX_RMT_MEM_TX_WRAP_EN",  "RMT_MEM_TX_WRAP_EN_CH0","RMT","CH%s_TX_CONF0", "MEM_TX_WRAP_EN",""),
    ("REFLEX_RMT_CARRIER_EFF_EN",  "RMT_CARRIER_EFF_EN_CH0","RMT","CH%s_TX_CONF0", "CARRIER_EFF_EN","left set, as ESP-IDF leaves it; inert with the carrier off"),
    ("REFLEX_RMT_CARRIER_OUT_LV",  "RMT_CARRIER_OUT_LV_CH0","RMT","CH%s_TX_CONF0", "CARRIER_OUT_LV",""),
    ("REFLEX_RMT_LOOP_STOP_EN",    "RMT_LOOP_STOP_EN_CH0",  "RMT","CH%s_TX_LIM",   "LOOP_STOP_EN",  ""),
    ("REFLEX_RMT_CONF_UPDATE",     "RMT_CONF_UPDATE_CH0", "RMT", "CH%s_TX_CONF0",  "CONF_UPDATE",  "latch the channel config"),
    ("REFLEX_RMT_APB_FIFO_MASK",   "RMT_APB_FIFO_MASK",   "RMT", "SYS_CONF",       "APB_FIFO_MASK","1 addresses channel memory directly"),
    ("REFLEX_RMT_SCLK_ACTIVE",     "RMT_RMT_SCLK_ACTIVE",     "RMT", "SYS_CONF",       "SCLK_ACTIVE",  ""),
    ("REFLEX_RMT_CH0_TX_END_INT_RAW","RMT_CH0_TX_END_INT_RAW","RMT","INT_RAW",     "CH%s_TX_END",  "transmission complete"),
    ("REFLEX_PCR_RMT_CONF_REG",      "PCR_RMT_CONF_REG",      "PCR", "RMT_CONF",      None, "peripheral clock and reset"),
    ("REFLEX_PCR_RMT_SCLK_CONF_REG", "PCR_RMT_SCLK_CONF_REG", "PCR", "RMT_SCLK_CONF", None, ""),
    ("REFLEX_PCR_RMT_CLK_EN",        "PCR_RMT_CLK_EN",        "PCR", "RMT_CONF",      "RMT_CLK_EN",  ""),
    ("REFLEX_PCR_RMT_RST_EN",        "PCR_RMT_RST_EN",        "PCR", "RMT_CONF",      "RMT_RST_EN",  "asserted means held in reset"),
    ("REFLEX_PCR_RMT_SCLK_EN",       "PCR_RMT_SCLK_EN",       "PCR", "RMT_SCLK_CONF", "SCLK_EN",     ""),
]

# Value masks: (1 << bitWidth) - 1 rather than a single bit.
WIDTH_MASKS = [
    ("REFLEX_SPI_MEM_MMU_PAGE_SIZE", "SPI_MEM_MMU_PAGE_SIZE", "SPI0", "SPI_MEM_MMU_POWER_CTRL", "SPI_MMU_PAGE_SIZE", "field value mask"),
    ("REFLEX_LP_WDT_STG_MASK",       "LP_WDT_WDT_STG0_V",     "LP_WDT", "WDTCONFIG0", "WDT_STG0",  "3 bits: 0 off, 3 reset system, 4 reset RTC too"),
    ("REFLEX_LP_WDT_RESET_LEN_MASK", "LP_WDT_WDT_SYS_RESET_LENGTH_V", "LP_WDT", "WDTCONFIG0", "WDT_SYS_RESET_LENGTH", "same width as the CPU field"),
    ("REFLEX_LEDC_TIMER_SEL_MASK",   "LEDC_TIMER_SEL_CH0_V",    "LEDC", "CH%s_CONF0",   "TIMER_SEL",   ""),
    ("REFLEX_LEDC_DUTY_MASK",        "LEDC_DUTY_CH0_V",         "LEDC", "CH%s_DUTY",    "DUTY",        ""),
    ("REFLEX_LEDC_DUTY_RES_MASK",    "LEDC_TIMER0_DUTY_RES_V",  "LEDC", "TIMER%s_CONF", "DUTY_RES",    ""),
    ("REFLEX_LEDC_CLK_DIV_MASK",     "LEDC_CLK_DIV_TIMER0_V",   "LEDC", "TIMER%s_CONF", "CLK_DIV",     "Q10.8 divider"),
    # The two fields of COEX_PTI, so the value Reflex writes is built from
    # bridge-verified fields instead of the magic 0x83 the measurement produced.
    ("REFLEX_154_COEX_PTI_MASK",     "IEEE802154_COEX_PTI",     "IEEE802154", "COEX_PTI", "COEX_PTI",     "traffic priority, 4 bits"),
    ("REFLEX_154_COEX_ACK_PTI_MASK", "IEEE802154_COEX_ACK_PTI", "IEEE802154", "COEX_PTI", "COEX_ACK_PTI", "hardware-ACK priority, 4 bits"),
    ("REFLEX_PCR_LEDC_SCLK_SEL_MASK","PCR_LEDC_SCLK_SEL_V",     "PCR",  "LEDC_SCLK_CONF", "LEDC_SCLK_SEL", "3 selects XTAL"),
    ("REFLEX_PCNT_MODE_MASK",        "PCNT_CH0_POS_MODE_U0_V",  "PCNT", "U%s_CONF0", "CH0_POS_MODE", "all four mode fields are 2 bits"),
    ("REFLEX_PCNT_LIM_MASK",         "PCNT_CNT_H_LIM_U0_V",     "PCNT", "U%s_CONF2", "CNT_H_LIM",    ""),
    ("REFLEX_PCNT_CNT_MASK",         "PCNT_PULSE_CNT_U0_V",     "PCNT", "U%s_CNT",   "CNT",          "16-bit, read as signed"),
    ("REFLEX_RMT_DIV_CNT_MASK",      "RMT_DIV_CNT_CH0_V",       "RMT",  "CH%s_TX_CONF0", "DIV_CNT",  "channel clock divider"),
    ("REFLEX_RMT_MEM_SIZE_MASK",     "RMT_MEM_SIZE_CH0_V",      "RMT",  "CH%s_TX_CONF0", "MEM_SIZE", "in 48-word blocks"),
    ("REFLEX_RMT_SCLK_DIV_NUM_MASK", "RMT_RMT_SCLK_DIV_NUM_V",      "RMT",  "SYS_CONF",  "SCLK_DIV_NUM", ""),
    ("REFLEX_RMT_SCLK_SEL_MASK",     "RMT_RMT_SCLK_SEL_V",          "RMT",  "SYS_CONF",  "SCLK_SEL",     ""),
    ("REFLEX_RMT_TX_LIM_MASK",       "RMT_TX_LIM_CH0_V",            "RMT",  "CH%s_TX_LIM", "TX_LIM",     ""),
    ("REFLEX_PCR_RMT_SCLK_DIV_A_MASK",  "PCR_RMT_SCLK_DIV_A_V",   "PCR", "RMT_SCLK_CONF", "SCLK_DIV_A",   ""),
    ("REFLEX_PCR_RMT_SCLK_DIV_B_MASK",  "PCR_RMT_SCLK_DIV_B_V",   "PCR", "RMT_SCLK_CONF", "SCLK_DIV_B",   ""),
    ("REFLEX_PCR_RMT_SCLK_DIV_NUM_MASK","PCR_RMT_SCLK_DIV_NUM_V", "PCR", "RMT_SCLK_CONF", "SCLK_DIV_NUM", ""),
    ("REFLEX_PCR_RMT_SCLK_SEL_MASK",    "PCR_RMT_SCLK_SEL_V",     "PCR", "RMT_SCLK_CONF", "SCLK_SEL",     ""),
]

# Field shifts. REFLEX_REG_SET_FIELD takes mask and shift explicitly rather
# than reconstructing `NAME_V` / `NAME_S` by token pasting the way IDF's
# REG_SET_FIELD does — pasted names fail at the preprocessor with an error that
# names neither the field nor the call site.
SHIFTS = [
    ("REFLEX_SPI_MEM_MMU_PAGE_SIZE_S", "SPI_MEM_MMU_PAGE_SIZE_S", "SPI0", "SPI_MEM_MMU_POWER_CTRL", "SPI_MMU_PAGE_SIZE", "field shift"),
    ("REFLEX_LP_WDT_STG0_S",           "LP_WDT_WDT_STG0_S",       "LP_WDT", "WDTCONFIG0", "WDT_STG0", ""),
    ("REFLEX_LP_WDT_SYS_RESET_LEN_S",  "LP_WDT_WDT_SYS_RESET_LENGTH_S", "LP_WDT", "WDTCONFIG0", "WDT_SYS_RESET_LENGTH", ""),
    ("REFLEX_LP_WDT_CPU_RESET_LEN_S",  "LP_WDT_WDT_CPU_RESET_LENGTH_S", "LP_WDT", "WDTCONFIG0", "WDT_CPU_RESET_LENGTH", ""),
    ("REFLEX_LEDC_TIMER_SEL_S",   "LEDC_TIMER_SEL_CH0_S",   "LEDC", "CH%s_CONF0",   "TIMER_SEL",   ""),
    ("REFLEX_LEDC_DUTY_S",        "LEDC_DUTY_CH0_S",        "LEDC", "CH%s_DUTY",    "DUTY",        ""),
    ("REFLEX_154_COEX_PTI_S",     "IEEE802154_COEX_PTI_S",     "IEEE802154", "COEX_PTI", "COEX_PTI",     ""),
    ("REFLEX_154_COEX_ACK_PTI_S", "IEEE802154_COEX_ACK_PTI_S", "IEEE802154", "COEX_PTI", "COEX_ACK_PTI", ""),
    ("REFLEX_LEDC_DUTY_RES_S",    "LEDC_TIMER0_DUTY_RES_S", "LEDC", "TIMER%s_CONF", "DUTY_RES",    ""),
    ("REFLEX_LEDC_CLK_DIV_S",     "LEDC_CLK_DIV_TIMER0_S",  "LEDC", "TIMER%s_CONF", "CLK_DIV",     ""),
    ("REFLEX_PCR_LEDC_SCLK_SEL_S","PCR_LEDC_SCLK_SEL_S",    "PCR",  "LEDC_SCLK_CONF", "LEDC_SCLK_SEL", ""),
    ("REFLEX_PCNT_CH0_POS_MODE_S",   "PCNT_CH0_POS_MODE_U0_S",   "PCNT", "U%s_CONF0", "CH0_POS_MODE",   ""),
    ("REFLEX_PCNT_CH0_NEG_MODE_S",   "PCNT_CH0_NEG_MODE_U0_S",   "PCNT", "U%s_CONF0", "CH0_NEG_MODE",   ""),
    ("REFLEX_PCNT_CH0_HCTRL_MODE_S", "PCNT_CH0_HCTRL_MODE_U0_S", "PCNT", "U%s_CONF0", "CH0_HCTRL_MODE", ""),
    ("REFLEX_PCNT_CH0_LCTRL_MODE_S", "PCNT_CH0_LCTRL_MODE_U0_S", "PCNT", "U%s_CONF0", "CH0_LCTRL_MODE", ""),
    ("REFLEX_PCNT_CNT_H_LIM_S",      "PCNT_CNT_H_LIM_U0_S",      "PCNT", "U%s_CONF2", "CNT_H_LIM",      ""),
    ("REFLEX_PCNT_CNT_L_LIM_S",      "PCNT_CNT_L_LIM_U0_S",      "PCNT", "U%s_CONF2", "CNT_L_LIM",      ""),
    ("REFLEX_RMT_DIV_CNT_S",         "RMT_DIV_CNT_CH0_S",        "RMT",  "CH%s_TX_CONF0", "DIV_CNT",  ""),
    ("REFLEX_RMT_MEM_SIZE_S",        "RMT_MEM_SIZE_CH0_S",       "RMT",  "CH%s_TX_CONF0", "MEM_SIZE", ""),
    ("REFLEX_RMT_SCLK_DIV_NUM_S",    "RMT_RMT_SCLK_DIV_NUM_S",       "RMT",  "SYS_CONF",  "SCLK_DIV_NUM", ""),
    ("REFLEX_RMT_SCLK_SEL_S",        "RMT_RMT_SCLK_SEL_S",           "RMT",  "SYS_CONF",  "SCLK_SEL",     ""),
    ("REFLEX_RMT_TX_LIM_S",          "RMT_TX_LIM_CH0_S",         "RMT",  "CH%s_TX_LIM", "TX_LIM",     ""),
    ("REFLEX_PCR_RMT_SCLK_DIV_A_S",  "PCR_RMT_SCLK_DIV_A_S",     "PCR", "RMT_SCLK_CONF", "SCLK_DIV_A",   ""),
    ("REFLEX_PCR_RMT_SCLK_DIV_B_S",  "PCR_RMT_SCLK_DIV_B_S",     "PCR", "RMT_SCLK_CONF", "SCLK_DIV_B",   ""),
    ("REFLEX_PCR_RMT_SCLK_DIV_NUM_S","PCR_RMT_SCLK_DIV_NUM_S",   "PCR", "RMT_SCLK_CONF", "SCLK_DIV_NUM", ""),
    ("REFLEX_PCR_RMT_SCLK_SEL_S",    "PCR_RMT_SCLK_SEL_S",       "PCR", "RMT_SCLK_CONF", "SCLK_SEL",     ""),
]

# Not in the SVD. Each states where it does come from.
LITERALS = [
    ("REFLEX_SOC_IRAM_LOW",        "SOC_IRAM_LOW",        0x40800000, "CPU memory map, not a peripheral: HP SRAM instruction bus window"),
    ("REFLEX_SOC_IRAM_HIGH",       "SOC_IRAM_HIGH",       0x40880000, "CPU memory map"),
    ("REFLEX_SOC_RTC_IRAM_LOW",    "SOC_RTC_IRAM_LOW",    0x50000000, "CPU memory map: LP SRAM"),
    ("REFLEX_SOC_RTC_IRAM_HIGH",   "SOC_RTC_IRAM_HIGH",   0x50004000, "CPU memory map: LP SRAM"),
    ("REFLEX_TIMG_WDT_WKEY_VALUE", "TIMG_WDT_WKEY_VALUE", 0x50D83AA1, "watchdog write-protect magic; a key, not an address"),
    ("REFLEX_LEDC_LS_SIG_OUT0_IDX","LEDC_LS_SIG_OUT0_IDX", 0,         "GPIO matrix signal index; lives in gpio_sig_map, not the SVD"),
    ("REFLEX_PCNT_SIG_CH0_IN0_IDX", "PCNT_SIG_CH0_IN0_IDX",  101, "GPIO matrix signal index; gpio_sig_map, not the SVD. Unit 0 channel 0 edge input"),
    ("REFLEX_PCNT_CTRL_CH0_IN0_IDX","PCNT_CTRL_CH0_IN0_IDX", 103, "GPIO matrix signal index; the level/control input gating the same channel"),
    ("REFLEX_GPIO_MATRIX_CONST_ZERO_INPUT", "GPIO_MATRIX_CONST_ZERO_INPUT", 0x3C, "gpio_pins.h, not the SVD. Routing this into a signal index is how the matrix disconnects an input"),
    ("REFLEX_TIMG_WDT_WKEY", "TIMG_WDT_WKEY_VALUE", 0x50D83AA1, "hal/mwdt_ll.h, not the SVD. Unlocks a timer-group watchdog"),
    ("REFLEX_LP_WDT_WKEY", "LP_WDT_WKEY_VALUE", 0x50D83AA1, "hal/lpwdt_ll.h, not the SVD. Unlocks the watchdog registers"),
    ("REFLEX_SIG_GPIO_OUT_IDX", "SIG_GPIO_OUT_IDX", 128, "GPIO matrix signal index; gpio_sig_map, not the SVD. Routing this back onto a pin is how a peripheral output is detached, and it was a bare 128 in shell.c"),
    ("REFLEX_RMT_SIG_OUT0_IDX", "RMT_SIG_OUT0_IDX", 71, "GPIO matrix signal index; gpio_sig_map, not the SVD. Guessed as 51 first and the bridge rejected it, which is the entire point of the bridge"),
    ("REFLEX_IO_MUX_MCU_SEL_V",    "MCU_SEL",              0x7,        "IO_MUX function-select field mask (3 bits)"),
    ("REFLEX_SOC_SYSTIMER_FIXED_DIVIDER", "SOC_SYSTIMER_FIXED_DIVIDER", 1, "capability flag from soc_caps.h"),
    ("REFLEX_INTR_SRC_SYSTIMER_TARGET1", "ETS_SYSTIMER_TARGET1_INTR_SOURCE", 58,
     "interrupt-matrix source number; an enum position in soc/interrupts.h, not SVD data"),
    ("REFLEX_INTR_SRC_USB_SERIAL_JTAG", "ETS_USB_SERIAL_JTAG_INTR_SOURCE", 48,
     "interrupt-matrix source number; enum position in soc/interrupts.h. Needed to own console RX: the ESP-IDF driver cannot be left installed alongside direct FIFO reads, and polling cannot replace it because the shell idles 50ms while a 64-byte FIFO fills in 5.5ms at 115200 baud"),
    ("REFLEX_INTR_SRC_ZB_MAC", "ETS_ZB_MAC_SOURCE", 12,
     "interrupt-matrix source number; enum position in soc/interrupts.h. The 802.15.4 MAC. Needed so the hand-off can adopt the radio's line rather than quiescing it: under Reflex's trap vector only the tick and console were live, which left the blob-free radio unable to receive"),
]


def load_svd():
    root = ET.parse(SVD).getroot()
    per = {}
    for p in root.iter("peripheral"):
        base = p.findtext("baseAddress")
        if base is None:
            continue
        regs = {}
        for r in p.iter("register"):
            fields = {}
            for f in r.iter("field"):
                fields[f.findtext("name")] = (
                    int(f.findtext("bitOffset")), int(f.findtext("bitWidth")))
            regs[r.findtext("name")] = (int(r.findtext("addressOffset"), 0), fields)
        per[p.findtext("name")] = (int(base, 0), regs)

    # Follow derivedFrom.
    #
    # An SVD peripheral that is identical to another but for its base address is
    # written as <peripheral derivedFrom="TIMG0"> with no registers of its own.
    # Without this, TIMG1 resolves to a base and nothing else, and asking for
    # TIMG1.WDTCONFIG0 fails as "absent from SVD" — which is true of the XML and
    # false of the silicon.
    for p in root.iter("peripheral"):
        name = p.findtext("name")
        parent = p.get("derivedFrom")
        if not parent or name not in per or per[name][1]:
            continue
        if parent in per:
            per[name] = (per[name][0], per[parent][1])
    return per


def resolve(per, pname, rname, fname, width_mask=False):
    if pname not in per:
        raise KeyError(f"peripheral {pname} absent from SVD")
    base, regs = per[pname]
    if rname is None:
        return base, f"{pname} base"
    if rname not in regs:
        raise KeyError(f"{pname}.{rname} absent from SVD")
    off, fields = regs[rname]
    if fname is None:
        return base + off, f"{pname}.{rname} @ base+0x{off:X}"
    if fname not in fields:
        raise KeyError(f"{pname}.{rname}.{fname} absent from SVD")
    bit, width = fields[fname]
    if width_mask:
        return (1 << width) - 1, f"{pname}.{rname}.{fname} width {width}"
    return 1 << bit, f"{pname}.{rname}.{fname} bit {bit}"


def build():
    per = load_svd()
    out, bridge = [], []
    for name, idf, p, r, f, note in REGS:
        val, prov = resolve(per, p, r, f)
        out.append((name, val, prov, note))
        bridge.append((name, idf))
    for name, idf, p, r, f, note in WIDTH_MASKS:
        val, prov = resolve(per, p, r, f, width_mask=True)
        out.append((name, val, prov, note))
        bridge.append((name, idf))
    for name, idf, p, r, f, note in SHIFTS:
        base, regs = per[p]
        bit, _w = regs[r][1][f]
        out.append((name, bit, f"{p}.{r}.{f} bit {bit}", note))
        bridge.append((name, idf))
    for name, idf, val, note in LITERALS:
        out.append((name, val, "literal", note))
        bridge.append((name, idf))
    return out, bridge


def render_header(entries):
    L = [
        "/* Generated by tools/soc_scraper.py from tools/esp32c6.svd — do not edit.",
        " *",
        " * Reflex's own ESP32-C6 register constants, replacing the ESP-IDF",
        " * soc register headers. These are silicon facts rather than ESP-IDF code:",
        " * every address and bit position below is computed from the vendor SVD that",
        " * already backs the 12,738-node shadow atlas, so nothing here was copied by",
        " * hand from a vendor header where a typo would write to a real register.",
        " *",
        " * Equivalence with the ESP-IDF macros these replace is proved, not asserted:",
        " * `make soc-bridge` compiles a translation unit of _Static_asserts inside a",
        " * real ESP-IDF build. Regenerate with `make soc-header`.",
        " */",
        "#ifndef REFLEX_SOC_ESP32C6_H",
        "#define REFLEX_SOC_ESP32C6_H",
        "",
        "#include <stdint.h>",
        "",
    ]
    width = max(len(n) for n, _, _, _ in entries)
    for name, val, prov, note in entries:
        comment = prov if not note else f"{prov} — {note}"
        L.append(f"/* {comment} */")
        L.append(f"#define {name:<{width}} 0x{val:08X}u")
    L += ["", "#endif /* REFLEX_SOC_ESP32C6_H */", ""]
    return "\n".join(L)


def render_bridge(bridge):
    L = [
        "/* Generated by tools/soc_scraper.py --emit-bridge — do not edit.",
        " *",
        " * Proof, not paperwork. Every constant in reflex_soc_esp32c6.h is derived",
        " * from the vendor SVD; this compiles those values against the ESP-IDF macros",
        " * they replace and fails the build on any divergence. It exists so the",
        " * cutover away from the ESP-IDF soc headers is verified, not believed.",
        " *",
        " * Built by `make soc-bridge`, which compiles it inside the ESP-IDF container.",
        " * It is deliberately not part of the firmware: it is a test that happens to",
        " * be a translation unit, and it is the only file left that includes both.",
        " */",
        "#include \"reflex_soc_esp32c6.h\"",
        "",
        "#include \"soc/soc.h\"",
        "#include \"soc/soc_caps.h\"",
        "#include \"soc/reg_base.h\"",
        "#include \"soc/systimer_reg.h\"",
        "#include \"soc/usb_serial_jtag_reg.h\"",
        "#include \"soc/gpio_sig_map.h\"",
        "#include \"soc/io_mux_reg.h\"",
        "#include \"soc/lp_wdt_reg.h\"",
        "#include \"hal/lpwdt_ll.h\"",
        "#include \"soc/lp_aon_reg.h\"",
        "#include \"soc/lp_analog_peri_reg.h\"",
        "#include \"soc/pmu_reg.h\"",
        "#include \"soc/pcr_reg.h\"",
        "#include \"soc/ledc_reg.h\"",
        "#include \"soc/pcnt_reg.h\"",
        "#include \"soc/gpio_pins.h\"",
        "#include \"soc/rmt_reg.h\"",
        "#include \"soc/extmem_reg.h\"",
        "#include \"soc/assist_debug_reg.h\"",
        "#include \"soc/spi_mem_reg.h\"",
        "#include \"soc/timer_group_reg.h\"",
        "#include \"soc/wdt_periph.h\"",
        "#include \"soc/interrupts.h\"",
        # Not in soc/include like the others: this peripheral's macros live
        # under soc/esp32c6/register/. ESP-IDF's own driver reaches these
        # registers through a struct rather than through the macros, but the
        # macros are what the struct is generated from and are the right thing
        # to assert an address against.
        "#include \"soc/ieee802154_reg.h\"",
        "",
    ]
    for name, idf in bridge:
        L.append(f'_Static_assert({name} == (uint32_t)({idf}),')
        L.append(f'               "{name} diverges from {idf}");')
    L += ["", "/* Silence the unused-translation-unit warning. */",
          "const int reflex_soc_bridge_ok = 1;", ""]
    return "\n".join(L)


def clang_format(path):
    """Format generated output in place, so it satisfies the same style gate as
    hand-written code. Generated files still have to pass `make format-diff`,
    and reformatting them by hand would simply be undone by the next run — the
    generator has to emit what the style asks for."""
    exe = shutil.which("clang-format")
    if not exe:
        print(f"  (clang-format not found; {path.name} left unformatted)")
        return
    subprocess.run([exe, "-i", str(path)], check=True)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--emit-bridge", action="store_true")
    ap.add_argument("--check", action="store_true")
    a = ap.parse_args()

    entries, bridge = build()
    header = render_header(entries)

    if a.check:
        # Compare against the formatted rendering: the committed header has been
        # through clang-format, so a raw string comparison would report every
        # up-to-date header as stale.
        exe = shutil.which("clang-format")
        want = header
        if exe:
            want = subprocess.run([exe, "--assume-filename=" + HEADER.name],
                                  input=header, capture_output=True,
                                  text=True, check=True).stdout
        cur = HEADER.read_text() if HEADER.exists() else ""
        if cur != want:
            print("reflex_soc_esp32c6.h is out of date — run 'make soc-header'")
            return 1
        print(f"SoC header up to date ({len(entries)} constants).")
        return 0

    HEADER.write_text(header)
    clang_format(HEADER)
    print(f"wrote {HEADER.relative_to(ROOT)} ({len(entries)} constants)")
    if a.emit_bridge:
        BRIDGE.write_text(render_bridge(bridge))
        clang_format(BRIDGE)
        print(f"wrote {BRIDGE.relative_to(ROOT)} ({len(bridge)} assertions)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
