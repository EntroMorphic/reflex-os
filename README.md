# Reflex OS v2.4 (The Apex Mesh)

Reflex OS is a binary-hosted ternary operating environment for the Seeed Studio XIAO ESP32C6. It reinterprets physical hardware as a coherent **Geometric Tapestry**, replacing traditional binary software abstractions with a software-defined ternary execution substrate known as **GOOSE** (Geometric Ontologic Operating System Execution).

## The Distributed Silicon Stack

- **Global G.O.O.N.I.E.S.:** Hardware is addressable by name across physical devices. Peer resources (e.g., `peer.worker.agency.led`) are paged into the local Loom via secure Atmospheric Discovery.
- **Recursive Holons:** Hierarchical manifolds allow complex systems to be treated as single ternary cells, enabling recursive regulation without losing real-time determinism.
- **Postural Swarms:** Multi-node coordination via a "Geometric Hive Mind." Swarms reach consensus using probabilistic ternary summation and inertial state flips.
- **The All-Seeing Atlas:** Exhaustive MMIO coverage (9,531 atomic intents) using **Shadow Paging** and **Loom Eviction** to maintain a fixed 16KB LP RAM footprint.
- **The Sanctuary Guard:** Substrate-level security including MMIO isolation, Authority Sentries, and the **Aura Shield** for secure inter-system arcing.

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
