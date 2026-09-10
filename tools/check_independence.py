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
    # RF bring-up, and Tier D's bedrock rather than a driver dependency.
    # esp_phy/btbb sequence libphy.a and libbtbb.a for calibration and the
    # analog front end; esp_modem_clock is refcounted clock gating shared with
    # Wi-Fi and Bluetooth. Reflex's own MAC calls these three and owns
    # everything above them. They are one-shot silicon bring-up, not a state
    # machine, which is the distinction that makes the trade worth making.
    ("esp_private/phy",  "D", "Radio"),
    ("esp_phy",          "D", "Radio"),
    ("esp_private/esp_modem_clock", "D", "Radio"),
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


# Dependencies reached by a local `extern` declaration rather than an #include.
#
# The scan below reads include lines, so anything declared inline is invisible
# to it — and declaring inline is a thing this codebase does deliberately, for
# ROM entry points. That is defensible for ROM, which is silicon. It is not
# defensible generally: intr_handler_set and its two siblings are ESP-IDF C in
# components/riscv/interrupt.c, were declared locally to avoid growing the
# count (the comment saying so is still in reflex_hal_esp32c6.c), and were
# described in another comment as living in mask ROM, which they do not. Between
# those two things a borrowed interrupt layer read as a hardware fact.
#
# So: symbols with a ROM prefix are hardware and stay uncounted; anything else
# reached by extern must be named here with a tier, and a new one is an error in
# the same way a new include is. Lowering the number by changing declaration
# style is exactly the hiding place this tool exists to prevent.
ROM_EXTERN_PREFIXES = ("esp_rom_", "Cache_", "ets_", "rom_")
# Mask-ROM entry points that do not carry a rom-ish prefix.
ROM_EXTERN_NAMES = ("software_reset",)
# The project's own symbols, and the linker's wrap machinery. Declaring these
# across translation units is ordinary C, not a borrowed dependency.
PROJECT_EXTERN_PREFIXES = ("reflex_", "goose_", "__real_", "__wrap_", "app_main")

EXTERN_TIERS = {
    # A libbtbb symbol with no public header, called once from Reflex's MAC
    # init exactly where ESP-IDF's mac_init calls it. Declared locally because
    # there is nothing to include; counted because it is still borrowed.
    "ieee802154_txon_delay_set": ("D", "Radio"),
    "intr_handler_set": ("C", "ESP-IDF interrupt dispatch table"),
    "intr_handler_get": ("C", "ESP-IDF interrupt dispatch table"),
    "intr_handler_get_arg": ("C", "ESP-IDF interrupt dispatch table"),
}

EXTERN_RE = re.compile(r"^\s*extern\s+[A-Za-z_][\w \*]*?\b(\w+)\s*\(")


# Bedrock: symbols defined only inside a vendor binary.
#
# The policy this encodes is a decision, not a measurement: a dotted line is
# drawn around the blobs, and everything outside it is Reflex's to take. What
# *is* a measurement is which symbols fall inside — a symbol is bedrock if the
# vendor archives define it, which nm can answer and nobody has to adjudicate.
# That distinction matters, because "this dependency is unavoidable" is exactly
# the claim a project tells itself when it has stopped trying.
#
# Bedrock is reported apart from the ratcheted tiers and is deliberately not
# ratcheted here: it cannot be driven down by writing code, so counting it
# alongside the tiers that can would mix a floor with a debt. It is ratcheted by
# make blob-check instead, which measures the binaries themselves.
BEDROCK_ARCHIVES = (
    "components/esp_phy/lib/{target}/libphy.a",
    "components/esp_phy/lib/{target}/libbtbb.a",
    "components/esp_coex/lib/{target}/libcoexist.a",
)
_bedrock_cache = None


def bedrock_symbols(target="esp32c6"):
    """Symbols the vendor archives define. Empty when the toolchain is absent."""
    global _bedrock_cache
    if _bedrock_cache is not None:
        return _bedrock_cache
    idf = os.environ.get("IDF_PATH", "")
    syms = set()
    if idf:
        for rel in BEDROCK_ARCHIVES:
            path = os.path.join(idf, rel.format(target=target))
            if not os.path.isfile(path):
                continue
            try:
                out = subprocess.run(["riscv32-esp-elf-nm", "-g", "--defined-only", path],
                                     capture_output=True, text=True).stdout
            except (OSError, subprocess.SubprocessError):
                continue
            for line in out.splitlines():
                p = line.split()
                if len(p) == 3 and p[1] in "TtWwDdBbRr":
                    syms.add(p[2])
    _bedrock_cache = syms
    return syms


