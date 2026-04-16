# Implementation Status (v2.5.1)

## Summary

Reflex OS is a two-layer firmware for the XIAO ESP32C6:

- **GOOSE substrate** (`components/goose/`) — the geometric fabric, shadow-paged MMIO atlas, distributed atmospheric mesh, and supervisor. This is the "ternary-hosted" layer and is the project's load-bearing identity.
- **Ternary VM** (`vm/`) — region-checked memory, three-state soft cache, packed bytecode loader, background task runtime, and TASM-compiled programs.

Both layers build and have been revalidated on the XIAO ESP32C6 after the audit-remediation pass: clean cold boot, full Atlas weave, Geometric Arcing demo active, supervisor pulsing at 10Hz with no `LOOM_CONTENTION_FAULT` under nominal load.

## GOOSE Substrate

### G1 — Fabric, Loom, and Geometric Cells
- Source: `components/goose/goose_runtime.c`, `include/goose.h`
- 256-slot RTC-backed cell table with lattice-hash resolver (503 buckets)
- Cell types: VIRTUAL, HARDWARE_IN, HARDWARE_OUT, INTENT, SYSTEM_ONLY, PINNED, FIELD_PROXY, NEURON, NEED
- Cold-boot reset keyed on `fabric_magic` (0xF11BFABE) and wakeup cause
- Round-robin eviction of unpinned cells when the loom saturates
- Lock: `loom_authority` spinlock with timeout-and-skip semantics (see [`../SECURITY.md`](../SECURITY.md) §2)

### G2 — G.O.O.N.I.E.S. Naming Service
- Source: `components/goose/goose_runtime.c` (`goonies_register`, `goonies_resolve`, `goonies_resolve_cell`, `goonies_resolve_by_capability`)
- Hierarchical name registry (`sys.*`, `agency.*`, `perception.*`, `peer.*`)
- Immutability enforcement for protected zones against re-registration
- Shadow-atlas pre-check to prevent squatting on unpaged hardware names
- `peer.*` prefix triggers atmospheric discovery and allocates a ghost cell

### G3 — Atlas and Shadow Paging
- Source: `goose_atlas.c`, `goose_shadow_atlas.c` (SVD-generated from `tools/esp32c6.svd` via `tools/goose_scraper.py`, 9527 MMIO nodes)
- Boot-time weave projects 104 high-priority MMIO cells into the active Loom
- On-demand paging via `goonies_resolve_cell`: unregistered names fall through to shadow resolve, allocate a cell, bind agency
- **Full-surface name resolution**: `goonies find <name>` falls through from the live registry to `goose_shadow_resolve`, so every one of the 9527 catalog entries is addressable by name without pre-paging a cell. Live vs shadow hits are labeled in the shell output (`[live]` vs `[shadow]`).
- **`atlas verify`** shell command walks the entire 9527-entry catalog with a full round-trip equality check (name resolves, `addr` / `mask` / `type` all match, coord round-trips through `goose_make_shadow_coord`). Adjacent-pair duplicate sweep runs first to catch any scraper regression that would let binary search silently consolidate dupes. Validated on-device: `ok=9527/9527, duplicates=0, failures=0`.
- Sanctuary Guard: `is_sanctuary_address` restricts non-system mappings to a whitelist of peripheral bases.

The distinction between "catalog coverage" and "live Loom capacity" is load-bearing: the catalog holds 100% of the SVD-documented MMIO surface (not undocumented registers, eFuse bits outside the SVD schema, or silicon-revision deltas), while the live Loom is bounded to `GOOSE_FABRIC_MAX_CELLS=256` by the RTC SLOW memory budget. Name resolution does not require a cell allocation; only state read/write and route establishment do.

