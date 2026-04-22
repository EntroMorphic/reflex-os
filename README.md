# Reflex OS

A purpose-aware ternary operating system for embedded mesh networks. ~26,000 lines of C and assembly, hardware-validated on RISC-V (ESP32-C6) and Xtensa (ESP32).

**In plain terms:** Reflex OS is firmware for ESP32 microcontrollers that represents hardware state using three values (-1, 0, +1) instead of binary. You tell the OS what you're trying to do (`purpose set led`), and it prioritizes the hardware routes that serve that goal — learning which connections matter through a reward signal, like a nervous system forming habits.

Reflex OS reinterprets physical hardware as a coherent **Geometric Tapestry**, replacing traditional binary software abstractions with a ternary execution substrate known as **GOOSE** — the **G**eometric **O**ntologic **O**perating **S**ystem **E**xecution. The kernel owns interrupt context switching, modulates task priorities based on user-declared purpose, and learns from reward signals via Hebbian associative learning.

## Architecture

```
┌─────────────────────────────────────────────────┐
│  Substrate (GOOSE)                              │
│  Cells → Routes → Fields → Holons              │
│  Ternary state propagation + Hebbian learning   │
├─────────────────────────────────────────────────┤
│  Kernel Supervisor                              │
│  Purpose-modulated priorities, 1Hz policy tick  │
├─────────────────────────────────────────────────┤
│  Port Assembly (reflex_portasm.S)               │
│  Owns rtos_int_enter/exit via --wrap            │
├─────────────────────────────────────────────────┤
│  Scheduling HAL (FreeRTOS or reflex_task_kernel)│
├─────────────────────────────────────────────────┤
│  Hardware (ESP32-C6 / ESP32)                    │
└─────────────────────────────────────────────────┘
```

## Highlights

- **Purpose-Modulated Scheduling** — User declares intent (`purpose set photography`); the kernel boosts tasks serving that domain, learns faster under reward, deactivates non-matching holons.
- **Hebbian Learning in the Kernel** — Fire-together-wire-together: routes that co-activate under reward commit learned orientations. The OS literally rewires its routing topology through experience.
- **Multi-Architecture** — Runs on ESP32-C6 (RISC-V) and ESP32 (Xtensa). One codebase, platform-specific backends behind `reflex_task.h` / `reflex_hal.h`.
- **Hardware Independent** — In 802.15.4 mode, the platform depends on ONE open-source component (ieee802154). All drivers are direct register writes. Custom bootloader (Boot0, 5.2KB).
- **Global G.O.O.N.I.E.S.** — Hardware addressable by name across physical devices via secure atmospheric mesh.
- **Recursive Holons** — Named field groups managed as lifecycle units, activated/deactivated by purpose domain matching.
- **The All-Seeing Atlas** — 9,527-node shadow catalog of the ESP32-C6 MMIO surface, 104 nodes pre-woven at boot.
- **Coherent Heartbeat** — LP RISC-V coprocessor mirroring intent state at 1Hz across the RTC boundary.
- **Autonomous Learning** — The OS generates its own reward/pain signals by evaluating purpose fulfillment. No manual input needed.
- **Mesh Auto-Discovery** — Boards sharing an Aura key find each other automatically via `ARC_OP_DISCOVER` heartbeats.
- **Loom Viewer** — Real-time substrate visualization via Rerun.io. Push-based telemetry streams topology, plasticity, and mesh events to a graphical viewer with zero polling overhead.

## Hardware

| Target | Architecture | Role | Status |
|--------|-------------|------|--------|
| Seeed XIAO ESP32-C6 | RISC-V | Primary (kernel, substrate, mesh) | Production |
| ESP32 (original) | Xtensa | Mesh peer | Validated |

## Build & Flash

### Prerequisites

