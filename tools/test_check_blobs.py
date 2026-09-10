#!/usr/bin/env python3
"""Tests for check_blobs.py — the vendor-blob ratchet.

Both cases here are defects the tool actually shipped with, found by
red-teaming it one commit after it was written.
"""
import importlib.util
import json
import os
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
spec = importlib.util.spec_from_file_location("cb", os.path.join(HERE, "check_blobs.py"))
cb = importlib.util.module_from_spec(spec)
spec.loader.exec_module(cb)

FAILURES = []


def check(name, cond, got=None):
    if cond:
        print(f"  PASS  {name}")
    else:
        FAILURES.append(name)
        print(f"  FAIL  {name}")
        if got is not None:
            print(f"          got: {got}")


print("--- a sibling directory is not this project ---")
# `os.path.realpath(ROOT) in os.path.realpath(p)` is substring containment, and
# it matched any sibling whose name merely begins with ROOT — so a vendor blob
# under /x/reflex-os-vendor/ was taken for one of ours and silently dropped from
# the count. A blob ratchet that under-counts is worse than none.
with tempfile.TemporaryDirectory() as td:
    root = os.path.join(td, "proj")
    sibling = os.path.join(td, "proj-vendor")
    os.makedirs(root)
    os.makedirs(sibling)
    check("the project directory is inside itself", cb._under(os.path.realpath(root), root))
    check("a file under it is inside it",
          cb._under(os.path.realpath(os.path.join(root, "a", "b.a")), root))
    check("a sibling with a longer name is NOT inside it",
          not cb._under(os.path.realpath(os.path.join(sibling, "lib.a")), root),
          sibling)

print("\n--- only objects that reach the image are counted ---")
# The first version walked every .obj in the build directory. For the own-entry
# build that is 1,016 objects, of which the linker keeps 312; the other 704 are
# compiled and discarded. Counting their undefined symbols inflated the number
# by 1 here, 5 for build_independence and 7 for the default build's phy/btbb/
# coex subset — and those inflated figures were published in the README, the
# Kconfig help, the changelog and the ledger before this test existed.
MAP = """
Linker script and memory map

 .text.foo      0x40800000       0x10 esp-idf/x/libx.a(kept_one.c.obj)
 .text.bar      0x40800010       0x20 /idf/components/esp_phy/lib/esp32c6/libphy.a(phy.o)
 .rodata.baz    0x40800030        0x4 esp-idf/y/liby.a(kept_two.c.obj)
"""
kept = cb.linked_objects(MAP)
check("objects named in the map are kept", {"kept_one.c.obj", "kept_two.c.obj"} <= kept, kept)
check("an archive member is recorded by its member name", "phy.o" in kept, kept)
check("an object absent from the map is not kept", "discarded.c.obj" not in kept, kept)

print("\n--- linked_bytes sums only the named archive ---")
check("libphy bytes", cb.linked_bytes(MAP, "libphy.a") == 0x20,
      cb.linked_bytes(MAP, "libphy.a"))
check("an archive not present sums to zero", cb.linked_bytes(MAP, "libnope.a") == 0)

print("\n--- the ratchet is per configuration and refuses an unknown one ---")
doc_path = os.path.join(HERE, "blob_baseline.json")
if os.path.exists(doc_path):
    doc = json.load(open(doc_path))
    cfgs = doc.get("configurations", {})
    check("a baseline exists for the own-entry build", "build_own_entry" in cfgs, sorted(cfgs))
    for name, cfg in cfgs.items():
        check(f"{name} records both bytes and symbols",
              "bytes" in cfg and "symbols" in cfg, cfg)
    oe = cfgs.get("build_own_entry", {})
    check("the own-entry floor is the measured 802.15.4 figure",
          oe.get("bytes") == 48566 and oe.get("symbols") == 14, oe)
else:
    print("  SKIP  no blob baseline recorded yet")

print()
if FAILURES:
    print(f"{len(FAILURES)} failed")
    sys.exit(1)
print("All tests passed.")