### G4 — Atmospheric Mesh (ESP-NOW)
- Source: `goose_atmosphere.c`
- Four arc ops: `SYNC` (state propagation), `QUERY` (discovery), `ADVERTISE` (response), `POSTURE` (swarm consensus)
- Aura MAC: HMAC-SHA256 over (op, coord, name_hash, state, nonce), truncated to 32 bits; rejected on RX mismatch
- Self-arc suppression via local MAC comparison
- 10Hz query throttle; ±100 accumulator saturation; ±10 hysteresis threshold for posture flip
- Self-loopback demo field (`manifest_demo_arc`) pinned in `main.c` after Wi-Fi service start

### G5 — Supervisor (Harmonic Regulation)
- Source: `goose_supervisor.c`
- 10Hz pulse: equilibrium check + rebalance across supervised fields
- 1Hz sub-passes (time-division from the 10Hz tick):
  - `weave_sync`: autonomous fabrication — wires `GOOSE_CELL_NEED` cells to capability-matched sinks
  - `learn_sync`: plasticity pass (currently random mutation under `sys.ai.pain` / `sys.ai.reward`; see notes)
  - `swarm_sync`: decays `swarm_accumulator` toward zero when no posture traffic
- Recursive field processing up to depth 3 for `GOOSE_CELL_FIELD_PROXY` and `GOOSE_CELL_NEURON` sub-fields

### G6 — LoomScript Binary Loader
- Source: `goose_library.c`, `include/goose.h` (`loom_header_t`, `goose_weave_loom`)
- Magic: `LOOM` (0x4D4F4F4C); packed cell/route/transition entries
- Example fragments: `examples/button_blink.loom`, `examples/self_heal.loom`
- Compiler: `tools/loomc.py`

### G7 — Gateway, ETM, DMA Bridges
- Source: `goose_gateway.c` (legacy message bridge), `goose_etm.c` (event-task-matrix scaffold), `goose_dma.c` (GDMA route manifestation)
- Gateway wires fabric traffic to/from legacy message APIs
- ETM and DMA remain scaffold-level; the runtime exports the APIs but the routes are not yet exercised by boot-time manifests

## Ternary VM

### S001 — Ternary Message Fabric
- Source: `core/fabric.c`, `core/include/reflex_fabric.h`
- Trit-indexed QoS channels (Critical, System, Telem)
- Fabric-native Button HAL (GPIO 9) and LED service

### S002 — Ternary Shared Memory (MMU-style region check)
- Source: `vm/mmu.c`, `vm/include/reflex_vm_mem.h`
- Region-based translation with overlap protection
- Memory-safe boundary enforcement in the interpreter
- Note: this is a protection layer, not a translating MMU; see README for framing

### S003 — Soft-Cache and Coherency Proxy
- Source: `vm/cache.c`, `vm/include/reflex_cache.h`
- Direct-mapped 16-set cache for `word18` values
- Three-state (I/E/M) coherency — simplified MESI without Shared
- `reflex_vm_host_write` invalidates on host-side mutation
- Performance metrics visible via `vm info`

### T007v2 — Packed Bytecode Loader
- Source: `vm/loader.c`, [`vm/loader-v2.md`](vm/loader-v2.md), `tools/tasm.py`
- 32-bit dense instruction packing
- CRC32 checksum verification
- Packed data-segment unpacking into VM private memory
- Hex-loading path available via `vm loadhex` shell command

### Ternary Supervisor v1.0
- Source: `examples/supervisor.tasm`, assembled to `.rfxv`/`.hex` locally (artifacts gitignored)
- Background ternary program managing fabric → LED path
- `vm loadhex` and `vm task start` target the same loaded image
- Validated on device by injecting a message to VM node 7 and observing LED state change

## Hardware-Validated Behaviors

