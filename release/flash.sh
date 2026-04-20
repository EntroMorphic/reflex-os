#!/bin/bash
# Reflex OS — Flash Script
# Usage: ./flash.sh [port]
# Example: ./flash.sh /dev/cu.usbmodem1101
#
# Requirements: Python 3 + esptool (pip install esptool)

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PORT="${1:-}"

if [ -z "$PORT" ]; then
    # Auto-detect: look for ESP32-C6 USB-JTAG port
    if [ "$(uname)" = "Darwin" ]; then
        PORT=$(ls /dev/cu.usbmodem* 2>/dev/null | head -1)
    else
        PORT=$(ls /dev/ttyACM* 2>/dev/null | head -1)
    fi
    if [ -z "$PORT" ]; then
        echo "Error: No ESP32-C6 detected. Plug in the board or specify port:"
        echo "  ./flash.sh /dev/cu.usbmodem1101"
        exit 1
    fi
    echo "Auto-detected port: $PORT"
fi

echo ""
echo "  ╔══════════════════════════════════════╗"
echo "  ║      Flashing Reflex OS...           ║"
echo "  ╚══════════════════════════════════════╝"
echo ""

python3 -m esptool \
    --chip esp32c6 \
    -p "$PORT" \
    -b 460800 \
    --before default_reset \
    --after hard_reset \
    write_flash \
    --flash_mode dio \
    --flash_size 4MB \
    --flash_freq 80m \
    0x0 "$SCRIPT_DIR/bootloader.bin" \
    0x8000 "$SCRIPT_DIR/partition-table.bin" \
    0x10000 "$SCRIPT_DIR/reflex_os.bin"

echo ""
echo "Done. Board will reboot into Reflex OS."
echo "Connect with: screen $PORT 115200"
