#!/usr/bin/env bash
#
# Prove that Reflex's generated SoC constants equal the ESP-IDF macros they
# replace, by compiling tools/soc_assert_bridge.c — a translation unit of
# _Static_asserts — with the real ESP-IDF toolchain and include paths.
#
# This is the step that makes the cutover safe. A register address is not
# something to eyeball: a wrong value does not fail to build, it writes to a
# different peripheral. Rather than trusting the SVD derivation, this compares
# every generated constant against the vendor macro and fails the build on any
# divergence.
#
# Flags come from the project's own compile_commands.json, so the bridge is
# compiled exactly as the firmware is — same defines, same include order, same
# toolchain — rather than with paths guessed by hand.
#
# Requires a prior `make idf-build` for compile_commands.json.
# Usage: tools/check_soc_bridge.sh   (or: make soc-bridge)

set -euo pipefail
cd "$(dirname "$0")/.."

CC_JSON=build/compile_commands.json
IDF_IMAGE=${IDF_IMAGE:-espressif/idf:release-v5.5}

[ -f "$CC_JSON" ] || {
    echo "$CC_JSON not found — run 'make idf-build' first."; exit 1; }

# Run natively when we are already inside an ESP-IDF environment (the CI build
# jobs are), and reach for Docker only when we are not. Without this the CI
# job would need Docker inside a container to check what it just compiled.
NATIVE=0
[ -n "${IDF_PATH:-}" ] && [ -d "${IDF_PATH:-/nonexistent}" ] && NATIVE=1
if [ "$NATIVE" = "0" ]; then
    command -v docker >/dev/null 2>&1 || {
        echo "docker required (or run inside an ESP-IDF environment)"; exit 1; }
fi

WORK=$(mktemp -d)
trap 'rm -rf "$WORK"' EXIT

python3 - "$CC_JSON" "$WORK/run.sh" <<'PY'
import json, shlex, sys
cc, out = sys.argv[1], sys.argv[2]
entries = json.load(open(cc))
ref = [e for e in entries if e["file"].endswith("reflex_hal_esp32c6.c")]
if not ref:
    sys.exit("no compile entry for reflex_hal_esp32c6.c; rebuild with idf-build")
toks = shlex.split(ref[0]["command"])
cc_bin = toks[0]
flags = [t for t in toks if t.startswith(("-I", "-D", "-std", "-mabi", "-march"))]
# The bridge includes both worlds; -Iinclude reaches Reflex's own header.
flags += ["-I/work/include"]
cmd = " ".join([cc_bin, "-fsyntax-only"] + [shlex.quote(f) for f in flags] +
               ["/work/tools/soc_assert_bridge.c"])
open(out, "w").write("#!/bin/bash\nset -e\n" + cmd + "\n")
PY
chmod +x "$WORK/run.sh"

n=$(grep -c '_Static_assert' tools/soc_assert_bridge.c || echo 0)

# Key on the compiler's exit status, not on grepping its output. Piping the
# build into `grep` and testing that was the first version, and it reported
# success on a deliberately corrupted constant: under `pipefail` the pipeline
# inherits the compiler's failure, so the `if` went false exactly when the
# assertion fired. That is the same shape as the `format-check` recipe this
# repository already had to fix — a check that cannot fail.
if [ "$NATIVE" = "1" ]; then
    # compile_commands.json records container paths; /work is this checkout.
    sed -i "s#/work/#$PWD/#g" "$WORK/run.sh"
    run_bridge() { "$WORK/run.sh" 2>&1; }
else
    run_bridge() { docker run --rm -u "$(id -u):$(id -g)" -e HOME=/tmp \
        -v "$PWD":/work -v "$WORK":/w -w /work "$IDF_IMAGE" /w/run.sh 2>&1; }
fi

if ! out=$(run_bridge); then
    echo "$out" | grep -E "static assertion|error:" || echo "$out" | tail -20
    echo
    echo "SoC bridge FAILED — a generated constant diverges from ESP-IDF."
    echo "Regenerate with tools/soc_scraper.py; do not hand-edit the header."
    exit 1
fi
echo "SoC bridge: $n constants proved identical to ESP-IDF."
