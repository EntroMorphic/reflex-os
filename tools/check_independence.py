#!/usr/bin/env python3
"""Measure what Reflex OS still borrows from ESP-IDF, by tier, and ratchet it.

The single source of truth for independence status is this measurement, not a
table someone remembers to update. `docs/independence-dependency-map.md` had
drifted three ways by 2026-09-07 — a constant count that had settled at a
different number, a tier whose stated blocker had already been built, and an
"no hardware was attached" note contradicted by a section of the same document.
Prose drifts. A measurement that CI runs does not.

Two guarantees, both enforced by `--check`:

  1. **Ratchet.** No tier may grow. Independence moves one way; a new ESP-IDF
     include in a tier that was going down is a regression and fails the build.
  2. **No unclassified dependencies.** An include this script cannot place in a
     tier is an error, not a warning. That is what stops a genuinely new kind
     of coupling from arriving disguised as noise.

Scope. The independence path is the ESP32-C6 with `CONFIG_REFLEX_RADIO_802154`.
Sources compiled only off that path — the classic-ESP32 backend, the Wi-Fi
stack, the ESP-NOW radio — are measured and reported but not ratcheted, because
they are deliberately borrowed. They are listed so that "off-path" stays a
decision rather than a hiding place.
"""
import json
import os
import re
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
BASELINE = os.path.join(os.path.dirname(os.path.abspath(__file__)), "independence_baseline.json")

# Directories that hold Reflex-owned source. build/ and esp-idf/ are theirs.
SCAN = ["components", "vm", "shell", "core", "kernel", "storage",
        "services", "drivers", "main", "net", "platform"]

INCLUDE_RE = re.compile(r'^\s*#\s*include\s+[<"]((?:esp_|freertos/|driver/|soc/|hal/|nvs|sdkconfig|riscv/|xtensa)[^">]*)[">]')

# Tier assignment, longest prefix wins. Mirrors the table in
# docs/independence-dependency-map.md section 1.
TIERS = [
    ("freertos/",        "C", "FreeRTOS as the scheduler"),
    ("esp_intr_alloc",   "C", "FreeRTOS as the scheduler"),
    ("esp_ieee802154",   "D", "Radio"),
    ("esp_now",          "D", "Radio"),
    ("esp_wifi",         "D", "Radio"),
    ("esp_netif",        "D", "Radio"),
    ("esp_event",        "D", "Radio"),
    ("driver/",          "E", "Console RX and peripheral drivers"),
    ("esp_sleep",        "B", "Deep-sleep entry"),
    ("esp_heap_caps",    "B", "Heap reporting"),
    ("nvs",              "F", "Storage, partitions"),
    ("sdkconfig",        "F", "Build system"),
    ("esp_system",       "F", "Startup, reset"),
    ("esp_timer",        "F", "Startup, timers"),
    ("esp_rom_sys",      "F", "ROM entry points"),
    ("esp_log",          "F", "Logging"),
    ("esp_random",       "F", "Entropy"),
    ("esp_mac",          "F", "Identity"),
    ("soc/",             "A", "SoC register constants"),
    ("hal/",             "A", "SoC register constants"),
]

# Compiled only off the independence path (C6 + 802.15.4). Reported, not
# ratcheted — see the module docstring.
OFF_PATH = (
    "platform/esp32/",                       # classic ESP32 mesh peer
    "net/",                                  # Wi-Fi stack, gated by net/CMakeLists.txt
    "platform/esp32c6/reflex_radio_esp32c6.c",  # ESP-NOW, replaced by 802154
)


def classify(inc):
    for prefix, tier, what in TIERS:
        if inc.startswith(prefix) or prefix in inc:
            return tier, what
    return None, None


