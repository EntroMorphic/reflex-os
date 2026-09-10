#!/usr/bin/env python3
"""Tests for the independence checker's preprocessor awareness.

The checker is the declared single source of truth for independence status, so
a parser inside it that nothing exercises is a measurement nobody has checked.
These cover the case that was actually wrong — an include fenced off from the
independence path being counted anyway — and the conservative behaviour that
keeps an unrecognised condition from hiding a real dependency.
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import check_independence as ci  # noqa: E402

FAILURES = []


def check(desc, cond, got=""):
    if cond:
        print(f"  PASS  {desc}")
    else:
        print(f"  FAIL  {desc}\n          got: {got!r}")
        FAILURES.append(desc)


def active(src):
    return [t.strip() for _n, t in ci._active_lines(src.splitlines(keepends=True))]


print("--- preprocessor awareness ---")

# The bug this was written for: shell.c fences the UART console away from the
# C6, and the checker counted it regardless.
src = """#if SOC_USB_SERIAL_JTAG_SUPPORTED
#include "a.h"
#else
#include "driver/uart.h"
#endif
"""
got = active(src)
check("the taken branch is kept", '#include "a.h"' in got, got)
check("the untaken branch is dropped", '#include "driver/uart.h"' not in got, got)

# Same guard, written the other way round.
src = """#if !SOC_USB_SERIAL_JTAG_SUPPORTED
#include "driver/uart.h"
#else
#include "b.h"
#endif
"""
got = active(src)
check("a negated condition inverts", '#include "driver/uart.h"' not in got, got)
check("its else branch is kept", '#include "b.h"' in got, got)

# Conservatism: a condition the tool does not understand must not be used to
# excuse a dependency. Both branches count.
src = """#if SOMETHING_UNKNOWN
#include "c.h"
#else
#include "d.h"
#endif
"""
got = active(src)
check("an unknown condition counts both branches",
      '#include "c.h"' in got and '#include "d.h"' in got, got)

# Nesting must not leak: an inactive outer branch hides everything inside it.
src = """#if !SOC_USB_SERIAL_JTAG_SUPPORTED
#if SOMETHING_UNKNOWN
#include "e.h"
#endif
#endif
#include "f.h"
"""
got = active(src)
check("an inactive outer branch hides its inside", '#include "e.h"' not in got, got)
check("and closes correctly afterwards", '#include "f.h"' in got, got)

# ifdef / ifndef spellings of the same fence.
src = """#ifdef SOC_USB_SERIAL_JTAG_SUPPORTED
#include "g.h"
#endif
#ifndef SOC_USB_SERIAL_JTAG_SUPPORTED
#include "h.h"
#endif
"""
got = active(src)
check("ifdef is evaluated", '#include "g.h"' in got, got)
check("ifndef is evaluated", '#include "h.h"' not in got, got)

# elif: only the first true branch is taken.
src = """#if !SOC_USB_SERIAL_JTAG_SUPPORTED
#include "i.h"
#elif CONFIG_REFLEX_RADIO_802154
#include "j.h"
#else
#include "k.h"
#endif
"""
got = active(src)
check("elif takes over from a false if", '#include "j.h"' in got, got)
check("and its else is then dropped", '#include "k.h"' not in got, got)

# The target fence: a driver kept only for the classic ESP32 is not a
# dependency of the C6 path.
src = """#if !CONFIG_IDF_TARGET_ESP32C6
#include "driver/ledc.h"
#endif
#include "driver/rmt_tx.h"
"""
got = active(src)
check("a driver fenced off from the C6 is not counted",
      '#include "driver/ledc.h"' not in got, got)
check("one still on the path is", '#include "driver/rmt_tx.h"' in got, got)

print("\n--- agreement with the real build ---")
# The measurement must match what the compiler actually did. The C6 build's
# dependency output is the only authority on that.
root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
dep = None
for dirpath, _dirs, files in os.walk(os.path.join(root, "build")):
    if "shell.c.obj.d" in files:
        dep = os.path.join(dirpath, "shell.c.obj.d")
        break
if dep is None:
    print("  SKIP  no C6 build present to compare against (run `idf.py build`)")
else:
    text = open(dep, errors="replace").read()
    on, _off, _unknown = ci.scan()
    claimed = {inc for _r, _n, inc, tier, _w in on if tier == "E"}
    for inc in claimed:
        check(f"{inc} is really in the build", inc in text, dep)
    for gone in ("driver/uart.h", "driver/uart_vfs.h"):
        check(f"{gone} is absent from both", gone not in text and gone not in claimed,
              "present in build" if gone in text else "still claimed")

print("\n--- build-defined symbols come from the build, not from a table ---")
# The value of REFLEX_OWN_ENTRY decides whether two Tier C dependencies are
# counted. Getting it from a hand-written table would be an assertion; these
# check that it is read back from the build the tool was pointed at, and that
# an absent build stays conservative rather than assuming "off".
import json as _json
import tempfile as _tempfile

check("REFLEX_OWN_ENTRY is not hardcoded in PATH_SYMBOLS",
      "REFLEX_OWN_ENTRY" not in ci.PATH_SYMBOLS,
      sorted(ci.PATH_SYMBOLS))

with _tempfile.TemporaryDirectory() as td:
    # A build that defines it.
    with open(os.path.join(td, "compile_commands.json"), "w") as fh:
        _json.dump([{"command": "cc -DREFLEX_OWN_ENTRY=1 -DOTHER -c x.c", "file": "x.c"}], fh)
    ci.set_build_dir(td)
    got = ci.apply_build_defines()
    check("defined in the build reads as defined", got.get("REFLEX_OWN_ENTRY") is True, got)
    check("and reaches PATH_SYMBOLS", ci.PATH_SYMBOLS.get("REFLEX_OWN_ENTRY") is True, got)
    check("#ifndef on it is inactive there",
          ci._eval_cond("ifndef", "REFLEX_OWN_ENTRY") is False, got)
    # An unrelated -D must not become a path symbol of its own.
    check("a stray -D does not become a path symbol",
          "OTHER" not in ci.PATH_SYMBOLS, sorted(ci.PATH_SYMBOLS))

with _tempfile.TemporaryDirectory() as td:
    # A build that does not define it.
    with open(os.path.join(td, "compile_commands.json"), "w") as fh:
        _json.dump([{"command": "cc -c x.c", "file": "x.c"}], fh)
    ci.set_build_dir(td)
    got = ci.apply_build_defines()
    check("absent from the build reads as not defined",
          got.get("REFLEX_OWN_ENTRY") is False, got)
    check("#ifndef on it is active there",
          ci._eval_cond("ifndef", "REFLEX_OWN_ENTRY") is True, got)

with _tempfile.TemporaryDirectory() as td:
    # No compile_commands.json at all: the conservative answer is "unknown",
    # which counts both branches. Assuming "off" here would silently count a
    # fenced dependency that the build may not have.
    ci.PATH_SYMBOLS.pop("REFLEX_OWN_ENTRY", None)
    ci.set_build_dir(td)
    got = ci.apply_build_defines()
    check("no compile_commands.json resolves nothing", got == {}, got)
    check("and leaves the symbol unknown",
          ci._eval_cond("ifndef", "REFLEX_OWN_ENTRY") is None,
          ci.PATH_SYMBOLS.get("REFLEX_OWN_ENTRY"))

print("\n--- the own-entry build really has dropped intr_handler_set ---")
# The claim that Tier C fell from 4 to 2 rests on the compiler, not the scan.
# If the build is present, ask the object file directly.
oe = os.path.join(root, "build_own_entry")
obj = None
for dirpath, _dirs, files in os.walk(oe):
    if "reflex_hal_esp32c6.c.obj" in files:
        obj = os.path.join(dirpath, "reflex_hal_esp32c6.c.obj")
        break
if obj is None:
    print("  SKIP  no own-entry build present (run `make own-entry-build`)")
else:
    blob = open(obj, "rb").read()
    check("intr_handler_set is not referenced by the own-entry HAL object",
          b"intr_handler_set" not in blob, obj)
    check("intr_handler_get still is, and is still counted",
          b"intr_handler_get" in blob, obj)

print()
if FAILURES:
    print(f"{len(FAILURES)} failed")
    sys.exit(1)
print("All tests passed.")