- [ESP-IDF v5.5](https://docs.espressif.com/projects/esp-idf/en/v5.5/esp32c6/get-started/) — Espressif's IoT Development Framework
- Python 3.8+ (for TASM compiler and SDK)
- `clang-format` (optional, for `make format-check`: `brew install clang-format` / `apt install clang-format`)

```bash
# ESP32-C6 (primary)
source ~/Projects/esp-idf/export.sh   # Adjust path to your ESP-IDF install
idf.py set-target esp32c6
idf.py build
idf.py -p /dev/cu.usbmodem1101 flash  # Adjust port for your board

# ESP32 (mesh peer)
idf.py -B build_esp32 set-target esp32
SDKCONFIG_DEFAULTS="sdkconfig.defaults.esp32" idf.py -B build_esp32 build
```

### Radio Backend Selection

```
idf.py menuconfig → Reflex OS → Radio backend
  [*] IEEE 802.15.4 (blob-free)    ← fully independent of ESP-IDF
  [ ] ESP-NOW (Wi-Fi)              ← requires WiFi binary blob
```

## Shell

| Command | Description |
|---|---|
| `help` | List available commands |
| `status` | System summary (uptime, purpose, LED, MAC, peers) |
| `reboot` | Warm software reset |
| `sleep <secs>` | Enter deep sleep for N seconds (LP-timer wake) |
| `led on` / `led off` | Control the onboard LED |
| `led status` | Query the physical LED state |
| `goonies ls` | List the hierarchical hardware DNS registry |
| `goonies find <name>` | Resolve a name: live registry first, then fall through to the 9527-entry shadow catalog. Output is labeled `[live]` or `[shadow]`. |
| `atlas verify` | Walk the entire SVD-documented MMIO shadow catalog (full round-trip + duplicate sweep). Prints progress dots; reports `ok=N/N, duplicates=D, failures=F`. |
| `temp` | Read the internal die temperature (ternary state: cold/normal/warm) |
| `purpose set <name>` | Declare the current operating purpose; persists name to NVS. Amplifies Hebbian plasticity and biases `weave_sync` routing toward the named domain. |
| `purpose get` | Report the active purpose name (or "inactive") |
| `purpose clear` | Clear the active purpose and erase from NVS |
| `snapshot save` | Persist supervised-route plasticity (learned_orientation + hebbian_counter) to NVS |
| `snapshot load` | Restore plasticity from the last snapshot |
| `snapshot clear` | Erase all snapshot blobs from NVS |
| `vm info` | Inspect the ternary VM state (status, IP, steps) |
| `vm run <name>` | Load and execute an embedded program by name |
| `vm stop` | Halt the running VM |
| `vm list` | List all embedded programs |
| `vm loadhex <HEX>` | Load a CRC32-verified packed image from hex string |
| `heartbeat` | Read the LP core's parallel pulse counter |
| `aura setkey <32 hex>` | Provision the HMAC-SHA256 Aura key into NVS |
| `mesh mac` | Print the local Wi-Fi STA MAC (used to identify boards in a mesh trial) |
| `mesh emit <state>` | Broadcast an `ARC_OP_SYNC` packet with the given ternary state (`-1\|0\|1`) without mutating the local cell |
| `mesh query <name>` | Broadcast an `ARC_OP_QUERY` for `<name>`; peers respond with `ARC_OP_ADVERTISE` if they have the name locally |
| `mesh posture <state> <weight>` | Broadcast an `ARC_OP_POSTURE` with weight clamped to `SWARM_WEIGHT_MAX=4` |
| `mesh stat` | Dump the mesh RX counters (sync/query/advertise/posture/mmio_sync, plus version_mismatch, aura_fail, replay_drop, self_drop) |
| `mesh status` | Mesh summary (peer count, total RX/TX, per-peer status) |
| `mesh ping` | Broadcast a sync arc to all peers |
| `mesh peer add <name> <mac>` | Register a named peer for MMIO sync (e.g., `mesh peer add bravo B4:3A:45:8A:C8:24`) |
| `mesh peer ls` | List registered peers with active/stale status and last-seen time |
| `services` | List registered services |
| `config <get\|set>` | Persistence management |
| `bonsai <experiment>` | Run one of the in-tree bonsai experiments — see `bonsai/EXPERIMENT-*.md` |
| `telemetry <on\|off>` | Enable/disable real-time substrate telemetry streaming to the host |

## Loom Viewer (Substrate Visualization)

Real-time visualization of the GOOSE substrate via [Rerun.io](https://rerun.io):

```bash
pip install rerun-sdk pyserial
python tools/loom_viewer.py /dev/cu.usbmodem1101
```

The viewer connects to the board, enables telemetry, and renders the live substrate topology (cells as nodes, routes as edges), Hebbian plasticity time series, mesh arc events, and supervisor equilibrium — all push-based with no polling.

## Documentation

Canonical project documentation lives in [`docs/`](docs/):

- [`docs/architecture.md`](docs/architecture.md) — Module layout and the GOOSE substrate.
- [`docs/boot.md`](docs/boot.md) — Boot sequence: what happens from power-on to shell.
- [`docs/holons.md`](docs/holons.md) — Holon lifecycle: purpose-driven field group activation.
- [`docs/learning.md`](docs/learning.md) — Hebbian learning: reward, pain, and route plasticity.
- [`docs/sleep.md`](docs/sleep.md) — Deep sleep: what survives, what's lost, wakeup.
- [`docs/storage.md`](docs/storage.md) — Persistent storage: KV store and config patterns.
- [`docs/implementation-status.md`](docs/implementation-status.md) — What is built, validated, and what limits are known.
- [`docs/strategy.md`](docs/strategy.md) — The Chronicler's Path: gaps, advantages, and next phases.
- [`docs/potentials.md`](docs/potentials.md) — Realized milestones and the biological-frontier roadmap.
- [`docs/tasm-spec.md`](docs/tasm-spec.md) — Ternary Assembler syntax and GOOSE-native opcodes.
- [`docs/ternary-architecture.md`](docs/ternary-architecture.md) — Background on the ternary data model.
- [`docs/vm/`](docs/vm/) — Ternary VM internals: cache, loader, state, syscalls.
- [`SECURITY.md`](SECURITY.md) — Sanctuary Guard, Authority Sentry, Aura protocol.
- [`sdk/python/README.md`](sdk/python/README.md) — Python SDK: serial control of Reflex boards.
- [`examples/tasm/README.md`](examples/tasm/README.md) — TASM assembler: write programs for the ternary VM.
- [`CONTRIBUTING.md`](CONTRIBUTING.md) — How to contribute.
- [`CHANGELOG.md`](CHANGELOG.md) — Release notes.

## License

See [`LICENSE`](LICENSE).