def scan():
    on, off, unknown = [], [], []
    for d in SCAN:
        p = os.path.join(ROOT, d)
        if not os.path.isdir(p):
            continue
        for dirpath, _dirs, files in os.walk(p):
            if "build" in dirpath.split(os.sep):
                continue
            for f in files:
                if not f.endswith((".c", ".h", ".S")):
                    continue
                full = os.path.join(dirpath, f)
                rel = os.path.relpath(full, ROOT)
                try:
                    with open(full, errors="replace") as fh:
                        lines = fh.readlines()
                except OSError:
                    continue
                for n, line in enumerate(lines, 1):
                    m = INCLUDE_RE.match(line)
                    if not m:
                        continue
                    inc = m.group(1)
                    tier, what = classify(inc)
                    rec = (rel, n, inc, tier, what)
                    if tier is None:
                        unknown.append(rec)
                    elif rel.startswith(OFF_PATH):
                        off.append(rec)
                    else:
                        on.append(rec)
    return on, off, unknown


def counts(records):
    c = {}
    for _rel, _n, _inc, tier, _what in records:
        c[tier] = c.get(tier, 0) + 1
    return c


def main():
    check = "--check" in sys.argv
    update = "--update" in sys.argv
    verbose = "-v" in sys.argv or "--verbose" in sys.argv

    on, off, unknown = scan()
    cur = counts(on)

    print("ESP-IDF surface still borrowed, by tier "
          "(independence path: ESP32-C6 + CONFIG_REFLEX_RADIO_802154)")
    print()
    labels = {
        "A": "SoC constants and ROM entry points",
        "B": "Deep-sleep entry, heap reporting",
        "C": "FreeRTOS as the scheduler",
        "D": "Radio",
        "E": "Console RX, peripheral drivers",
        "F": "Build system, startup, heap, image",
    }
    for tier in sorted(set(list(cur) + ["A", "B", "C", "D", "E", "F"])):
        n = cur.get(tier, 0)
        mark = "clear" if n == 0 else ""
        print(f"  Tier {tier}  {n:>3}  {labels.get(tier,''):<34} {mark}")
    print(f"\n  ON-PATH TOTAL: {sum(cur.values())}")

    if verbose:
        print("\n  on-path detail:")
        for rel, n, inc, tier, _w in sorted(on):
            print(f"    {tier}  {rel}:{n}  {inc}")

    print(f"\n  off-path (deliberately borrowed, not ratcheted): {len(off)}")
    if verbose:
        for rel, n, inc, tier, _w in sorted(off):
            print(f"    {tier}  {rel}:{n}  {inc}")

    if unknown:
        print("\n  UNCLASSIFIED — a new kind of dependency has appeared:")
        for rel, n, inc, _t, _w in sorted(unknown):
            print(f"    {rel}:{n}  {inc}")

    if update:
        with open(BASELINE, "w") as fh:
            json.dump({"on_path": cur, "note":
                       "Ratchet baseline. Lower is the only legal direction. "
                       "Regenerate deliberately with tools/check_independence.py --update."},
                      fh, indent=2, sort_keys=True)
            fh.write("\n")
        print(f"\nBaseline updated: {os.path.relpath(BASELINE, ROOT)}")
        return 0

    if not check:
        return 0

    if unknown:
        print("\nFAILED: unclassified ESP-IDF dependency. Place it in a tier in "
              "tools/check_independence.py, or remove it.")
        return 1

    try:
        with open(BASELINE) as fh:
            base = json.load(fh)["on_path"]
    except (OSError, KeyError, ValueError):
        print(f"\nFAILED: no baseline. Create it with --update.")
        return 1

    regressed = False
    for tier, n in sorted(cur.items()):
        b = base.get(tier, 0)
        if n > b:
            print(f"\nFAILED: Tier {tier} grew from {b} to {n}. "
                  f"Independence moves one way.")
            regressed = True
    if regressed:
        return 1

    improved = [(t, base.get(t, 0), n) for t, n in sorted(cur.items()) if n < base.get(t, 0)]
    for t, b, n in improved:
        print(f"\n  Tier {t} improved: {b} -> {n}. "
              f"Run --update to lower the ratchet.")
    print("\nIndependence: no tier has regressed.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
