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

print()
if FAILURES:
    print(f"{len(FAILURES)} failed")
    sys.exit(1)
print("All tests passed.")
