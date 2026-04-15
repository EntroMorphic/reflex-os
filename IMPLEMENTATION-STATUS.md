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
- Lock: `loom_authority` spinlock with timeout-and-skip semantics (see SECURITY.md §2)

### G2 — G.O.O.N.I.E.S. Naming Service
- Source: `components/goose/goose_runtime.c` (`goonies_register`, `goonies_resolve`, `goonies_resolve_cell`, `goonies_resolve_by_capability`)
- Hierarchical name registry (`sys.*`, `agency.*`, `perception.*`, `peer.*`)
- Immutability enforcement for protected zones against re-registration
- Shadow-atlas pre-check to prevent squatting on unpaged hardware names
- `peer.*` prefix triggers atmospheric discovery and allocates a ghost cell

### G3 — Atlas and Shadow Paging
- Source: `goose_atlas.c`, `goose_shadow_atlas.c` (SVD-generated, 9k+ MMIO nodes)
- Boot-time weave projects 104 high-priority MMIO cells into the active loom
- On-demand paging via `goonies_resolve_cell`: unregistered names fall through to shadow resolve, allocate a cell, bind agency
- Sanctuary Guard: `is_sanctuary_address` restricts non-system mappings to a whitelist of peripheral bases

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
- Source: `vm/loader.c`, `VM-LOADER-V2.md`, `tasm.py`
- 32-bit dense instruction packing
- CRC32 checksum verification
- Packed data-segment unpacking into VM private memory
- Hex-loading path available via `vm loadhex` shell command

### Ternary Supervisor v1.0
- Source: `supervisor.tasm`, `supervisor.hex`
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

## Current Developer Flow

1. **Code**: write `.tasm` or `.ls` files
2. **Compile**: `python3 tasm.py program.tasm program.rfxv` or `python3 tools/loomc.py program.ls program.loom`
3. **Deploy**: `vm loadhex <HEX>` in the Reflex shell, or `vm load` for the built-in sample image
4. **Execute**: `vm task start` for background or `vm run` for foreground

## Known Gaps (docs lead, code trails)

Closed in the current remediation sweep:

- ~~Plasticity rule~~: replaced with reward-gated co-activation Hebbian in `goose_supervisor_learn_sync` (Phase 3).
- ~~NEURON aggregation~~: multi-route ternary sum-and-threshold in `internal_process_transitions` via `neuron_quorum` (Phase 2).
- ~~Autonomous Fabrication hardcoded suffix~~: generic last-segment capability matcher in `goose_supervisor_weave_sync` (Phase 2).
- ~~Replay protection on Atmosphere~~: 16-slot replay cache in `atmosphere_recv_cb` (Phase 1).
- ~~Aura key material~~: NVS-backed key with `aura setkey <hex>` shell command; compile-time default retained as unprovisioned fallback (Phase 1).

Remaining:

- **"MMU-backed memory" framing**: `vm/mmu.c` is a region-and-bounds checker, not a translating MMU. Docs narrowed to "region-protected shared ternary memory" in Phase 4. No code change pending.
- **LP-core "Coherent Heartbeat"**: `components/goose/ulp/lp_pulse.c` addressed in Phase 5 — LP rewritten to ULP-local globals, HP bootstrap added, sync hook in supervisor pulse.
- **Aura wire size**: HMAC-SHA256 truncated to 32 bits for wire compatibility; collision resistance capped at the birthday bound (~2^16). Future protocol epoch could expand to 64+ bits.
- **Aura key provisioning**: factory-fresh boards share the compile-time default until an operator runs `aura setkey`. Derive-from-efuse remains a follow-up.

## Next Steps

1. **Wave 2 (mechanical)**: NEURON quorum aggregation; generic capability matching in autonomous fabrication
2. **Wave 3 (design call required)**: plasticity rule selection (Hebbian / reward-gated / richer); MMU definition (region-check vs translating); eager vs on-demand projection of the 9k-node shadow atlas
3. **Atmosphere hardening**: replay cache; derived key material; optional expansion of wire Aura beyond 32 bits
4. **Advanced I/O**: expose I2C and SPI buses to the Ternary Fabric
5. **Multi-VM contexts**: support concurrent execution of multiple ternary task images
