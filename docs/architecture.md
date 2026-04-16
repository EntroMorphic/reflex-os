# Reflex OS Architecture (v2.5.1)

## Intent

Reflex OS is split into a binary host layer and a ternary execution substrate known as **GOOSE** (Geometric Ontologic Operating System Execution).

The binary host layer (ESP-IDF) owns boot, hardware drivers, and low-level scheduling. The GOOSE layer reinterprets the machine as a **Tapestry** of geometric nodes (Cells) and signal flows (Routes).

## The GOOSE Substrate

### 1. The Loom (Ternary Fabric)
A high-performance spatial memory model residing in RTC RAM. It utilizes **Lattice Hashing** for $O(1)$ lookup of ternary cells across power cycles.

### 2. G.O.O.N.I.E.S. & Shadow Paging (The All-Seeing Atlas)
The "Loom DNS." It provides a hierarchical naming service (`agency.led.intent`) for geometric coordinates.

- **Full-Surface Coverage:** the SVD-generated shadow catalog (`goose_shadow_atlas.c`, 9527 entries) covers 100% of the SVD-documented ESP32-C6 MMIO register + field surface (`tools/esp32c6.svd`). Every entry is resolvable by name via `goose_shadow_resolve`, and the shell's `goonies find` falls through from the live registry to the catalog transparently. The `atlas verify` shell command walks the entire catalog with full round-trip equality (name, addr, mask, type, coord) plus an adjacent-pair duplicate sweep. Not included: undocumented registers, eFuse bits outside the SVD field schema, or silicon-revision deltas the SVD file doesn't capture.
- **Shadow Paging:** when a cell (not just a name) is needed, the G.O.O.N.I.E.S. resolver autonomously pages in cells from the catalog to RTC RAM on demand. The bounded 256-slot live Loom is a working-set constraint, not a coverage constraint — catalog names are addressable even without a cell.
- **Loom Eviction:** a round-robin recycling policy that ensures the 256-slot RAM Loom never overflows. Unpinned shadow nodes are evicted to make room for new discoveries.

### 4. Atmospheric Mesh (Global G.O.O.N.I.E.S.)
Reflex OS extends the Tapestry across physical devices via ESP-NOW, implemented in `goose_atmosphere.c`.
- **Peer Scoping:** names prefixed with `peer.` trigger mesh-wide discovery (`ARC_OP_QUERY` → `ARC_OP_ADVERTISE`).
- **Ghost Solidification:** local "ghost cells" represent remote hardware; state changes are arced across the mesh via `ARC_OP_SYNC`.
- **Postural Consensus:** swarms coordinate using ternary sum-and-threshold with a per-packet weight cap (`SWARM_WEIGHT_MAX=4`) and inertial hysteresis (`SWARM_THRESHOLD=10`, `SWARM_ACCUM_MAX=100`). A single peer cannot flip posture alone; consensus requires at least three cooperating peers.
- **Aura Authentication:** every arc packet carries an HMAC-SHA256 MAC over (version, op, coord, name_hash, state, nonce), truncated to 32 bits for wire compatibility. Keys are NVS-backed with first-boot random auto-provisioning; pairing requires an operator to run `aura setkey <hex>` on every peer.
- **Replay Guard:** a 64-slot time-bounded (5 s) replay cache rejects duplicate `(src_mac, nonce)` pairs. Slot index blends nonce and the last two MAC bytes to minimize cross-peer false positives.
- **Protocol Epoch:** `GOOSE_ARC_VERSION` in the packet header lets cross-firmware peers log `AURA_VERSION_MISMATCH` (rate-limited per peer) instead of silently dropping.

### 5. Hierarchical Holons (Recursive Fields)
Manifolds can be nested, allowing complex systems to be treated as single ternary cells.
- **Asynchronous Pulse:** parent fields sample the health of children without temporal dependency, preserving real-time deterministic timing. `internal_process_transitions` recurses up to depth 3.
- **FIELD_PROXY Projection:** subsystems (e.g., `agency.motor`) project a single "success trit" — implemented in `goose_runtime.c` as a read from `routes[0].cached_source->state` on the child sub-field.
- **NEURON Quorum:** `GOOSE_CELL_NEURON` cells aggregate all routes of the child sub-field via ternary sum-and-majority (threshold = floor(n/2)+1) in `neuron_quorum`. Unlike FIELD_PROXY's single-route projection, NEURON aggregation reflects the full sub-field consensus.

### 3. The Supervisor (Harmonic Regulation)
An autonomic regulation pass in `goose_supervisor.c`. The supervisor runs on a 100 ms tick (10 Hz) and interleaves several sub-passes at 1 Hz via time-division:
- **check_equilibrium / rebalance:** monitors supervised fields for disequilibrium (blocked intent routes) and exercises meta-agency to restore flow.
- **weave_sync (Autonomous Fabrication):** scans `fabric_cells[]` for `GOOSE_CELL_NEED` cells with non-zero state and wires them to capability-matched sinks via a generic last-segment resolver. `sys.need.led` → `.led` → any cell with that suffix.
- **learn_sync (Hebbian Plasticity):** reward-gated co-activation rule. Under `sys.ai.reward`, each supervised route's `hebbian_counter` moves by +1 on source/sink agreement and -1 on disagreement. On threshold crossing, the counter commits its sign into `learned_orientation`. Under `sys.ai.pain`, counters decay toward zero (unlearning without reset).
- **swarm_sync:** decays the `swarm_accumulator` toward zero when no atmospheric postures are arriving.

