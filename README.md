# Reflex OS

A purpose-aware ternary operating system for embedded mesh networks. ~24,500 lines of C and assembly, hardware-validated on RISC-V (ESP32-C6) and Xtensa (ESP32).

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
- **The All-Seeing Atlas** — 9,531-node shadow catalog of the ESP32-C6 MMIO surface, 104 nodes pre-woven at boot.
- **Coherent Heartbeat** — LP RISC-V coprocessor mirroring intent state at 1Hz across the RTC boundary.

## Hardware

| Target | Architecture | Role | Status |
|--------|-------------|------|--------|
| Seeed XIAO ESP32-C6 | RISC-V | Primary (kernel, substrate, mesh) | Production |
| ESP32 (original) | Xtensa | Mesh peer | Validated |

## Build & Flash

```bash
# ESP32-C6 (primary)
source ~/Projects/esp-idf/export.sh
idf.py set-target esp32c6
idf.py build
idf.py -p /dev/cu.usbmodem1101 flash

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
| `reboot` | Warm software reset |
| `sleep <secs>` | Enter deep sleep for N seconds (LP-timer wake) |
| `led status` | Query the physical LED state via the Tapestry |
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
