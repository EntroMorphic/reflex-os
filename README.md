# Reflex OS v2.6.0

[![Build](https://github.com/EntroMorphic/reflex-os/actions/workflows/build.yml/badge.svg)](https://github.com/EntroMorphic/reflex-os/actions/workflows/build.yml)

Reflex OS is a binary-hosted ternary operating environment for the Seeed Studio XIAO ESP32-C6. It reinterprets physical hardware as a coherent **Geometric Tapestry**, replacing traditional binary software abstractions with a software-defined ternary execution substrate known as **GOOSE** — the **G**eometric **O**ntologic **O**perating **S**ystem **E**xecution.

## Highlights

- **Global G.O.O.N.I.E.S.** — Hardware is addressable by name across physical devices. Peer resources (e.g. `peer.worker.agency.led`) are paged into the local Loom via secure atmospheric discovery.
- **Recursive Holons** — Hierarchical manifolds let complex subsystems be treated as single ternary cells for recursive regulation without losing real-time determinism.
- **Postural Swarms** — Multi-node coordination via a geometric hive mind. Swarms reach consensus using ternary summation with per-peer weight caps and inertial hysteresis.
- **The All-Seeing Atlas** — A 9,531-node shadow catalog of the ESP32-C6 MMIO surface, with 104 high-priority nodes pre-woven at boot and the remainder paged on demand into a 256-slot active Loom via shadow paging and round-robin eviction.
- **The Sanctuary Guard** — Substrate-level security including MMIO isolation, authority sentries, HMAC-SHA256 **Aura** packets with replay cache, and NVS-provisioned keys for secure inter-system arcing.
- **Coherent Heartbeat** — A LP RISC-V coprocessor program that ticks in parallel to HP at 1 Hz, mirroring geometric intent state across the RTC boundary.

## Hardware

- **Board:** Seeed Studio XIAO ESP32-C6
- **Architecture:** RISC-V HP Core + RISC-V LP Core (Coherent Heartbeat via the ULP LP-core coprocessor)
- **Memory:** Region-protected shared ternary memory with a three-state (I/E/M) MESI-lite soft-cache and host-side coherency proxy
- **I/O:** Full peripheral mapping (the Atlas) including GPIO, LEDC, RMT, PMU, and the LP I/O subsystem

## Build & Flash

```bash
source ~/Projects/esp-idf/export.sh
idf.py set-target esp32c6     # only needed on first build
idf.py build
idf.py -p /dev/cu.usbmodem1101 flash
```

A clean checkout regenerates `sdkconfig` from `sdkconfig.defaults`. The target is pinned via `CONFIG_IDF_TARGET="esp32c6"` in the defaults file.

## Shell

| Command | Description |
|---|---|
| `help` | List available commands |
| `reboot` | Warm software reset |
| `sleep <secs>` | Enter deep sleep for N seconds (LP-timer wake) |
| `led status` | Query the physical LED state via the Tapestry |
| `goonies ls` | List the hierarchical hardware DNS registry |
| `goonies find <name>` | Resolve a name: live registry first, then fall through to the 9527-entry shadow catalog. Output is labeled `[live]` or `[shadow]`. |
| `atlas verify` | Walk the entire SVD-documented MMIO shadow catalog (full round-trip + duplicate sweep). Prints progress dots; reports `ok=N/N, duplicates=D, failures=F`. |
| `vm info` | Inspect the ternary VM state |
| `vm loadhex <HEX>` | Load a CRC32-verified packed image |
| `heartbeat` | Read the LP core's parallel pulse counter |
| `aura setkey <32 hex>` | Provision the HMAC-SHA256 Aura key into NVS |
| `mesh mac` | Print the local Wi-Fi STA MAC (used to identify boards in a mesh trial) |
| `mesh emit <state>` | Broadcast an `ARC_OP_SYNC` packet with the given ternary state (`-1\|0\|1`) without mutating the local cell |
| `mesh query <name>` | Broadcast an `ARC_OP_QUERY` for `<name>`; peers respond with `ARC_OP_ADVERTISE` if they have the name locally |
| `mesh posture <state> <weight>` | Broadcast an `ARC_OP_POSTURE` with weight clamped to `SWARM_WEIGHT_MAX=4` |
| `mesh stat` | Dump the mesh RX counters (sync/query/advertise/posture, plus version_mismatch, aura_fail, replay_drop, self_drop) |
| `services` | List registered services |
| `config <get\|set>` | Persistence management |
| `bonsai <experiment>` | Run one of the in-tree bonsai experiments — see `bonsai/EXPERIMENT-*.md` |

## Documentation

Canonical project documentation lives in [`docs/`](docs/):

- [`docs/architecture.md`](docs/architecture.md) — Module layout and the GOOSE substrate.
- [`docs/implementation-status.md`](docs/implementation-status.md) — What is built, validated, and what limits are known.
- [`docs/strategy.md`](docs/strategy.md) — The Chronicler's Path: gaps, advantages, and next phases.
- [`docs/potentials.md`](docs/potentials.md) — Realized milestones and the biological-frontier roadmap.
- [`docs/tasm-spec.md`](docs/tasm-spec.md) — Ternary Assembler syntax and GOOSE-native opcodes.
- [`docs/ternary-architecture.md`](docs/ternary-architecture.md) — Background on the ternary data model.
- [`docs/vm/`](docs/vm/) — Ternary VM internals: cache, loader, state, syscalls.
- [`SECURITY.md`](SECURITY.md) — Sanctuary Guard, Authority Sentry, Aura protocol.
- [`CONTRIBUTING.md`](CONTRIBUTING.md) — How to contribute.
- [`CHANGELOG.md`](CHANGELOG.md) — Release notes.

## License

See [`LICENSE`](LICENSE).