def classify_extern(sym):
    """Tier for a symbol reached by a local extern, or None if it is ROM."""
    if sym.startswith(ROM_EXTERN_PREFIXES) or sym in ROM_EXTERN_NAMES:
        return None
    if sym.startswith(PROJECT_EXTERN_PREFIXES):
        return None
    # Bedrock before the tier table, and by lookup rather than by listing: a
    # symbol that only a vendor binary defines cannot be reimplemented from
    # register documentation, because there is none.
    if sym in bedrock_symbols():
        return ("Z", "Bedrock: vendor binary")
    return EXTERN_TIERS.get(sym, ("?", "unclassified extern"))


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
                    if m:
                        inc = m.group(1)
                        tier, what = classify(inc)
                        rec = (rel, n, inc, tier, what)
                        if tier is None:
                            unknown.append(rec)
                        elif not_built or rel.startswith(OFF_PATH):
                            off.append(rec)
                        else:
                            on.append(rec)
                        continue
                    # A locally declared extern is a dependency too, and used to
                    # be invisible here. ROM entry points are silicon and stay
                    # uncounted; anything else must be named in EXTERN_TIERS.
                    em = EXTERN_RE.match(line)
                    if not em:
                        continue
                    sym = em.group(1)
                    cls = classify_extern(sym)
                    if cls is None:
                        continue
                    tier, what = cls
                    rec = (rel, n, f"extern {sym}()", tier, what)
                    if tier == "?":
                        unknown.append((rel, n, f"extern {sym}()", None, what))
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
    # The device path never defines this. Declarations fenced off for the host
    # suite are not dependencies of a board.
    "REFLEX_HOST_BUILD": False,
    # Filled in from the build being measured — see BUILD_DEFINED_SYMBOLS.
    # Deliberately absent here rather than guessed: with no value the tool
    # counts both branches, which is the conservative answer.
}

# Symbols whose value is a property of the configuration, not of the project.
#
# REFLEX_OWN_ENTRY is not a Kconfig option; it arrives as a bare -D on the
# compiler command line, so no sdkconfig in the tree records it and a table here
# would be an assertion rather than a measurement. Reading it back from
# compile_commands.json asks the build what the compiler was actually told.
#
# The distinction matters because it is exactly the hole this tool exists to
# close. Fencing a dependency behind #ifndef REFLEX_OWN_ENTRY removes it from
# one configuration and leaves it in another; a tool that cannot tell the two
# apart either counts a dependency the build does not have, or — far worse —
# takes the fence on faith and counts a dependency away that is still there.
# Only symbols named here are read this way. A stray -D on some unrelated
# symbol changes nothing, because an unknown symbol still counts both branches.
BUILD_DEFINED_SYMBOLS = ("REFLEX_OWN_ENTRY",)


def apply_build_config():
    """Set PATH_SYMBOLS from the measured build's sdkconfig.

    The values of CONFIG_* symbols are a property of the configuration, exactly
    like REFLEX_OWN_ENTRY, and they were previously asserted in PATH_SYMBOLS by
    hand — CONFIG_REFLEX_RADIO_802154 and CONFIG_IDF_TARGET_ESP32C6 were simply
    declared true. That was right for the two builds that existed and became
    wrong the moment a third appeared: CONFIG_REFLEX_RADIO_802154_OWN_MAC is set
    in one configuration and not the others, and a hand-written table cannot be
    right about both.

    Reading them from the build's own sdkconfig makes the tool measure the
    configuration rather than remember it. A symbol the file marks "is not set"
    is False; anything absent entirely stays unknown, so both its branches are
    counted.
    """
    path = os.path.join(_build_dir, "sdkconfig")
    if not os.path.isfile(path):
        return 0
    n = 0
    try:
        with open(path, errors="replace") as fh:
            for line in fh:
                line = line.strip()
                m = re.match(r"^(CONFIG_[A-Za-z0-9_]+)=(.*)$", line)
                if m:
                    val = m.group(2).strip()
                    # Only booleans decide preprocessor branches. A string or
                    # number left as True would make `#if CONFIG_X` look taken
                    # when the value is what matters.
                    if val in ("y", "n"):
                        PATH_SYMBOLS[m.group(1)] = (val == "y")
                        n += 1
                    continue
                m = re.match(r"^#\s*(CONFIG_[A-Za-z0-9_]+) is not set$", line)
                if m:
                    PATH_SYMBOLS[m.group(1)] = False
                    n += 1
    except OSError:
        return 0
    return n


