#!/usr/bin/env python3
"""Check what ESP-IDF code is actually linked into the image.

The independence checker counts `#include` lines, which is the right unit for
deliberate coupling but blind to two things: a driver whose objects the linker
discards is still counted, and a dependency wired in by ESP-IDF's startup
rather than by an include is not counted at all.

Both showed up the moment Tier E reached zero. The four peripheral drivers it
had owned turned out to contribute no objects to the image either — a stronger
result than the include count could state — while ESP-IDF's USB-serial-JTAG VFS
is still linked and is what every printf in the shell travels through, invisible
to a scan of include lines because nothing includes it.

So this reads the linker map, which is the only account of what is really in the
image. It asserts that the peripherals Reflex has taken over contribute nothing,
and reports what remains of the console as the known residue it is.
"""
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
MAP = os.path.join(ROOT, "build", "reflex_os.map")

# Peripherals Reflex drives itself. No object from these belongs in the image.
OWNED = ["ledc", "pcnt", "rmt", "uart"]

# Still linked, and honest about why. The console's receive half is Reflex's;
# its transmit half still reaches the wire through the VFS ESP-IDF's startup
# registers, which is a Tier F question (startup) rather than a Tier E one.
KNOWN_RESIDUE = {
    "usb_serial_jtag": "stdout still travels through ESP-IDF's console VFS, "
                       "registered by its startup rather than by an include",
}


def members(lib):
    """Archive members the linker actually extracted, not merely offered."""
    with open(MAP, errors="replace") as fh:
        text = fh.read()
    return sorted(set(re.findall(r"libesp_driver_%s\.a\(([^)]+)\)" % re.escape(lib), text)))


def main():
    if not os.path.exists(MAP):
        print(f"no linker map at {MAP} — build first with `idf.py build`")
        return 1

    # Prove the pattern can find something before trusting an empty result.
    # An empty answer from a broken query reads exactly like a clean bill of
    # health, and this check was written twice for that reason.
    if not members("gpio"):
        print("FAIL: the map query found no objects even for esp_driver_gpio, "
              "which is certainly linked — the pattern is wrong, so every "
              "empty result below would be meaningless")
        return 1

    failed = False
    print("ESP-IDF driver objects linked into the image")
    for lib in OWNED:
        objs = members(lib)
        if objs:
            print(f"  FAIL  esp_driver_{lib}: {', '.join(objs)}")
            failed = True
        else:
            print(f"  ok    esp_driver_{lib}: nothing extracted")

    print()
    for lib, why in KNOWN_RESIDUE.items():
        objs = members(lib)
        print(f"  residue  esp_driver_{lib}: {len(objs)} object(s) — {why}")
        for o in objs:
            print(f"           {o}")

    print()
    if failed:
        print("A peripheral Reflex owns is still contributing code to the image.")
        return 1
    print("Image: no ESP-IDF driver Reflex has taken over is linked.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
