# Reflex OS

Reflex OS is a binary-hosted ternary operating environment for the Seeed Studio XIAO ESP32C6, built on ESP-IDF.

The project currently includes a working ternary VM foundation on hardware:

- balanced ternary data model
- `word18` execution words and packed trit storage
- ternary arithmetic helpers
- MVP soft-opcode ISA
- register-based interpreter
- VM image loader
- VM shell commands over USB Serial/JTAG
- host syscall bridge
- FreeRTOS-backed VM task runtime

## Hardware

- Board: Seeed Studio XIAO ESP32C6
- Flash: 4MB
- Console: USB Serial/JTAG

## Current Capabilities

- boots and prints Reflex OS bring-up output on hardware
- runs ternary VM self-checks at startup
- exposes VM control commands over the serial shell
- supports host syscalls for log, uptime, and scalar config reads
- supports a background VM task runtime

## Docs

- `OS-PRD.md`: product requirements
- `OS-BACKLOG.md`: implementation backlog and task order
- `ARCHITECTURE.md`: project module layout and boundaries
- `TERNARY-ARCHITECTURE.md`: ternary model decisions
- `SOFT-OPCODES.md`: MVP ISA
- `VM-STATE.md`: VM state model
- `VM-LOADER.md`: VM image and loader contract
- `VM-SYSCALLS.md`: syscall bridge contract
- `IMPLEMENTATION-STATUS.md`: current implemented state and validation notes

## Shell Commands

Current shell commands:

- `help`
- `vm info`
- `vm load`
- `vm run [steps]`
- `vm step`
- `vm regs`
- `vm task start`
- `vm task stop`
- `vm task status`

## Build

```bash
source /Users/aaronjosserand-austin/Projects/esp-idf/export.sh
idf.py build
```

## Flash

```bash
source /Users/aaronjosserand-austin/Projects/esp-idf/export.sh
idf.py -p /dev/cu.usbmodem1101 flash
```

## Hardware Validation Baseline

The current image has been validated on the XIAO ESP32C6 for:

- ternary self-check
- VM self-check
- VM loader self-check
- VM syscall bridge
- VM task runtime
- serial shell command execution