def apply_build_defines():
    """Set PATH_SYMBOLS for BUILD_DEFINED_SYMBOLS from the build's own flags.

    Returns a dict of what was resolved, for reporting. A build with no
    compile_commands.json resolves nothing and leaves those symbols unknown,
    so the count stays conservative rather than silently assuming "off".
    """
    cc = os.path.join(_build_dir, "compile_commands.json")
    if not os.path.isfile(cc):
        return {}
    try:
        with open(cc, errors="replace") as fh:
            entries = json.load(fh)
    except (OSError, ValueError):
        return {}
    # Unanimity, not union. A symbol defined for one translation unit and not
    # another has no single value for the whole build, and taking the union
    # would let a flag on a single file discount a fence in every other one —
    # the tool being fooled by exactly the kind of asymmetry it exists to catch.
    # Counted over C compilations only: the scan reads .c/.h/.S, and headers
    # take their value from the .c that includes them.
    seen_c = 0
    counts_defined = {sym: 0 for sym in BUILD_DEFINED_SYMBOLS}
    for e in entries:
        if not str(e.get("file", "")).endswith(".c"):
            continue
        seen_c += 1
        cmd = e.get("command") or " ".join(e.get("arguments", []))
        toks = {t[2:].split("=", 1)[0] for t in cmd.split() if t.startswith("-D")}
        for sym in BUILD_DEFINED_SYMBOLS:
            if sym in toks:
                counts_defined[sym] += 1
    if not seen_c:
        return {}
    resolved = {}
    for sym in BUILD_DEFINED_SYMBOLS:
        n = counts_defined[sym]
        if n == seen_c:
            value = True
        elif n == 0:
            value = False
        else:
            # Split across the build. No honest single value, so leave it
            # unknown: both branches get counted, which overstates rather than
            # understates the dependency.
            print(f"  {sym} defined for {n} of {seen_c} C files — no single value, "
                  f"left unknown and both branches counted")
            continue
        PATH_SYMBOLS[sym] = value
        resolved[sym] = value
    return resolved


def assembly_uses_build_symbol():
    """Scanned .S files that condition on a build-defined symbol.

    CMAKE_C_FLAGS does not reach the assembler — measured, 0 of 8 .S
    compilations carry -DREFLEX_OWN_ENTRY while 980 of 980 .c ones do — so a
    value resolved from C compilations says nothing about an assembly file. No
    .S file conditions on one today; this reports it if that ever changes,
    rather than quietly applying the C answer to a file that never saw the flag.
    """
    hits = []
    for d in SCAN:
        base = os.path.join(ROOT, d)
        if not os.path.isdir(base):
            continue
        for dirpath, _dirs, files in os.walk(base):
            if "build" in dirpath.split(os.sep):
                continue
            for f in files:
                if not f.endswith(".S"):
                    continue
                full = os.path.join(dirpath, f)
                try:
                    text = open(full, errors="replace").read()
                except OSError:
                    continue
                for sym in BUILD_DEFINED_SYMBOLS:
                    if re.search(r"^\s*#\s*(if|ifdef|ifndef|elif)\b.*\b" + sym + r"\b",
                                 text, re.M):
                        hits.append((os.path.relpath(full, ROOT), sym))
    return hits

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


def load_baseline_doc():
    """The baseline file as a dict, or an empty one if it is absent/unreadable."""
    try:
        with open(BASELINE) as fh:
            doc = json.load(fh)
    except (OSError, ValueError):
        return {}
    if not isinstance(doc, dict):
        return {}
    return migrate_baseline_doc(doc)


