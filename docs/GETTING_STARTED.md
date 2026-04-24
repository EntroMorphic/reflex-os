# Getting Started with Reflex OS

This guide takes you from zero to a working Reflex OS board. Every step includes verification so you know it worked before moving on.

## 1. Hardware

| Item | Recommendation | Notes |
|------|---------------|-------|
| **Board** | [Seeed XIAO ESP32-C6](https://www.seeedstudio.com/Seeed-Studio-XIAO-ESP32C6-p-5884.html) | RISC-V, USB-C, tiny. This is the primary target. |
| **USB cable** | USB-C data cable | Some cables are charge-only and won't work. If your board doesn't appear as a serial device, try a different cable. |
| **Second board** (optional) | Same, or any ESP32 | For mesh networking experiments. |

No breadboard, LED, or external components needed — the board has a built-in LED and USB serial.

## 2. Install ESP-IDF

Reflex OS builds on [ESP-IDF v5.5](https://docs.espressif.com/projects/esp-idf/en/v5.5/esp32c6/get-started/), Espressif's IoT Development Framework.

### macOS

```bash
brew install cmake ninja dfu-util
mkdir -p ~/esp && cd ~/esp
git clone -b v5.5 --recursive https://github.com/espressif/esp-idf.git
cd esp-idf
./install.sh esp32c6
```

### Linux (Ubuntu/Debian)

```bash
sudo apt install git wget flex bison gperf python3 python3-pip python3-venv \
  cmake ninja-build ccache libffi-dev libssl-dev dfu-util libusb-1.0-0
mkdir -p ~/esp && cd ~/esp
git clone -b v5.5 --recursive https://github.com/espressif/esp-idf.git
cd esp-idf
./install.sh esp32c6
```

### Windows

Use [ESP-IDF Windows Installer v5.5](https://docs.espressif.com/projects/esp-idf/en/v5.5/esp32c6/get-started/windows-setup.html). It installs everything including the ESP-IDF Terminal. Run all subsequent commands in that terminal.

### Verify

```bash
source ~/esp/esp-idf/export.sh    # Add to your shell profile for convenience
idf.py --version
# Expected: ESP-IDF v5.5 or v5.5.x
```

If `source` fails, adjust the path to wherever you cloned esp-idf.

## 3. Clone Reflex OS

```bash
git clone https://github.com/EntroMorphic/reflex-os.git
cd reflex-os
```

## 4. Run Host Tests (no hardware needed)

This verifies your C toolchain works and the codebase is healthy.

```bash
make test
```

**Expected output:**
```
26 passed, 0 failed
```

If this fails, check that you have `gcc` or `clang` installed. On macOS, `xcode-select --install` provides it. On Linux, `sudo apt install build-essential`.

## 5. Build Firmware

```bash
source ~/esp/esp-idf/export.sh    # Skip if already exported in this terminal
idf.py set-target esp32c6         # First time only — sets the chip target
idf.py build
```

**Expected:** Build completes with `Project build complete.` and no errors. First build takes a few minutes; subsequent builds are incremental (~10 seconds).

If the build fails with missing components, ensure you cloned ESP-IDF with `--recursive` and ran `./install.sh esp32c6`.

## 6. Find Your Serial Port

Plug in your board via USB. Then find the port:

### macOS

```bash
ls /dev/cu.usbmodem*
# Expected: /dev/cu.usbmodem1101 (or similar)
```

If nothing appears, try `ls /dev/cu.usb*`. If still nothing, try a different USB cable — yours may be charge-only.

### Linux

```bash
ls /dev/ttyACM*
# Expected: /dev/ttyACM0 (or similar)
```

If you get a permission error later, add yourself to the `dialout` group:
```bash
sudo usermod -aG dialout $USER
# Log out and back in for this to take effect
```

### Windows (ESP-IDF Terminal)

```
mode
# Look for COM3, COM4, etc.
```

Or check Device Manager under "Ports (COM & LPT)".

## 7. Flash

```bash
idf.py -p /dev/cu.usbmodem1101 flash
```

Replace `/dev/cu.usbmodem1101` with your actual port from step 6.

**Expected:** Ends with `Hard resetting via RTS pin...` — the board reboots into Reflex OS.

### Troubleshooting

| Problem | Fix |
|---------|-----|
| `No serial data received` | Wrong port, or charge-only cable. Retry step 6. |
| `Permission denied` | Linux: add yourself to `dialout` group (step 6). macOS: no extra permissions needed. |
| `Failed to connect` | Hold the BOOT button on the board while flashing, release after "Connecting..." appears. |

## 8. Connect to the Shell

Open a serial terminal at 115200 baud:

### macOS / Linux

```bash
screen /dev/cu.usbmodem1101 115200
```

Press Enter. You should see:

```
reflex>
```

Type `help` to see available commands. Type `status` for a system summary.

To exit `screen`: press `Ctrl-A`, then `K`, then `Y`.

### Alternative: idf.py monitor

```bash
idf.py -p /dev/cu.usbmodem1101 monitor
```

This includes automatic crash decoding. Exit with `Ctrl-]`.

### Windows

Use [PuTTY](https://www.putty.org/) or the ESP-IDF Terminal's built-in monitor:
```
idf.py -p COM3 monitor
```

## 9. First Commands

Once you see `reflex>`, try these:

```
reflex> status
```
Shows uptime, purpose, LED state, MAC address, and peer count.

```
reflex> led on
reflex> led off
```
Controls the onboard LED.

```
reflex> purpose set led
```
Declares an operating purpose. The kernel now prioritizes tasks serving LED control and begins Hebbian learning on active routes.

```
reflex> goonies ls
```
Lists the live hardware cells woven into the substrate.

```
reflex> goonies read agency.ledc.conf0
```
Reads a live MMIO register by name — the LED controller's configuration register.

```
reflex> vitals
```
Displays metabolic state: temperature, battery, mesh health, heap pressure.

```
reflex> temp
```
Reads the die temperature in ternary: cold (-1), normal (0), warm (+1).

The full command reference is in the [README shell table](../README.md#shell).

## 10. Install the Python SDK (optional)

The SDK lets you control a board from Python scripts.

```bash
python3 -m venv .venv              # Create a virtual environment
source .venv/bin/activate           # Activate it (Linux/macOS)
# .venv\Scripts\activate            # Windows
pip install -e sdk/python/
```

### Verify

```bash
reflex-cli --discover              # Find connected boards
reflex-cli /dev/cu.usbmodem1101    # Connect and print status
```

### Use in Python

```python
from reflex import ReflexNode

with ReflexNode("/dev/cu.usbmodem1101") as node:
    print(node.status())
    node.led_on()
    node.purpose_set("led")
```

### Run an Example

```bash
python examples/blink/blink.py /dev/cu.usbmodem1101
```

All examples are in [`examples/`](../examples/) with descriptions in [`examples/README.md`](../examples/README.md).

## 11. Launch the Loom Viewer (optional)

Real-time visualization of the GOOSE substrate via [Rerun.io](https://rerun.io):

```bash
pip install rerun-sdk pyserial     # In your venv
python tools/loom_viewer.py /dev/cu.usbmodem1101
```

The viewer connects to the board, enables telemetry, and renders the live substrate topology — cells as nodes, routes as edges, Hebbian plasticity time series, and mesh events. All push-based, no polling.

## 12. Next Steps

| Goal | Resource |
|------|----------|
| Understand the architecture | [`docs/architecture.md`](architecture.md) |
| Learn how the OS boots | [`docs/boot.md`](boot.md) |
| Explore Hebbian learning | [`docs/learning.md`](learning.md) |
| Write ternary VM programs | [`docs/tasm-spec.md`](tasm-spec.md) and [`examples/tasm/`](../examples/tasm/) |
| Set up mesh networking | [`examples/multi-node/`](../examples/multi-node/) |
| Port to a new chip | [`docs/PLATFORM_BACKEND.md`](PLATFORM_BACKEND.md) |
| Contribute | [`CONTRIBUTING.md`](../CONTRIBUTING.md) and [`docs/ONBOARDING.md`](ONBOARDING.md) |
| Security model | [`SECURITY.md`](../SECURITY.md) |
| Full shell reference | [`README.md`](../README.md#shell) |
| Complete documentation index | [`docs/README.md`](README.md) |

## Troubleshooting

### Build fails with "component not found"

```bash
cd ~/esp/esp-idf
git submodule update --init --recursive
./install.sh esp32c6
source export.sh
```

### Board not detected after plugging in

1. Try a different USB cable (charge-only cables have no data lines).
2. Try a different USB port (some hubs don't work).
3. On macOS, check System Information → USB for the device.
4. On Linux, check `dmesg | tail` after plugging in.

### Flash succeeds but shell doesn't respond

1. Close any other program using the serial port (only one connection at a time).
2. Press the RST (reset) button on the board.
3. Try `idf.py -p <port> monitor` instead of `screen`.

### `make test` fails

Ensure you have a C compiler: `gcc --version` or `clang --version`. On macOS, run `xcode-select --install`. On Linux, run `sudo apt install build-essential`.

### Python SDK import error

Make sure you installed in a virtual environment and activated it:
```bash
source .venv/bin/activate
pip install -e sdk/python/
```
