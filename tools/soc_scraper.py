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
]

# Value masks: (1 << bitWidth) - 1 rather than a single bit.
WIDTH_MASKS = [
    ("REFLEX_SPI_MEM_MMU_PAGE_SIZE", "SPI_MEM_MMU_PAGE_SIZE", "SPI0", "SPI_MEM_MMU_POWER_CTRL", "SPI_MMU_PAGE_SIZE", "field value mask"),
]

# Field shifts. REFLEX_REG_SET_FIELD takes mask and shift explicitly rather
# than reconstructing `NAME_V` / `NAME_S` by token pasting the way IDF's
# REG_SET_FIELD does — pasted names fail at the preprocessor with an error that
# names neither the field nor the call site.
SHIFTS = [
    ("REFLEX_SPI_MEM_MMU_PAGE_SIZE_S", "SPI_MEM_MMU_PAGE_SIZE_S", "SPI0", "SPI_MEM_MMU_POWER_CTRL", "SPI_MMU_PAGE_SIZE", "field shift"),
]

# Not in the SVD. Each states where it does come from.
LITERALS = [
    ("REFLEX_SOC_IRAM_LOW",        "SOC_IRAM_LOW",        0x40800000, "CPU memory map, not a peripheral: HP SRAM instruction bus window"),
    ("REFLEX_SOC_IRAM_HIGH",       "SOC_IRAM_HIGH",       0x40880000, "CPU memory map"),
    ("REFLEX_SOC_RTC_IRAM_LOW",    "SOC_RTC_IRAM_LOW",    0x50000000, "CPU memory map: LP SRAM"),
    ("REFLEX_SOC_RTC_IRAM_HIGH",   "SOC_RTC_IRAM_HIGH",   0x50004000, "CPU memory map: LP SRAM"),
    ("REFLEX_TIMG_WDT_WKEY_VALUE", "TIMG_WDT_WKEY_VALUE", 0x50D83AA1, "watchdog write-protect magic; a key, not an address"),
    ("REFLEX_LEDC_LS_SIG_OUT0_IDX","LEDC_LS_SIG_OUT0_IDX", 0,         "GPIO matrix signal index; lives in gpio_sig_map, not the SVD"),
    ("REFLEX_IO_MUX_MCU_SEL_V",    "MCU_SEL",              0x7,        "IO_MUX function-select field mask (3 bits)"),
    ("REFLEX_SOC_SYSTIMER_FIXED_DIVIDER", "SOC_SYSTIMER_FIXED_DIVIDER", 1, "capability flag from soc_caps.h"),
    ("REFLEX_INTR_SRC_SYSTIMER_TARGET1", "ETS_SYSTIMER_TARGET1_INTR_SOURCE", 58,
     "interrupt-matrix source number; an enum position in soc/interrupts.h, not SVD data"),
    ("REFLEX_INTR_SRC_USB_SERIAL_JTAG", "ETS_USB_SERIAL_JTAG_INTR_SOURCE", 48,
     "interrupt-matrix source number; enum position in soc/interrupts.h. Needed to own console RX: the ESP-IDF driver cannot be left installed alongside direct FIFO reads, and polling cannot replace it because the shell idles 50ms while a 64-byte FIFO fills in 5.5ms at 115200 baud"),
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
        "#include \"soc/lp_aon_reg.h\"",
        "#include \"soc/lp_analog_peri_reg.h\"",
        "#include \"soc/pmu_reg.h\"",
        "#include \"soc/pcr_reg.h\"",
        "#include \"soc/extmem_reg.h\"",
        "#include \"soc/assist_debug_reg.h\"",
        "#include \"soc/spi_mem_reg.h\"",
        "#include \"soc/timer_group_reg.h\"",
        "#include \"soc/wdt_periph.h\"",
        "#include \"soc/interrupts.h\"",
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
