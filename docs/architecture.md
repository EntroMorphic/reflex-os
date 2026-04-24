# Reflex OS Architecture

## Intent

Reflex OS is a purpose-aware ternary operating system. It consists of three layers:

1. **Reflex Kernel** — owns interrupt context switching, tick generation, and scheduling policy. Modulates task priorities based on purpose, Hebbian maturity, and holon lifecycle.
2. **GOOSE Substrate** — the ternary execution fabric. Cells, routes, fields, and holons propagate balanced ternary state and learn from reward signals.
3. **Platform HAL** — direct register access to hardware (GPIO, timers, RNG, flash, radio). Abstracted behind `reflex_hal.h`, `reflex_task.h`, `reflex_kv.h`, `reflex_radio.h`.

The system builds for ESP32-C6 (RISC-V, primary) and ESP32 (Xtensa, mesh peer). In 802.15.4 mode, the platform depends on ONE open-source component. All other hardware access is direct register writes or mask ROM calls.

## The Reflex Kernel

### Port Assembly (`kernel/reflex_portasm.S`)
Intercepts FreeRTOS's `rtos_int_enter`/`rtos_int_exit` via `--wrap` linker flags. Handles: TCB pivot (save/restore SP), ISR stack switching, hardware stack guard (ASSIST_DEBUG peripheral). Runs in IRAM.

### Supervisor (`kernel/reflex_freertos_compat.c`)
Highest-priority task. Calls a registered policy function at 1Hz. The policy is provided by the GOOSE substrate via weak/strong symbol linkage — the kernel doesn't know about cells or routes.

### Policy Engine (`components/goose/goose_supervisor.c`)
Reads purpose, resolves route endpoint names via `goonies_resolve_name_by_coord`, applies domain-specific priority boost (+3 for matching, +1 general). Hebbian maturity adds +2. Pain signal dampens. Deactivated holons force base priority.

### Standalone Backend (`kernel/reflex_task_kernel.c`)
Complete implementation of all 13 `reflex_task.h` functions: create, delete, delay, yield, get_by_name, set/get_priority, queue create/send/recv, critical enter/exit, mutex init. Uses setjmp/longjmp cooperative scheduling with SYSTIMER tick.

**Production default** (`CONFIG_REFLEX_KERNEL_SCHEDULER=y` in sdkconfig.defaults): the kernel owns interrupt context switching via `reflex_portasm.S` (`--wrap rtos_int_enter/exit`), scheduling policy via the 1Hz supervisor, and task priority modulation. FreeRTOS remains as the task management backend because ESP-IDF drivers (WiFi, USB-JTAG, ESP-NOW) create internal FreeRTOS tasks that require its API. The `reflex_task.h` abstraction isolates the substrate completely — no substrate code calls FreeRTOS directly.

## The GOOSE Substrate

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
- **`GOOSE_CELL_PURPOSE`:** a user-declared intent cell (`sys.purpose`) that tells the substrate what the machine is currently being used for. Purpose has two concrete effects: (1) `learn_sync` doubles Hebbian reward increments — the OS learns faster when it knows its purpose; (2) `weave_sync` biases autonomous fabrication toward sinks whose name contains the purpose domain as a segment (e.g., `purpose set sensor` preferentially routes needs to cells with `.sensor.` in their name, falling back to generic suffix matching when no domain match exists). The purpose name (max 15 chars) is persisted to NVS and restored on boot, so the substrate remembers what it was for across power cycles. Shell: `purpose set <name>/get/clear`. API: `goose_purpose_set_name`, `goose_purpose_get_name`, `goose_purpose_clear`.
- **Internal Temperature Sensor:** the first real perception cell. `perception.temp.reading` reads the C6 die temperature every 5 seconds via the ESP-IDF `temperature_sensor` driver and maps it to ternary state: cold (<40°C), normal (40–55°C), warm (>55°C). Shell: `temp`.
- **Tapestry Snapshots (Phase 29):** `goose_snapshot_save/load/clear` serialize and restore supervised-route plasticity (`learned_orientation` + `hebbian_counter`) to NVS. Save and load hold `loom_authority` for consistency with `learn_sync`. NVS keys use FNV-1a hashes of full field names to avoid truncation collisions.

