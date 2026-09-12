#!/usr/bin/env bash
#
# Verify every ROM address in platform/esp32c6/reflex_rom_esp32c6.ld against the
# ROM linker scripts ESP-IDF ships for the ESP32-C6.
#
# The fragment is inert today — ESP-IDF's own esp32c6.rom.ld resolves these
# symbols while the build is still an ESP-IDF build — which is exactly why it
# needs checking. An unused artifact nobody verifies rots quietly, and the
# failure mode when it is finally used is a jump to whatever occupies that
# address. Several ESP-IDF names are aliases (esp_rom_printf is ets_printf), so
# an alias is followed to the underlying symbol before comparing.
#
# Usage: tools/check_rom_symbols.sh   (or: make rom-check)

set -uo pipefail
cd "$(dirname "$0")/.."

LD=platform/esp32c6/reflex_rom_esp32c6.ld
IDF_IMAGE=${IDF_IMAGE:-espressif/idf:release-v5.5}

[ -f "$LD" ] || { echo "$LD not found"; exit 1; }

# Run natively when we are already inside an ESP-IDF environment, and reach for
# Docker only when we are not. The ROM linker scripts this gate reads against
# are plain text files ESP-IDF ships; a local checkout has them, so requiring a
# container to grep them made the gate unrunnable on a maintainer's machine for
# no reason other than habit.
NATIVE=0
IDF_LD_DIR=""
if [ -n "${IDF_PATH:-}" ] && [ -d "${IDF_PATH:-/nonexistent}/components/esp_rom/esp32c6/ld" ]; then
    NATIVE=1
    IDF_LD_DIR="$IDF_PATH/components/esp_rom/esp32c6/ld"
fi
if [ "$NATIVE" = "0" ]; then
    "$(dirname "$0")/require_docker.sh" "rom-check (or run inside an ESP-IDF environment)" || exit 1
fi

# "PROVIDE ( name = 0xADDR );" -> "name 0xADDR"
grep -oE '^PROVIDE \( *[A-Za-z_][A-Za-z0-9_]* *= *0x[0-9a-fA-F]+' "$LD" \
  | sed -E 's/^PROVIDE \( *//; s/ *= */ /' > /tmp/reflex_rom_want.$$

count=$(wc -l < /tmp/reflex_rom_want.$$)
[ "$count" -gt 0 ] || { echo "no PROVIDE lines parsed from $LD"; exit 1; }

# The comparison itself is identical in both worlds; only the directory holding
# ESP-IDF's ROM linker scripts differs. Keep one copy of the logic and hand it
# that directory, so the native path and the Docker path cannot drift apart.
CMP=$(mktemp)
trap 'rm -f /tmp/reflex_rom_want.$$ "$CMP"' EXIT
cat > "$CMP" <<'CMPEOF'
cd "$1"
fail=0
seen=0
while read -r name want; do
  # Follow one level of aliasing: PROVIDE ( a = b );
  target=$(grep -hE "^PROVIDE *\( *$name *=" *.ld 2>/dev/null | head -1 | sed -E "s/.*= *//; s/ *\).*//; s/;.*//")
  if [ -z "$target" ]; then
      target=$(grep -hE "^$name *=" *.ld 2>/dev/null | head -1 | sed -E "s/.*= *//; s/;.*//")
  fi
  case "$target" in
    0x*) got="$target" ;;
    "")  echo "MISSING $name (not in ESP-IDF rom.ld)"; fail=1; continue ;;
    *)   got=$(grep -hE "^$target *=" *.ld 2>/dev/null | head -1 | sed -E "s/.*= *//; s/;.*//") ;;
  esac
  if [ -z "$got" ]; then echo "UNRESOLVED $name -> $target"; fail=1; continue; fi
  # Normalise to lowercase for comparison.
  a=$(printf "%s" "$want" | tr "A-Z" "a-z")
  b=$(printf "%s" "$got"  | tr "A-Z" "a-z")
  if [ "$a" != "$b" ]; then echo "MISMATCH $name: ours=$want idf=$got"; fail=1; fi
  seen=$((seen + 1))
done
# A gate that compared nothing must not report success.
if [ "$seen" -eq 0 ]; then echo "NO SYMBOLS READ (empty input)"; fail=1; fi
exit $fail
CMPEOF
chmod +x "$CMP"

if [ "$NATIVE" = "1" ]; then
    out=$(bash "$CMP" "$IDF_LD_DIR" < /tmp/reflex_rom_want.$$ 2>&1)
else
    out=$(docker run --rm -i -v "$CMP":/cmp.sh:ro \
        "$IDF_IMAGE" bash /cmp.sh /opt/esp/idf/components/esp_rom/esp32c6/ld \
        < /tmp/reflex_rom_want.$$ 2>&1)
fi
rc=$?

if [ $rc -ne 0 ]; then
    echo "$out" | grep -E "MISMATCH|MISSING|UNRESOLVED" || echo "$out" | tail -10
    echo
    echo "ROM symbol check FAILED — an address diverges from ESP-IDF's rom.ld."
    exit 1
fi
echo "ROM symbols: $count addresses match ESP-IDF's esp32c6 rom.ld."