### 6. Purpose and Perception
- **`GOOSE_CELL_PURPOSE`:** a user-declared intent cell (`sys.purpose`) that tells the substrate what the machine is currently being used for. When active, `learn_sync` doubles Hebbian reward increments — the OS learns faster when the user tells it what they're trying to do. Shell: `purpose set/get/clear`.
- **Internal Temperature Sensor:** the first real perception cell. `perception.temp.reading` reads the C6 die temperature every 5 seconds via the ESP-IDF `temperature_sensor` driver and maps it to ternary state: cold (<40°C), normal (40–55°C), warm (>55°C). Shell: `temp`.
- **Tapestry Snapshots (Phase 29):** `goose_snapshot_save/load/clear` serialize and restore supervised-route plasticity (`learned_orientation` + `hebbian_counter`) to NVS. Save and load hold `loom_authority` for consistency with `learn_sync`. NVS keys use FNV-1a hashes of full field names to avoid truncation collisions.

### 7. Coherent Heartbeat (LP RISC-V)
A parallel heartbeat program running on the ESP32-C6 LP coprocessor (`components/goose/ulp/lp_pulse.c`). Loaded and started from `goose_lp_heartbeat_init` at boot; HP mirrors `agency.led.intent` state into `ulp_lp_led_intent` each supervisor pulse via `goose_lp_heartbeat_sync`. LP ticks at ~1 Hz via `ULP_LP_CORE_WAKEUP_SOURCE_LP_TIMER` and increments `ulp_lp_pulse_count`, which HP reads via the `heartbeat` shell command as a liveness indicator.

## Module Layout

## `components/goose/`
The core GOOSE implementation.
- `goose_runtime.c`: lattice hashing, cell allocation, route processing, `loom_authority` serialization, LP heartbeat bootstrap.
- `goose_supervisor.c`: equilibrium checking, harmonic rebalancing, autonomous fabrication (`weave_sync`), Hebbian plasticity (`learn_sync`), swarm decay, LP liveness monitor.
- `goose_atmosphere.c`: ESP-NOW atmospheric mesh — HMAC-SHA256 Aura, replay cache, protocol epoch, self-arc suppression, posture weight cap, mesh RX stats.
- `goose_atlas.c`: the Root Zone — 26 peripheral categories × 4 register channels = 104 pre-woven atlas cells.
- `goose_shadow_atlas.c`: SVD-generated shadow catalog of 9,527 additional MMIO nodes, paged in on demand via `goose_shadow_resolve`.
- `goose_gateway.c`: legacy message bridge between the fabric and GOOSE cell state.
- `goose_library.c`: LoomScript binary loader (`.loom` format, magic `LOOM`).
- `goose_dma.c`, `goose_etm.c`: scaffold bridges for Geometric Flow (GDMA) and Silicon Agency (ETM).
- `ulp/lp_pulse.c`: LP core program (compiled for RISC-V LP target via `ulp_embed_binary`).

## `main/`

ESP-IDF entrypoint only.

Responsibilities:

- call host startup
- currently runs bring-up self-checks and enters the serial shell
- should still shrink over time as `core/` takes ownership of boot behavior

## `core/`

Binary host runtime foundation.

Responsibilities:

- boot sequencing
- version metadata
- logging facade
- uptime helpers
- system state
- service manager
- event bus core

## `vm/`

Ternary compute runtime.

Responsibilities:

- ternary data representation
- ternary arithmetic helpers
- opcode definitions
- VM state model
- interpreter
- program loading
- syscall dispatch from ternary code into host services
- task runtime for scheduled VM execution

## `shell/`

Serial CLI and command dispatch.

Responsibilities:

- line input
- command parsing
- command registry
- system commands
- VM inspection and execution commands
- VM task commands

## `storage/`

Persistent state and configuration.

Responsibilities:

- NVS initialization
- typed config store
- boot counters
- failure markers
- recovery flags

## `services/`

Long-lived host services registered with the service manager.

Responsibilities:

- LED service
- button service
- timer service
- Wi-Fi manager service
- future OTA service
- future ternary task host service if needed

## `drivers/`

Hardware abstraction wrappers around ESP-IDF.

Responsibilities:

- GPIO
- LED
- button
- timer wrappers
- I2C scaffold
- SPI scaffold
- UART scaffold

## `net/`

Network-specific host logic.

Responsibilities:

- Wi-Fi connection lifecycle
- IP status
- future protocol adapters

## Dependency Rules

Allowed direction should remain simple:

- `main` may depend on `core`
- `core` may depend on `storage`, `shell`, `services`, `vm`, `drivers`, and `net`
- `shell` may depend on `core`, `storage`, `services`, `vm`, and `net`
- `services` may depend on `core`, `drivers`, `storage`, `net`, and `vm` only when intentionally hosting VM work
- `vm` may depend on `core` only through narrow host-facing interfaces and syscall hooks
- `drivers` should not depend on `vm`
- `net` should not depend on `vm`

## Design Constraint

Do not spread ternary logic across the whole codebase.

Ternary behavior should stay concentrated in `vm/` and in carefully defined host bridge points. The host remains binary and deterministic. The ternary machine is an execution model hosted by the OS, not a replacement for the ESP-IDF stack.

## First Refactor Target

`main/main.c` still contains boot-time self-check printing and shell handoff. The next host-phase cleanup should move that into `core/boot` or an equivalent startup module.
