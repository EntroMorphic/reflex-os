#!/usr/bin/env python3
"""
GOONIES MMIO Exhaustive Test — validates register reads across the full
9,527-entry shadow atlas.

Modes:
  --sample   Test 1-2 entries per peripheral (default, ~120 tests, ~2 min)
  --full     Test every entry in the shadow atlas (9,527 tests, ~80 min)
  --zone X   Test all entries in a specific zone (agency, perception, sys, comm, logic, radio)

For each entry, the script sends `goonies read <name>` and classifies:
  READABLE  — got addr + raw hex + ternary
  BLOCKED   — Sanctuary Guard rejected (expected for protected peripherals)
  ERROR     — unexpected failure
  TIMEOUT   — board didn't respond

Usage:
  python tools/test_goonies_mmio.py /dev/cu.usbmodem1101
  python tools/test_goonies_mmio.py /dev/cu.usbmodem1101 --full
  python tools/test_goonies_mmio.py /dev/cu.usbmodem1101 --zone agency
"""

import sys
import os
import re
import time
import argparse
import serial
from collections import defaultdict

# Sanctuary Guard whitelist (from goose_runtime.c is_sanctuary_address)
SANCTUARY_WHITELIST = {
    0x60091000,  # GPIO
    0x60007000,  # LEDC
    0x60006000,  # RMT
    0x60000000,  # UART0
    0x60001000,  # UART1
    0x60004000,  # I2C
    0x60003000,  # SPI2
    0x6000C000,  # PCNT
    0x6000E000,  # ADC (APB_SARADC)
}


def is_sanctuary_allowed(addr):
    """Returns True if address is in the Sanctuary Guard whitelist."""
    base = addr & 0xFFFFF000
    return base in SANCTUARY_WHITELIST


def parse_shadow_atlas(repo_root):
    """Extract all entries from goose_shadow_atlas.c."""
    atlas_path = os.path.join(repo_root, "components/goose/goose_shadow_atlas.c")
    entries = []
    pattern = re.compile(
        r'\{"([^"]+)",\s*(0x[0-9a-fA-F]+),\s*(0x[0-9a-fA-F]+),\s*'
        r'(-?\d+),\s*(-?\d+),\s*(-?\d+),\s*(\w+)\}'
    )
    with open(atlas_path, "r") as f:
        for line in f:
            m = pattern.search(line)
            if m:
                name = m.group(1)
                addr = int(m.group(2), 16)
                mask = int(m.group(3), 16)
                cell_type = m.group(7)
                zone = name.split(".")[0]
                peripheral = ".".join(name.split(".")[:2])
                entries.append({
                    "name": name,
                    "addr": addr,
                    "mask": mask,
                    "type": cell_type,
                    "zone": zone,
                    "peripheral": peripheral,
                    "allowed": is_sanctuary_allowed(addr),
                })
    return entries


def sample_entries(entries):
    """Pick 1-2 representative entries per peripheral."""
    by_peripheral = defaultdict(list)
    for e in entries:
        by_peripheral[e["peripheral"]].append(e)

    sampled = []
    for periph, elist in sorted(by_peripheral.items()):
        # Pick first register-level entry (whole register, not bit field)
        regs = [e for e in elist if e["mask"] == 0xFFFFFFFF]
        fields = [e for e in elist if e["mask"] != 0xFFFFFFFF]
        if regs:
            sampled.append(regs[0])
        if fields:
            sampled.append(fields[0])
        elif len(regs) > 1:
            sampled.append(regs[1])
    return sampled


def cmd(ser, command, timeout=3.0):
    """Send a shell command and return the response."""
    ser.read(ser.in_waiting)
    ser.write((command + "\n").encode())
    buf = b""
    deadline = time.time() + timeout
    while time.time() < deadline:
        chunk = ser.read(ser.in_waiting or 1)
        if chunk:
            buf += chunk
            if b"reflex> " in buf:
                break
        else:
            time.sleep(0.05)
    return buf.decode("utf-8", errors="replace")


def test_entry(ser, entry):
    """Test a single atlas entry. Returns (status, detail)."""
    out = cmd(ser, f"goonies read {entry['name']}", timeout=5.0)

    if "addr=0x" in out and "raw=0x" in out:
        m = re.search(r"raw=0x([0-9a-fA-F]+)", out)
        raw = int(m.group(1), 16) if m else None
        m2 = re.search(r"masked=0x([0-9a-fA-F]+)", out)
        masked = int(m2.group(1), 16) if m2 else None
        m3 = re.search(r"ternary=(-?\d+)", out)
        ternary = int(m3.group(1)) if m3 else None
        return "READABLE", f"raw=0x{raw:08x} masked=0x{masked:08x} ternary={ternary}" if raw is not None else out.strip()

    if "cannot read" in out.lower():
        if entry["allowed"]:
            return "ERROR", f"whitelisted but unreadable: {out.strip()}"
        else:
            return "BLOCKED", "sanctuary guard (expected)"

    if "reflex>" not in out:
        return "TIMEOUT", out.strip()[:80]

    return "ERROR", out.strip()[:80]


