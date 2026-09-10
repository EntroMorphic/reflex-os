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
    # Reflex's own key-value store talks to flash through esp_flash_* rather
    # than the ROM. It used raw esp_rom_spiflash_* precisely to avoid this
    # dependency, and that is why it persisted nothing: raw ROM flash access
    # with the cache enabled never reaches the medium. Tier F alongside nvs,
    # because it is the same concern — storage — reached a different way, and
    # because a store that silently loses everything is worth less than the
    # count it was protecting. See docs/independence-dependency-map.md.
    ("esp_flash",        "F", "Storage, partitions"),
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
    compiled = compiled_sources()
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
                # A source the independence configuration does not compile is
                # off the path by definition. Reported, not counted — dropping
                # it outright would make "off-path" a hiding place, which is
                # the thing this tool was written to prevent.
                not_built = (compiled is not None and f.endswith((".c", ".S"))
                             and os.path.realpath(full) not in compiled)
                try:
                    with open(full, errors="replace") as fh:
                        lines = fh.readlines()
                except OSError:
                    continue
                for n, line in _active_lines(lines):
                    m = INCLUDE_RE.match(line)
                    if not m:
                        continue
                    inc = m.group(1)
                    tier, what = classify(inc)
                    rec = (rel, n, inc, tier, what)
                    if tier is None:
                        unknown.append(rec)
                    elif not_built or rel.startswith(OFF_PATH):
                        off.append(rec)
                    else:
                        on.append(rec)
    return on, off, unknown


# Symbols whose value is fixed on the independence path (ESP32-C6 with the
# 802.15.4 radio). Anything else is treated as unknown and both of its branches
# are counted, which keeps the measurement conservative.
PATH_SYMBOLS = {
    "SOC_USB_SERIAL_JTAG_SUPPORTED": True,
    "CONFIG_REFLEX_RADIO_802154": True,
    # The independence path is the ESP32-C6. Code fenced off from it — an
    # ESP-IDF peripheral driver kept only for the classic ESP32, say — is not a
    # dependency this path has, and the build's own header list agrees.
    "CONFIG_IDF_TARGET_ESP32C6": True,
}

COND_RE = re.compile(r"^\s*#\s*(if|ifdef|ifndef|elif|else|endif)\b\s*(.*)$")


def _eval_cond(kind, expr):
    """True / False for a condition this tool understands, None otherwise.

    Deliberately tiny: it recognises a bare symbol and a negated one, which is
    the whole vocabulary the on-path sources use to fence off code that belongs
    to the other target. Anything more elaborate returns None and is counted.
    """
    expr = expr.split("//")[0].split("/*")[0].strip()
    if kind == "ifdef":
        return PATH_SYMBOLS.get(expr)
    if kind == "ifndef":
        v = PATH_SYMBOLS.get(expr)
        return None if v is None else not v
    negated = expr.startswith("!")
    if negated:
        expr = expr[1:].strip()
    if not re.fullmatch(r"[A-Za-z_][A-Za-z0-9_]*", expr):
        return None
    v = PATH_SYMBOLS.get(expr)
    if v is None:
        return None
    return (not v) if negated else v


def _active_lines(lines):
    """Yield (lineno, text) for lines the independence build actually compiles.

    Counting an include the build never sees is not a harmless overestimate: it
    is the declared measure of independence reporting a dependency that does not
    exist. `shell/shell.c` fences driver/uart.h and driver/uart_vfs.h behind
    `#if SOC_USB_SERIAL_JTAG_SUPPORTED ... #else`, so the C6 build has never
    included either — confirmed against the build's own dependency output —
    while this tool counted both and put Tier E two higher than it was.
    """
    stack = []  # (active_here, condition_was_known, any_branch_taken)
    for n, line in enumerate(lines, 1):
        m = COND_RE.match(line)
        if m:
            kind, expr = m.group(1), m.group(2)
            if kind in ("if", "ifdef", "ifndef"):
                v = _eval_cond(kind, expr)
                stack.append([True if v is None else v, v is not None, v is True])
            elif kind == "elif" and stack:
                top = stack[-1]
                v = _eval_cond("if", expr)
                if not top[1] or v is None:
                    top[0], top[1] = True, False
                else:
                    top[0] = v and not top[2]
                    top[2] = top[2] or top[0]
            elif kind == "else" and stack:
                top = stack[-1]
                top[0] = True if not top[1] else not top[2]
            elif kind == "endif" and stack:
                stack.pop()
            continue
        if all(fr[0] for fr in stack):
            yield n, line


