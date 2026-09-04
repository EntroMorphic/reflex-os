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
command -v docker >/dev/null 2>&1 || { echo "docker required for rom-check"; exit 1; }

# "PROVIDE ( name = 0xADDR );" -> "name 0xADDR"
grep -oE '^PROVIDE \( *[A-Za-z_][A-Za-z0-9_]* *= *0x[0-9a-fA-F]+' "$LD" \
  | sed -E 's/^PROVIDE \( *//; s/ *= */ /' > /tmp/reflex_rom_want.$$
trap 'rm -f /tmp/reflex_rom_want.$$' EXIT

count=$(wc -l < /tmp/reflex_rom_want.$$)
[ "$count" -gt 0 ] || { echo "no PROVIDE lines parsed from $LD"; exit 1; }

out=$(docker run --rm -v /tmp/reflex_rom_want.$$:/want:ro "$IDF_IMAGE" bash -c '
cd /opt/esp/idf/components/esp_rom/esp32c6/ld
fail=0
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
done < /want
exit $fail
' 2>&1)
rc=$?

if [ $rc -ne 0 ]; then
    echo "$out" | grep -E "MISMATCH|MISSING|UNRESOLVED" || echo "$out" | tail -10
    echo
    echo "ROM symbol check FAILED — an address diverges from ESP-IDF's rom.ld."
    exit 1
fi
echo "ROM symbols: $count addresses match ESP-IDF's esp32c6 rom.ld."
