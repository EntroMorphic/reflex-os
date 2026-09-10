#!/usr/bin/env python3
"""Measure the vendor binary blobs a build links, by symbol and by byte.

`check_independence.py` counts ESP-IDF *source* dependencies — includes and
local externs — and it cannot see a binary at all. That gap is not theoretical:
disabling coexistence removed 27 of the image's 42 blob symbols and 10,130 bytes
of `libcoexist.a` without changing a single include, so the independence ratchet
reported no movement whatsoever for a real reduction in borrowed vendor code.
This is the measure that sees it.

A blob here is an archive the link pulls in from the ESP-IDF tree itself, rather
than one the build produced. Prebuilt vendor libraries live under
`$IDF_PATH/components/**/lib*/<target>/`; everything the build compiles lands
under the build directory. That is the whole rule, and it needs no list to
maintain — a new blob appearing in a future ESP-IDF is caught automatically and
shows up as an unrecognised increase rather than as silence.

Two numbers per build, both ratcheted:

  symbols  distinct blob-defined symbols that source objects in the image
           reference. The honest measure of coupling: how many places Reflex's
           image reaches into code nobody can read.
  bytes    how much of those archives the linker actually kept. The measure of
           how much unreadable code ships.

Usage:  tools/check_blobs.py [--build DIR] [--check] [--update] [-v]
"""
import json
import os
import re
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
BASELINE = os.path.join(os.path.dirname(os.path.abspath(__file__)), "blob_baseline.json")
DEFAULT_BUILD = os.path.join(ROOT, "build_own_entry")
NM = "riscv32-esp-elf-nm"


def blob_archives(map_path, idf):
    """Prebuilt archives this link pulled in from the ESP-IDF tree."""
    text = open(map_path, errors="replace").read()
    found = {}
    for m in re.finditer(r'(\S*/[\w\-]+\.a)\(', text):
        p = m.group(1)
        if not p.startswith("/"):
            continue
        # Built by this project, not shipped by the vendor.
        if os.path.realpath(ROOT) in os.path.realpath(p):
            continue
        if idf and os.path.realpath(idf) not in os.path.realpath(p):
            continue
        found[os.path.basename(p)] = p
    return found, text


def linked_bytes(map_text, archive_name):
    return sum(int(m.group(1), 16) for m in re.finditer(
        r'^\s+\.\S+\s+0x[0-9a-f]+\s+(0x[0-9a-f]+)\s+\S*' + re.escape(archive_name) + r'\(',
        map_text, re.M))


def defined_symbols(path):
    out = subprocess.run([NM, "-g", "--defined-only", path],
                         capture_output=True, text=True).stdout
    syms = set()
    for line in out.splitlines():
        p = line.split()
        if len(p) == 3 and p[1] in "TtWwDdBbRr":
            syms.add(p[2])
    return syms


def required_symbols(build, defined):
    """Blob symbols that objects in this build leave undefined."""
    need = {}
    for dirpath, _dirs, files in os.walk(build):
        for f in files:
            if not f.endswith(".obj"):
                continue
            out = subprocess.run([NM, "-u", os.path.join(dirpath, f)],
                                 capture_output=True, text=True).stdout
            for line in out.splitlines():
                s = line.split()
                if s and s[-1] in defined:
                    need.setdefault(s[-1], set()).add(f)
    return need


def measure(build, idf):
    map_path = os.path.join(build, "reflex_os.map")
    if not os.path.isfile(map_path):
        return None
    archives, map_text = blob_archives(map_path, idf)
    defined = {}
    for name, path in archives.items():
        for s in defined_symbols(path):
            defined.setdefault(s, name)
    need = required_symbols(build, defined)
    per = {}
    for name in archives:
        per[name] = {
            "bytes": linked_bytes(map_text, name),
            "symbols": sorted(s for s in need if defined[s] == name),
        }
    return per, need, defined


def main():
    idf = os.environ.get("IDF_PATH", "")
    build = DEFAULT_BUILD
    argv = sys.argv
    if "--build" in argv:
        i = argv.index("--build")
        if i + 1 >= len(argv):
            print("--build needs a directory")
            return 2
        build = os.path.abspath(argv[i + 1])
    verbose = "-v" in argv or "--verbose" in argv
    check = "--check" in argv
    update = "--update" in argv

    if not subprocess.run(["which", NM], capture_output=True).returncode == 0:
        print(f"{NM} not on PATH — run inside the ESP-IDF environment "
              f"(. $IDF_PATH/export.sh). Not judging.")
        return 0

    got = measure(build, idf)
    config = os.path.basename(build)
    if got is None:
        print(f"No {config}/reflex_os.map — build it first. Not judging.")
        return 0
    per, need, defined = got

    total_sym = len(need)
    total_bytes = sum(v["bytes"] for v in per.values())
    print(f"Vendor blobs linked by {config}:\n")
    for name in sorted(per):
        v = per[name]
        print(f"  {name:<16} {v['bytes']:>7} bytes   {len(v['symbols']):>3} symbols referenced")
        if verbose:
            for s in v["symbols"]:
                print(f"      {s}")
    print(f"\n  TOTAL: {total_bytes} bytes, {total_sym} symbols")

    cur = {"bytes": total_bytes, "symbols": total_sym}

    if update:
        try:
            doc = json.load(open(BASELINE))
        except (OSError, ValueError):
            doc = {}
        doc.setdefault("configurations", {})[config] = cur
        doc["note"] = ("Vendor blob ratchet. Lower is the only legal direction. "
                       "Regenerate deliberately with tools/check_blobs.py --update.")
        with open(BASELINE, "w") as fh:
            json.dump(doc, fh, indent=2, sort_keys=True)
            fh.write("\n")
        print(f"\nBaseline updated for {config}.")
        return 0

    if not check:
        return 0

    try:
        doc = json.load(open(BASELINE))
        base = doc["configurations"][config]
    except (OSError, KeyError, ValueError):
        print(f"\nFAILED: no blob baseline for '{config}'. Record it with "
              f"--update --build {config}.")
        return 1

    bad = False
    for k in ("bytes", "symbols"):
        if cur[k] > base[k]:
            print(f"\nFAILED: blob {k} grew from {base[k]} to {cur[k]}. "
                  f"Unreadable vendor code moves one way.")
            bad = True
    if bad:
        return 1
    for k in ("bytes", "symbols"):
        if cur[k] < base[k]:
            print(f"\n  blob {k} improved: {base[k]} -> {cur[k]}. Run --update to lower it.")
    print(f"\nBlobs: no growth ({config}).")
    return 0


if __name__ == "__main__":
    sys.exit(main())