# The set of sources the independence build actually compiles.
#
# Scanning every file under the source tree counts includes in files the
# independence configuration never builds. CMake selects between backends —
# reflex_kv_esp32c6.c only when the radio is *not* 802.15.4,
# reflex_task_esp32c6.c only when the Reflex scheduler is *not* selected — so
# five on-path includes were being carried by two files that configuration does
# not compile at all. The preprocessor-aware scan above fixed the same error one
# level down, inside a file; this is the same error one level up, between files.
#
# The build's own dependency output is the authority on what was compiled. When
# it is absent the scan falls back to counting everything and says so, because
# an overcount that announces itself is safer than a number that quietly
# depends on whether someone happened to build first.
INDEPENDENCE_BUILD = os.path.join(ROOT, "build_independence")

# Which build directory defines "compiled", overridable with --build.
#
# This was pinned to build_independence, and that quietly became the wrong
# question. The independence build still delegates its scheduling to FreeRTOS —
# it does not set CONFIG_REFLEX_TASK_BACKEND_REFLEX, so it compiles
# reflex_task_kernel.c and carries that file's three freertos includes. The
# configuration that actually achieves independence is the own-entry build,
# which compiles reflex_task_reflex.c instead and starts no FreeRTOS at all.
# Reporting the first build's number as the project's independence measures a
# configuration that is not the one making the claim.
_build_dir = INDEPENDENCE_BUILD


def set_build_dir(path):
    global _build_dir
    _build_dir = os.path.abspath(path)


def compiled_sources():
    """Absolute paths of sources in the independence build, or None."""
    if not os.path.isdir(_build_dir):
        return None
    srcs = set()
    for dirpath, _dirs, files in os.walk(_build_dir):
        for f in files:
            if not f.endswith(".obj.d"):
                continue
            try:
                with open(os.path.join(dirpath, f), errors="replace") as fh:
                    head = fh.read(4096)
            except OSError:
                continue
            # "path/to/x.c.obj: /abs/path/x.c /abs/other.h \"
            after = head.split(":", 1)[-1]
            for tok in after.replace("\\", " ").split():
                if tok.endswith((".c", ".S")):
                    srcs.add(os.path.realpath(tok))
                    break
    return srcs or None


def counts(records):
    c = {}
    for _rel, _n, _inc, tier, _what in records:
        c[tier] = c.get(tier, 0) + 1
    return c


def main():
    check = "--check" in sys.argv
    update = "--update" in sys.argv
    verbose = "-v" in sys.argv or "--verbose" in sys.argv
    # --build <dir>: measure a different configuration's compiled set. The
    # default remains build_independence so existing invocations are unchanged.
    if "--build" in sys.argv:
        i = sys.argv.index("--build")
        if i + 1 >= len(sys.argv):
            print("--build needs a directory")
            return 2
        set_build_dir(sys.argv[i + 1])
        print(f"measuring configuration: {os.path.relpath(_build_dir, ROOT)}")

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

    # Without the independence build there is nothing to filter against, so the
    # numbers include files that configuration never compiles — an upper bound,
    # not the measurement the baseline records. Ratcheting on it would fail the
    # build for a reason that has nothing to do with the code, so it says so and
    # declines to judge rather than reporting a regression it cannot support.
    if compiled_sources() is None:
        print("\n  No build_independence/ present, so these counts include "
              "sources the independence configuration does not compile.")
        print("  Upper bound only — the ratchet is not applied. Build it with:")
        print("    SDKCONFIG_DEFAULTS=sdkconfig.defaults.independence \\")
        print("      idf.py -B build_independence "
              "-DSDKCONFIG=build_independence/sdkconfig build")
        return 0

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
