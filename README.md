# Reflex OS v2.3 (The All-Seeing Atlas)

Reflex OS is a binary-hosted ternary operating environment for the Seeed Studio XIAO ESP32C6. It reinterprets physical hardware as a coherent **Geometric Tapestry**, replacing traditional binary software abstractions with a software-defined ternary execution substrate known as **GOOSE** (Geometric Ontologic Operating System Execution).

## The Soft Silicon Stack

- **The Loom (Ternary Fabric):** High-performance spatial memory in RTC RAM utilizing **Lattice Hashing** for $O(1)$ coordinate-based cell lookups.
- **G.O.O.N.I.E.S. & Shadow Atlas:** The *Geometric Object Oriented Name Identification Execution Service*. Features an exhaustive **Shadow Atlas** in Flash that provides 100% MMIO coverage through "Just-in-Time" paging.
- **The Sanctuary Guard:** Substrate-level security that prevents unauthorized MMIO mapping and protects against **Shadow Hijacking**.
- **Harmonic Supervisor:** An autonomic "immune system" that monitors the Loom for disequilibrium and re-levels system posture via meta-agency.
- **Atmospheric Arcing:** Secure, Aura-checked ESP-NOW state propagation for inter-system geometric communication.

## Hardware

- **Board:** Seeed Studio XIAO ESP32C6
- **Architecture:** RISC-V HP Core + RISC-V LP Core (Coherent Heartbeat)
- **Memory:** MMU-backed shared ternary memory with MESI-lite Soft-Cache.
- **I/O:** Full peripheral mapping (The Atlas) including GPIO, LEDC, RMT, and PMU.

## Core Documentation

- `ARCHITECTURE.md`: Module layout and the GOOSE geometric paradigm.
- `SECURITY.md`: The Sanctuary Guard and Authority Sentry specifications.
- `bonsai/IMPLEMENTATION-STATUS-BONSAI.md`: Detailed v2.2 validation results.
- `TASM-SPEC.md`: Ternary Assembler syntax and GOOSE-native opcodes.

## Shell Commands

- `help`, `reboot`, `version`, `uptime`, `heap`
- `goonies ls`: List the hierarchical hardware DNS registry.
- `goonies find <name>`: Resolve a name to its 9-trit coordinate.
- `bonsai breach`: Execute a Sanctuary Violation test (rejection demo).
- `bonsai heal`: Trigger a Supervisor rebalance pass on a tilted field.
- `led status`: Query the physical LED state via the Tapestry.
- `config <get|set>`: Persistence management.
- `vm info`: Inspect the Ternary VM state.

## Build & Flash

```bash
# Export ESP-IDF
source ~/Projects/esp-idf/export.sh

# Build & Flash
idf.py build
python3 -m esptool --chip esp32c6 --port /dev/cu.usbmodem1101 --before usb_reset write_flash 0x0 build/bootloader/bootloader.bin 0x8000 build/partition_table/partition-table.bin 0x10000 build/reflex_os.bin
```

## G.O.O.N.I.E.S. Oath (v2.2 Milestone)

Reflex OS v2.2 validates that a machine can be governed by geometry rather than binary logic. By leveraging the ESP32-C6's LP Core and RTC RAM, the OS maintains its identity and security posture even during deep reflection (sleep). 

**"GOONIES never say die!"**