### 7. Coherent Heartbeat (LP RISC-V)
A parallel heartbeat program running on the ESP32-C6 LP coprocessor (`components/goose/ulp/lp_pulse.c`). Loaded and started from `goose_lp_heartbeat_init` at boot; HP mirrors `agency.led.intent` state into `ulp_lp_led_intent` each supervisor pulse via `goose_lp_heartbeat_sync`. LP ticks at ~1 Hz via `ULP_LP_CORE_WAKEUP_SOURCE_LP_TIMER` and increments `ulp_lp_pulse_count`, which HP reads via the `heartbeat` shell command as a liveness indicator.

### 8. Loom Viewer (Substrate Visualization — Phase 30)
Real-time visualization of the GOOSE substrate via [Rerun.io](https://rerun.io). The architecture is push-based: the firmware emits structured telemetry lines (`#T:`-prefixed) on serial as state mutations occur. The host bridge (`tools/loom_viewer.py`) reads the serial stream, parses events, and logs them to Rerun for rendering.

- **Telemetry gate:** `goose_telemetry_enabled` (volatile bool, default false). The `TELEM_IF()` macro wraps every hook — cost when disabled is a single branch-not-taken (~2 cycles on RISC-V).
- **Delta compression:** hot-path hooks (10Hz route evaluation) only emit when state actually changes.
- **Event types:** cell state change (`C`), route sink write (`R`), Hebbian update (`H`), weave (`W`), alloc/evict (`A`/`E`), supervisor equilibrium (`B`), mesh arc (`M`), purpose (`P`), autonomous evaluation (`V`).
- **Rerun mapping:** cells as `GraphNodes` (color by type), routes as `GraphEdges` (color by coupling), Hebbian counters and system balance as `Scalars`, mesh events and lifecycle as `TextLog`.
- **Shell command:** `telemetry on/off` toggles emission. The Loom Viewer sends this automatically on connect.
- **Source:** `goose_telemetry.h` (gate macro + declarations), `goose_telemetry.c` (emitters), `tools/loom_viewer.py` (host bridge).

### 9. Metabolic Regulation (Phase 31)
Two-layer self-governance that modulates the substrate based on physical constraints.

- **Vital cells:** `perception.power.battery` (USB = permanent +1), `perception.mesh.health` (rx delta over 30s window), `perception.heap.pressure` (free heap thresholds). `perception.temp.reading` (existing) is also consumed.
- **Circuit breaker** (`sys.metabolic`): single cell, +1 thriving / 0 conserving / -1 surviving. Hard constraints (battery, heap) trigger surviving instantly. Recovery requires `REFLEX_METABOLIC_RECOVERY_TICKS` (30s) of sustained stability.
- **Surviving mode:** learning, fabrication, swarm sync, snapshots suspended. Only equilibrium check, evaluation, staleness, and watchdog run. Purpose-matching routes continue evaluating.
- **Resource governance (Layer 2):** per-vital, per-sub-pass. Mesh isolation *increases* discover frequency (inverted). Heap pressure blocks non-system shadow paging.
- **Testing:** `vitals override <vital> <state>` injects synthetic values. `vitals clear` resumes real hardware.
- **Source:** `goose_metabolic.h` / `goose_metabolic.c`. Telemetry: `#T:X,<metabolic>,<temp>,<batt>,<mesh>,<heap>`.

## Module Layout

## `components/goose/`
The core GOOSE implementation.
- `goose_runtime.c`: lattice hashing, cell allocation, route processing, `loom_authority` serialization, LP heartbeat bootstrap.
- `goose_supervisor.c`: equilibrium checking, harmonic rebalancing, autonomous fabrication (`weave_sync`), Hebbian plasticity (`learn_sync`), swarm decay, LP liveness monitor.
- `goose_atmosphere.c`: ESP-NOW atmospheric mesh — HMAC-SHA256 Aura, replay cache, protocol epoch, self-arc suppression, posture weight cap, mesh RX stats.
- `goose_atlas.c`: the Root Zone — 26 peripheral categories × 4 register channels = 104 pre-woven atlas cells.
- `goose_shadow_atlas.c`: SVD-generated shadow catalog of 12,738 MMIO nodes (registers + fields, including fully expanded array registers), paged in on demand via `goose_shadow_resolve`.
- `goose_telemetry.c`: push-based telemetry emitters for the Loom Viewer; `#T:`-prefixed serial lines gated by `goose_telemetry_enabled`.
- `goose_metabolic.c`: vital perception cells (battery, mesh, heap), circuit-breaker computation with hysteretic recovery, `vitals` shell data, override system for bench testing.
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