def migrate_baseline_doc(doc):
    """Read a pre-per-configuration baseline as the one build it measured.

    The file used to hold a single flat "on_path", from the days when
    build_independence was the only configuration measured. An un-migrated
    checkout must keep ratcheting rather than failing open, so that shape is
    read as that configuration's floor.
    """
    if "configurations" not in doc and "on_path" in doc:
        doc = dict(doc)
        doc["configurations"] = {os.path.basename(INDEPENDENCE_BUILD): doc["on_path"]}
    return doc


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

    # After the build directory is settled and before anything is scanned: the
    # configuration decides what "compiled" means, and it also decides the value
    # of the symbols the sources are fenced with.
    n_cfg = apply_build_config()
    if n_cfg:
        print(f"  {n_cfg} CONFIG_* symbols read from {os.path.relpath(_build_dir, ROOT)}/sdkconfig")
    else:
        print("  no sdkconfig for this build: CONFIG_* values fall back to the table")
    resolved = apply_build_defines()
    for sym, val in sorted(resolved.items()):
        print(f"  {sym} = {'defined' if val else 'not defined'} (from compile_commands.json)")
    missing = [s for s in BUILD_DEFINED_SYMBOLS if s not in resolved]
    if missing:
        print(f"  {', '.join(missing)} unresolved for this build, both branches counted")
    asm_hits = assembly_uses_build_symbol()
    for rel, sym in asm_hits:
        print(f"  WARNING: {rel} conditions on {sym}, which the assembler is never "
              f"given — the value above does not apply to it")

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
        "Z": "Bedrock: vendor binary",
    }
    for tier in sorted(set(list(cur) + ["A", "B", "C", "D", "E", "F"])):
        if tier == "Z":
            continue
        n = cur.get(tier, 0)
        mark = "clear" if n == 0 else ""
        print(f"  Tier {tier}  {n:>3}  {labels.get(tier,''):<34} {mark}")
    ratcheted = {t: n for t, n in cur.items() if t != "Z"}
    print(f"\n  ON-PATH TOTAL: {sum(ratcheted.values())}")
    z = cur.get("Z", 0)
    if z:
        # Below the line, and said so plainly. This number going down is not
        # progress and going up is not a regression; it is how much silicon
        # Reflex reaches that has no documentation behind it.
        print(f"  bedrock (vendor binary, not ratcheted here — see make blob-check): {z}")

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

    config = os.path.basename(_build_dir)

    if update:
        doc = load_baseline_doc()
        # Every tier, including the ones at zero. An absent key and a zero mean
        # the same thing to the ratchet, but they do not read the same: a tier
        # that has been driven to zero is the strongest claim this file makes,
        # and omitting it makes that claim look like an oversight. Written out
        # so the baseline states its floors rather than implying them.
        doc.setdefault("configurations", {})[config] = {
            t: cur.get(t, 0) for t in ("A", "B", "C", "D", "E", "F")
        }
        doc["note"] = ("Ratchet baseline, per build configuration. Lower is the only "
                       "legal direction. Regenerate deliberately with "
                       "tools/check_independence.py --update [--build <dir>].")
        doc.pop("on_path", None)
        with open(BASELINE, "w") as fh:
            json.dump(doc, fh, indent=2, sort_keys=True)
            fh.write("\n")
        print(f"\nBaseline updated for {config}: {os.path.relpath(BASELINE, ROOT)}")
        return 0

    if not check:
        return 0

    if unknown:
        print("\nFAILED: unclassified ESP-IDF dependency. Place it in a tier in "
              "tools/check_independence.py, or remove it.")
        return 1

    doc = load_baseline_doc()
    configs = doc.get("configurations")
    if configs is None:
        print("\nFAILED: no baseline. Create it with --update.")
        return 1
    if config not in configs:
        # Refused rather than passed. A configuration with no recorded floor is
        # a configuration nothing is holding, and the whole point of measuring
        # build_own_entry separately is that its numbers are the ones the
        # independence claim rests on.
        print(f"\nFAILED: no ratchet baseline for configuration '{config}'. "
              f"Known: {', '.join(sorted(configs)) or '(none)'}.")
        print(f"  Record it deliberately with:")
        print(f"    python3 tools/check_independence.py --update --build {config}")
        return 1
    base = configs[config]

    # Without the independence build there is nothing to filter against, so the
    # numbers include files that configuration never compiles — an upper bound,
    # not the measurement the baseline records. Ratcheting on it would fail the
    # build for a reason that has nothing to do with the code, so it says so and
    # declines to judge rather than reporting a regression it cannot support.
    if compiled_sources() is None:
        print(f"\n  No {config}/ present, so these counts include "
              "sources that configuration does not compile.")
        print("  Upper bound only — the ratchet is not applied. Build it with:")
        print("    SDKCONFIG_DEFAULTS=sdkconfig.defaults.independence \\")
        print("      idf.py -B build_independence "
              "-DSDKCONFIG=build_independence/sdkconfig build")
        return 0

    regressed = False
    for tier, n in sorted(cur.items()):
        if tier == "Z":
            continue
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
    print(f"\nIndependence: no tier has regressed ({config}).")
    return 0


if __name__ == "__main__":
    sys.exit(main())