def main():
    parser = argparse.ArgumentParser(description="GOONIES MMIO exhaustive test")
    parser.add_argument("port", help="Serial port")
    parser.add_argument("--baud", type=int, default=115200)
    parser.add_argument("--full", action="store_true", help="Test all 9,527 entries")
    parser.add_argument("--zone", help="Test all entries in a specific zone")
    args = parser.parse_args()

    repo_root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    entries = parse_shadow_atlas(repo_root)
    print(f"Parsed {len(entries)} shadow atlas entries")

    if args.zone:
        entries = [e for e in entries if e["zone"] == args.zone]
        print(f"Filtered to {len(entries)} entries in zone '{args.zone}'")
    elif not args.full:
        entries = sample_entries(entries)
        print(f"Sampled to {len(entries)} representative entries")

    # Group for reporting
    by_zone = defaultdict(list)
    for e in entries:
        by_zone[e["zone"]].append(e)
    for zone in sorted(by_zone):
        peripherals = len(set(e["peripheral"] for e in by_zone[zone]))
        allowed = sum(1 for e in by_zone[zone] if e["allowed"])
        blocked = len(by_zone[zone]) - allowed
        print(f"  {zone}: {len(by_zone[zone])} entries ({peripherals} peripherals, {allowed} whitelisted, {blocked} sanctuary-blocked)")

    # Connect
    ser = serial.Serial(args.port, args.baud, timeout=2)
    print(f"\nConnecting to {args.port}...")
    deadline = time.time() + 20
    while time.time() < deadline:
        chunk = ser.read(ser.in_waiting or 1)
        if chunk and b"reflex> " in chunk:
            break
        time.sleep(0.1)
    time.sleep(1)
    ser.read(ser.in_waiting)
    ser.write(b"\n")
    time.sleep(0.3)
    ser.read(ser.in_waiting)

    # Verify board is alive
    out = cmd(ser, "status")
    if "reflex-os" not in out:
        print("ERROR: board not responding")
        ser.close()
        sys.exit(1)
    print("Board connected.\n")

    # Run tests
    results = {"READABLE": [], "BLOCKED": [], "ERROR": [], "TIMEOUT": []}
    total = len(entries)
    start_time = time.time()

    for idx, entry in enumerate(entries):
        status, detail = test_entry(ser, entry)
        results[status].append((entry, detail))

        # Progress
        if (idx + 1) % 10 == 0 or idx == total - 1:
            elapsed = time.time() - start_time
            rate = (idx + 1) / elapsed if elapsed > 0 else 0
            eta = (total - idx - 1) / rate if rate > 0 else 0
            print(f"\r  [{idx+1}/{total}] R={len(results['READABLE'])} "
                  f"B={len(results['BLOCKED'])} E={len(results['ERROR'])} "
                  f"T={len(results['TIMEOUT'])} "
                  f"({rate:.1f}/s, ETA {eta:.0f}s)  ", end="", flush=True)

    elapsed = time.time() - start_time
    ser.close()

    # Report
    print(f"\n\n{'=' * 70}")
    print(f"GOONIES MMIO Test Report")
    print(f"{'=' * 70}")
    print(f"  Entries tested:  {total}")
    print(f"  Time:            {elapsed:.1f}s ({total/elapsed:.1f} tests/sec)")
    print(f"  READABLE:        {len(results['READABLE'])}")
    print(f"  BLOCKED:         {len(results['BLOCKED'])} (Sanctuary Guard, expected)")
    print(f"  ERROR:           {len(results['ERROR'])}")
    print(f"  TIMEOUT:         {len(results['TIMEOUT'])}")

    # Readable by zone
    readable_zones = defaultdict(int)
    for entry, _ in results["READABLE"]:
        readable_zones[entry["zone"]] += 1
    print(f"\n  Readable by zone:")
    for zone in sorted(readable_zones):
        print(f"    {zone}: {readable_zones[zone]}")

    # Blocked by zone
    blocked_zones = defaultdict(int)
    for entry, _ in results["BLOCKED"]:
        blocked_zones[entry["zone"]] += 1
    if blocked_zones:
        print(f"\n  Blocked by zone (expected):")
        for zone in sorted(blocked_zones):
            print(f"    {zone}: {blocked_zones[zone]}")

    # Errors
    if results["ERROR"]:
        print(f"\n  ERRORS ({len(results['ERROR'])}):")
        for entry, detail in results["ERROR"][:20]:
            print(f"    {entry['name']}: {detail}")
        if len(results["ERROR"]) > 20:
            print(f"    ... ({len(results['ERROR']) - 20} more)")

    # Timeouts
    if results["TIMEOUT"]:
        print(f"\n  TIMEOUTS ({len(results['TIMEOUT'])}):")
        for entry, detail in results["TIMEOUT"][:10]:
            print(f"    {entry['name']}: {detail}")

    # Verify expectations
    print(f"\n{'=' * 70}")
    errors = len(results["ERROR"]) + len(results["TIMEOUT"])
    if errors == 0:
        print(f"  PASS: {len(results['READABLE'])} readable, "
              f"{len(results['BLOCKED'])} sanctuary-blocked, 0 errors")
    else:
        print(f"  FAIL: {errors} unexpected errors/timeouts")
    print(f"{'=' * 70}")

    sys.exit(0 if errors == 0 else 1)


if __name__ == "__main__":
    main()