- **Physical interaction**: button presses publish messages to the fabric; VM task receives and toggles LED via the supervisor path
- **Cold boot integrity**: cold-boot path fully exercised, seeds origin cells, Atlas weave completes with 104 nodes projected
- **Atmospheric arcing**: Geometric Arcing demo runs post-Wi-Fi bring-up; ghost cell (-1, 0, 1) present in goonies listing; HMAC-SHA256 Aura verified on RX with replay cache
- **Supervisor pulse**: 10Hz tick sustained over stability window with no contention faults
- **Lock safety**: `goose_loom_try_lock` skips pulse on timeout rather than force-breaking
- **Cache coherency**: VM and host maintain coherent view via `reflex_vm_host_write` proxy
- **Binary integrity**: loader rejects truncated or corrupted hex strings via CRC32
- **Task integrity**: `vm task start` launches the same binary previously loaded via `vm loadhex`
- **Supervisor routing**: `TSEND` uses the node id stored in the destination register, matching TASM supervisor code and on-device behavior
- **System reliability**: 10s stability window + boot-loop detection + safe-mode fallback on panic-count threshold
- **Aura provisioning**: `aura setkey <hex>` persists a 16-byte HMAC key to NVS with boot-time reload
- **LP core Coherent Heartbeat**: ULP RISC-V LP core runs a 1 Hz counter loop in parallel to HP; `heartbeat` shell command exposes `lp_pulse_count`; HP mirrors `agency.led.intent` into `ulp_lp_led_intent` each supervisor pulse
- **Hebbian plasticity**: reward-gated co-activation counter per route commits to `learned_orientation` at threshold; pain signal decays counters toward zero
- **NEURON quorum**: `GOOSE_CELL_NEURON` cells aggregate all sub-field routes via ternary sum-and-majority via `neuron_quorum`
- **Full-surface MMIO name resolution**: the shell's `goonies find` falls through from the live registry to `goose_shadow_resolve` so every one of the 9527 SVD-documented entries is addressable by name without pre-paging a cell
- **Atlas verify**: `atlas verify` walks the full catalog with a complete round-trip (name / addr / mask / type / coord) plus an adjacent-pair duplicate sweep; validated on Alpha with `ok=9527/9527, duplicates=0, failures=0`
- **Three-board mesh field trial**: cross-board `ARC_OP_SYNC` propagation at ~5 Hz (317+ packets across two peers), `goonies find <peer-name>` triggers `ARC_OP_QUERY` → `ARC_OP_ADVERTISE` round-trip with `Ghost Solidified` log confirmation, `mesh posture <state> <weight>` crosses the `SWARM_THRESHOLD=10` hysteresis with three cooperating peers, and HMAC Aura rejection is observable (`aura_fail` counter climbs at the offending peer's emit rate after a deliberate key mismatch, then freezes after re-pairing)
- **Internal temperature sensor**: `perception.temp.reading` reads the C6 die at ~50-55°C, projecting live ternary state (cold/normal/warm) into the fabric; `temp` shell command reports raw celsius + state
- **`GOOSE_CELL_PURPOSE`**: `purpose set/get/clear` creates, queries, and clears a user-declared intent cell; learn_sync observably doubles Hebbian reward increments when active
- **Tapestry Snapshots**: `snapshot save/load/clear` exercised on Alpha with NVS read/write paths returning `ESP_OK`; 0 routes persisted (correct — no active plasticity scenario at boot without external stimulus)

## Current Developer Flow

1. **Code**: write `.tasm` or `.ls` files
2. **Compile**: `python3 tools/tasm.py program.tasm program.rfxv` or `python3 tools/loomc.py program.ls program.loom`
3. **Deploy**: `vm loadhex <HEX>` in the Reflex shell, or `vm load` for the built-in sample image
4. **Execute**: `vm task start` for background or `vm run` for foreground

## Known Gaps (docs lead, code trails)

Closed in the current remediation sweep:

- ~~Plasticity rule~~: replaced with reward-gated co-activation Hebbian in `goose_supervisor_learn_sync` (Phase 3).
- ~~NEURON aggregation~~: multi-route ternary sum-and-threshold in `internal_process_transitions` via `neuron_quorum` (Phase 2).
- ~~Autonomous Fabrication hardcoded suffix~~: generic last-segment capability matcher in `goose_supervisor_weave_sync` (Phase 2).
- ~~Replay protection on Atmosphere~~: 16-slot replay cache in `atmosphere_recv_cb` (Phase 1).
- ~~Aura key material~~: NVS-backed key with `aura setkey <hex>` shell command; compile-time default retained as unprovisioned fallback (Phase 1).
- ~~"MMU-backed memory" framing~~: README narrowed to "region-protected shared ternary memory" to match `vm/mmu.c` (Phase 4).
- ~~LP-core "Coherent Heartbeat"~~: ULP enabled (`CONFIG_ULP_COPROC_TYPE_LP_CORE=y`), LP program rewritten to LP-local globals, HP bootstrap via `ulp_lp_core_load_binary`/`ulp_lp_core_run` with 1 Hz LP_TIMER wakeup, `heartbeat` shell command reads `lp_pulse_count` (Phase 5).

Remaining (honest limits, not regressions):

- **Aura wire size**: HMAC-SHA256 truncated to 32 bits for protocol compatibility; collision resistance capped at the birthday bound (~2^16). Future protocol epoch can bump `GOOSE_ARC_VERSION` and expand the Aura field.
- **Aura key storage**: first-boot auto-provisioning generates a unique random 16-byte key via `esp_fill_random()` and persists it to NVS under `goose/aura_key`, so factory-fresh boards do not share a key (commit `afbfddc`). The remaining honest limit is that the key is still extractable from flash or JTAG by anyone with physical access to the board. Derive-from-efuse with secure boot remains a follow-up.
- **LP heartbeat does not drive the LED**: the onboard LED is on GPIO 15, a HP-only pin; the C6 LP core can only drive LP I/O (GPIO 0-7). The LP heartbeat is therefore a parallel counter + intent-mirror, not an LED driver. Driving the LED from the LP core would require external wiring to an LP I/O pin.
- **Catalog ≠ silicon**: `atlas verify` confirms 100% of the *SVD-documented* MMIO surface resolves cleanly, but the SVD is Espressif's published schema — not the full silicon. Undocumented registers, eFuse bits outside the SVD field schema, and silicon-revision deltas newer than `tools/esp32c6.svd` are not covered.

## Next Steps

Items 1–5 from the previous TODO all shipped in the current session:

- ~~v2.6.0 release tag~~ — tagged as `v2.6.0` at `61deaad`.
- ~~Session essay~~ — published at `docs/essay-substrate-as-interface.md`.
- ~~`GOOSE_CELL_PURPOSE`~~ — first slice shipped: `purpose set/get/clear` + Hebbian amplification.
- ~~Phase 29 Tapestry Snapshots~~ — `snapshot save/load/clear` + NVS serialization + loom_authority locking.
- ~~First real sensor~~ — internal temperature sensor as `perception.temp.reading`.

Near-term TODO (forward from the current state):

1. **Purpose-modulated capability matching in `weave_sync`** — when a PURPOSE cell is active, bias autonomous fabrication toward routes that serve the declared purpose's domain. Current first-slice is a bare 2× Hebbian amplifier; next slice gives purpose a routing effect.
2. **Purpose name persistence** — store the purpose name (not just the active/inactive bit) in NVS or as a goose cell attribute so `purpose get` can report WHAT the purpose is, not just that one exists.
3. **External I2C sensor on the fabric** — expose a BME280 (or similar I2C peripheral) as addressable GOOSE cells under `perception.i2c0.bme280.*`. The internal temp sensor proves the architecture; an external sensor proves the extensibility.
4. **Automatic snapshot cadence** — call `goose_snapshot_save` on a supervisor-driven timer (e.g., every 60 seconds after the stability marker) rather than requiring an explicit `snapshot save` from the shell.
5. **Multi-VM contexts** — support concurrent execution of multiple ternary task images.
6. **Aura key derivation from efuse** + secure-boot integration.
