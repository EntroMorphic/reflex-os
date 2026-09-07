# Implementation Status (v2.6.0)

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
- Source: `goose_atlas.c`, `goose_shadow_atlas.c` (SVD-generated from `tools/esp32c6.svd` via `tools/goose_scraper.py`, 12738 MMIO nodes)
- Boot-time weave projects 104 high-priority MMIO cells into the active Loom
- On-demand paging via `goonies_resolve_cell`: unregistered names fall through to shadow resolve, allocate a cell, bind agency
- **Full-surface name resolution**: `goonies find <name>` falls through from the live registry to `goose_shadow_resolve`, so every one of the 12738 catalog entries is addressable by name without pre-paging a cell. Live vs shadow hits are labeled in the shell output (`[live]` vs `[shadow]`).
- **`atlas verify`** shell command walks the entire 12738-entry catalog with a full round-trip equality check (name resolves, `addr` / `mask` / `type` all match, coord round-trips through `goose_make_shadow_coord`). Adjacent-pair duplicate sweep runs first to catch any scraper regression that would let binary search silently consolidate dupes. Validated on-device: `ok=12738/12738, duplicates=0, failures=0`.
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

### G6a — LoomScript Upload (TASM upload path)
- Source: `loom load <hex>` in `shell/shell.c` -> `goose_weave_loom`
- Implements `prd-tasm-upload.md`. Until this landed `goose_weave_loom` had **no caller at all** and the linker discarded it, so the parser hardening from the 2026-08-12 audit had been verified by inspection only.
- Admin-gated: weaving a fragment introduces routes and starts a pulse task from operator-supplied bytes, the same privilege class as `vm loadhex`. The command table gates whole commands, so the subcommand checks the session role directly.
- `loom fragments` reports the active count. Fragments are reboot-scoped; there is no unweave path.
- Hardware-validated rejection paths, each exercising a specific guard: bad magic (`0x10A`), header with no body (`0x104`), `cell_count` over cap (`0x104`), route index out of range (explicit Security Violation naming `src=9999 cells=1`), and unresolvable cell name (`0x105`). Board healthy afterward with `atlas verify` still 12738/12738.

### G6 — LoomScript Binary Loader
- Source: `goose_library.c`, `include/goose.h` (`loom_header_t`, `goose_weave_loom`)
- Magic: `LOOM` (0x4D4F4F4C); packed cell/route/transition entries
- Example fragments: `examples/button_blink.loom`, `examples/self_heal.loom`
- Compiler: `tools/loomc.py`

### G12 — Eviction Soak (2026-08-12)
- Making shadow-paged cells evictable activated a path that had previously never run (`evictions=0` before the fix), so it was soaked before being trusted.
- Setup: both C6s paired and purpose-active (learning and exploration running), continuous `bonsai bloat` paging churn on board A, mesh traffic between them. 27 cycles over 921s.
- **7,977 evictions**, `resolved=300/300` on every single cycle — paging never degraded, unlike the pre-fix behaviour where it stalled permanently after the first pass.
- **Free heap perfectly flat**: 188,412 bytes free and 185,820 minimum-ever, unchanged across all 27 cycles. No leak from the eviction path.
- Zero anomalies: no panics, no `LOOM_CONTENTION_FAULT`, no unexpected resets.
- Post-soak integrity intact: `atlas verify` 12738/12738 duplicates=0 failures=0; all seven seeded cells (`sys.origin`, `sys.purpose`, `sys.metabolic`, `sys.swarm.posture`, `sys.kernel.disposition`, `agency.led.intent`, `perception.heap.pressure`) still resolve; LED still responds.
- Observation, not a defect: the peer's `perception.mesh.health` read *connected* in the first cycle and *sparse* thereafter. Sustained local paging load makes discovery beacons less regular, so a peer legitimately observes thinner traffic — the vital reporting what is actually on the air. The inverted discovery governance responds by hunting more often, which is the intended reaction.
- `status` now reports `heap free=/min_free=` and `cells=`, added for this soak: `perception.heap.pressure` carries only a trit against 8K/16K thresholds, so a slow leak would have stayed invisible until it was already critical.

### G11 — Ternary Task Disposition (kernel policy layer)
- Source: `goose_kernel_policy_tick` in `goose_supervisor.c`; `kernel` shell command
- The substrate decides a **ternary stance** per supervised field before any integer priority exists:
  - `+1 engaged` — the field serves the declared purpose; give it headroom
  - ` 0 latent` — no commitment; the substrate has not decided this matters
  - `-1 withheld` — deliberately held back (pain, or a declared purpose that every containing holon sits outside)
- Priority handed to the host RTOS is **derived from** the stance, never the source of truth. The integer is a lossy projection of a ternary decision onto a binary scheduler the OS does not own — an honest description of what Reflex OS is today.
- `latent` is the load-bearing value. A field running with no purpose declared is *undecided*, which is a different claim from one being suppressed; a single priority number collapses the two.
- Aggregate published as routable substrate state at `sys.kernel.disposition` (0,0,5), not merely an internal variable.
- Observable via `kernel`: per-field stance plus the aggregate.
- Hardware-validated: no purpose → all latent; `purpose set led` → led_agency engaged; `purpose set mesh` → led_agency withheld via agency-holon deactivation; `purpose clear` → latent again.
- Note: this made the holon lifecycle real. `reflex_holon_add_field` had been called exactly once (for `autonomy`, whose empty domain makes it permanently active), so the `agency` and `comm` holons had no members and could never deactivate anything. `led_service` now joins `agency`.

### G9 — Metabolic Regulation (Phase 31)
- Source: `goose_metabolic.c`, `include/goose_metabolic.h`
- Two-layer self-governance: circuit breaker (aggregate, instant degradation, hysteretic recovery) + resource governance (per-vital, per-sub-pass).
- Vital cells: `perception.power.battery` (USB default +1), `perception.mesh.health` (rx delta, 30s window), `perception.heap.pressure` (free heap thresholds 8K/16K). Reads existing `perception.temp.reading`.
- Circuit breaker (`sys.metabolic`): +1 thriving / 0 conserving / -1 surviving. Hard constraints (battery, heap) trigger surviving instantly. Mesh excluded from circuit breaker (connectivity issue, not resource constraint — handled by discover governance). Recovery requires 30s sustained stability.
- Surviving mode: learning, fabrication, swarm sync, snapshots suspended. Equilibrium, evaluation, staleness, watchdog always run.
- Resource governance: mesh isolation increases discover frequency (inverted logic). Heap pressure blocks non-system shadow paging. Conserving mode halves learning rate.
- Testing: `vitals override <vital> <state>` injects synthetic values. `vitals clear` resumes real hardware.
- Telemetry: `#T:X,<metabolic>,<temp>,<batt>,<mesh>,<heap>` at 1Hz.
- Hardware-validated: default thriving on USB, temp override → conserving, battery override → surviving, learning/weave suspended in surviving, hysteresis holds on recovery, all cells visible on fabric, system stable throughout.

### G8 — Streaming Telemetry (Loom Viewer — Phase 30)
- Source: `goose_telemetry.c`, `include/goose_telemetry.h`, `tools/loom_viewer.py`
- Push-based telemetry: firmware emits `#T:`-prefixed lines on serial as state mutations occur. No polling.
- Gate: `goose_telemetry_enabled` volatile bool, toggled via `telemetry on/off` shell command. Zero cost when disabled (~2 cycles per hook site).
- Deferred emission: hot-path hooks (10Hz route evaluation, 1Hz Hebbian learning) snapshot state inside `loom_authority` and emit after unlock.
- Output: direct USB JTAG register writes via `reflex_hal_write_raw`, bypassing stdio buffering so all task contexts produce output.
- 10 event types: cell state change (`C`), route sink write (`R`), Hebbian update (`H`), weave (`W`), alloc/evict (`A`/`E`), supervisor equilibrium (`B`), mesh arc (`M`), purpose (`P`), autonomous evaluation (`V`).
- Host bridge: `tools/loom_viewer.py` reads serial in a background thread, parses events, and logs to Rerun.io for real-time visualization (GraphNodes/GraphEdges for topology, Scalars for time series, TextLog for events).
- Hardware-validated: 10Hz balance stream (50 lines in 5s), 1Hz eval events, purpose/alloc events from shell context, clean disable with zero stray lines, 10-second soak with zero malformed lines, shell commands work while telemetry streams, 10 rapid on/off toggles with no crash.

### G10 — Self-Expanding Perception (Phase 33: Curiosity Attractor)
- Source: `goose_supervisor_explore()` in `goose_supervisor.c`
- The OS is curious when purpose is active. A two-phase probe reads HARDWARE_IN shadow atlas registers 1 second apart. Registers whose values changed are **hot** — something is alive — and get paged into the Loom.
- Three layers: curiosity (probing) finds what's alive, Hebbian learning finds what's relevant, eviction forgets what's not. Timer noise (uncorrelated with purpose) never commits under Hebbian learning.
- Pain amplifies curiosity: under zero progress (5+ ticks), probe count doubles from 4 to 8 per pulse.
- Metabolic-gated: suspended in surviving, halved in conserving. Explicit heap guard.
- Cap: `REFLEX_EXPLORE_MAX_ACTIVE` (30) per purpose cycle. Resets on purpose change.
- Telemetry: `#T:D,<name>`. Shell: `status` shows `explore: curious/urgent/idle`.
- Hardware-validated: 9/9 on-device tests pass. Board discovered `perception.apb_saradc.apb_tsens_ctrl` and 29 other registers autonomously. Cap respected. Metabolic gating confirmed. Telemetry streaming.

### G11 — Role-Based Access (RBA)
- Source: `shell/shell.c` (dispatch table, subcmd_min_role, auth handler)
- Four roles: observer (read-only), agent (AI guardrail), operator (day-to-day), admin (everything).
- Every command has a `min_role` in the dispatch table. Commands with mixed sub-commands (config get/set, purpose get/set, led on/off/status, mesh query/peer add, vitals/vitals override, telemetry on/off, snapshot save/clear, vm info/loadhex) use `subcmd_min_role()` for fine-grained escalation.
- Voluntary capability declaration: `auth role <role>` restricts the session. Default: admin (backward compatible). No PINs — serial cable is the trust boundary. PIN auth deferred to remote-shell phase.
- Python SDK: `ReflexNode(port, role="agent")` sends `auth role` on connect. Commands exceeding the role raise `AccessDenied(PermissionError)`.
- Telemetry: `#T:U,<role>` on role transitions.
- Hardware-validated: 15/15 on-device tests pass. Observer correctly denied mutations (led on, reboot, purpose set, config set, vitals override, vm loadhex, telemetry on, mesh query). Observer allowed reads (status, temp, goonies, led status, config get, purpose get, mesh peer ls, vitals display). Agent/operator/admin escalation verified.

### G7 — Gateway, ETM, DMA Bridges
- Source: `goose_etm.c` (event-task-matrix scaffold), `goose_dma.c` (GDMA route manifestation). `goose_gateway.c` was removed 2026-08-12: it polled `REFLEX_NODE_GATEWAY` for `op == 0x10` messages that nothing in the tree ever sent, burning a 4096-byte task stack at 10Hz since the project moved to direct `goonies_resolve_cell` paths. Flagged in audit-2026-04-16 §5c.
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

- **Loom peak hold is eviction-driven, not boot-driven** (2026-09-07, C6). `status` now carries the site and timestamp of the peak. Fresh boot `max_us=321@0.082s(alloc)`, held flat across a full 171-check `hw-test` run (6,757 holds). Three `bonsai bloat` passes then drove it to `568@75.712s(alloc)` at 163 evictions and `1534@83.042s(alloc)` at 463, unchanged by the third pass at 763. The site is `alloc` in every case.
- **The 2026-09-07 P0 remediation, on three boards.** Firmware built clean for both targets (esp32c6 1.74MB, esp32 903KB, zero warnings) at commit `006b8e2`, flashed to two ESP32-C6s and one classic dual-core ESP32 ("the V3"). `make hw-test`: **171/171** on each C6, **170/171** on the V3 (the single failure is a test-portability issue, not firmware — see Known Gaps). Six targeted P0 probes pass on all three: a TSYS selector of `0x10003` is refused at load while `DELAY` still loads (P0-H1); a `.loom` fragment is refused for `GOOSE_COUPLING_RADIO`, for an `orientation` of 7, and for a non-zero `trans_count`, while the SOFTWARE fragment `loomc.py` actually emits is still accepted (P0-H2). **P0-H3 was written specifically for dual-core and this is its first run on dual-core silicon.**
- **QUERY egress throttle, measured live (P0-L3).** Two `mesh query` commands 50ms apart: the first reports `rc=0x0` / `#R:+1,ok`, the second `throttled (10Hz egress limit)` / `#R:-1,guard`. `mesh stat` then reads `tx_query=1 query_throttled=1` — the suppressed query is counted but never reaches the radio, confirming the gate sits ahead of the transmit counter.
- **`loom evictions` reads the previously dead eviction ring**: after two `bonsai bloat` passes the window reports `total=461 recent=8 distinct=8` with every victim in the `logic.*` shadow namespace — confirming directly that only shadow-paged cells are ever chosen and seeds never are. The empty state reports `total=0 (none yet)`. The anomaly branch was verified by mutation rather than left unexercised: forcing the ring to record a constant name produced `distinct=1` and the expected `ANOMALY` line, then the mutation was reverted.
- **Indexed registry under sustained paging churn**: three consecutive `bonsai bloat` passes (763 evictions, fabric saturated at 256 cells) hold peak loom time to 637us and average to 14us, against 1640us/93us measured on the same board at the same churn immediately before the change. `atlas verify` 12738/12738, 300/300 resolved per pass, heap flat (172096 free / 169532 min). Long-name registration is idempotent: repeated `goonies find agency.gpio.func100_in_sel_cfg.in_inv_sel` grows the registry by one, not one per call. Mesh unaffected: two paired C6s discover each other, `mesh query` completes a QUERY -> ADVERTISE round trip with `Ghost Solidified`, and `goonies find peer.charlie.led` resolves the phantom with `peer_id=1`.
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
- **Full-surface MMIO name resolution**: the shell's `goonies find` falls through from the live registry to `goose_shadow_resolve` so every one of the 12738 SVD-documented entries is addressable by name without pre-paging a cell
- **Atlas verify**: `atlas verify` walks the full catalog with a complete round-trip (name / addr / mask / type / coord) plus an adjacent-pair duplicate sweep; validated on Alpha with `ok=12738/12738, duplicates=0, failures=0`
- **Three-board mesh field trial**: cross-board `ARC_OP_SYNC` propagation at ~5 Hz (317+ packets across two peers), `goonies find <peer-name>` triggers `ARC_OP_QUERY` → `ARC_OP_ADVERTISE` round-trip with `Ghost Solidified` log confirmation, `mesh posture <state> <weight>` crosses the `SWARM_THRESHOLD=10` hysteresis with three cooperating peers, and HMAC Aura rejection is observable (`aura_fail` counter climbs at the offending peer's emit rate after a deliberate key mismatch, then freezes after re-pairing)
- **Internal temperature sensor**: `perception.temp.reading` reads the C6 die at ~50-55°C, projecting live ternary state (cold/normal/warm) into the fabric; `temp` shell command reports raw celsius + state
- **`GOOSE_CELL_PURPOSE`**: `purpose set/get/clear` creates, queries, and clears a user-declared intent cell; learn_sync observably doubles Hebbian reward increments when active
- **Purpose name persistence**: `purpose set sensor` persists `"sensor"` to NVS; reboot restores both the name and the active cell (`purpose restored from NVS: "sensor"` in boot log); `purpose get` reports the name; `purpose clear` + reboot → `inactive`
- **Purpose-modulated routing**: `weave_sync` uses segment-bounded domain matching (`.<purpose>.` or trailing `.<purpose>`) to bias capability resolution when a purpose is active, falling back to generic suffix match when no domain candidate exists
- **Tapestry Snapshots**: `snapshot save/load/clear` exercised on Alpha with NVS read/write paths returning `ESP_OK`; 0 routes persisted (correct — no active plasticity scenario at boot without external stimulus)
- **Self-Expanding Perception**: `purpose set led` triggers curiosity probing; board discovered `perception.apb_saradc.apb_tsens_ctrl` and 29 other registers autonomously; 30-cell cap respected; metabolic gating confirmed (surviving blocks exploration); telemetry `#T:D` events streaming; 9/9 on-device tests pass
- **HARDWARE_IN live sampling**: route evaluation reads live GPIO pins (`reflex_hal_gpio_get_level`) and MMIO registers (volatile pointer + bit_mask) before using source cell state
- **`goonies read`**: reads any named MMIO register (live cell, shadow entry, or GPIO pin) with raw hex, masked hex, and ternary interpretation
- **Role-Based Access**: `auth role observer` → `reboot` prints `denied: requires admin`; `auth role agent` → `purpose set led` works, `reboot` denied; observer can read (status, goonies, temp, config get, led status, vitals, mesh peer ls) but not mutate (led on, purpose set, config set, telemetry on, mesh query, vitals override); 15/15 on-device tests pass
- **Metabolic Regulation (Phase 31)**: `vitals` shows temp=0, battery=1, mesh=-1, heap=1, metabolic=thriving on USB-powered board. `vitals override temp -1` → conserving within 2s. `vitals override battery -1` → surviving (hard constraint). In surviving: zero `#T:H` and `#T:W` events (learning+weave suspended). `vitals clear` → hysteresis holds (stays degraded for recovery window). `#T:X,1,0,1,-1,1` streams at 1Hz. All vital cells visible in `goonies ls`. 10/10 on-device tests.
- **Streaming telemetry (Phase 30)**: `telemetry on` produces `#T:B,1` at exactly 10Hz (50 lines in 5s) and `#T:V,0,0` at 1Hz. `telemetry off` produces zero stray lines. `#T:P,<name>` and `#T:A,<name>,<type>` fire from shell context on `purpose set`. 10-second soak: B=100, V=10, 0 malformed. Shell commands (`status`, `goonies ls`, `heartbeat`) work while telemetry streams. 10 rapid on/off toggles stable.

## Current Developer Flow

1. **Code**: write `.tasm` or `.ls` files
2. **Compile**: `python3 tools/tasm.py program.tasm program.rfxv` or `python3 tools/loomc.py program.ls program.loom`
3. **Deploy**: `vm loadhex <HEX>` in the Reflex shell, or `vm load` for the built-in sample image
4. **Execute**: `vm task start` for background or `vm run` for foreground

## Known Gaps (docs lead, code trails)

### ESP-IDF independence

**Status lives in [`independence-dependency-map.md`](independence-dependency-map.md)**, the single source of truth, and is measured rather than asserted: `make independence` reports the current surface by tier and `make independence-check` ratchets it in CI. Do not restate tier status here — that is exactly how the map drifted three ways before 2026-09-07.

Summary at `641263b`: **23 ESP-IDF includes remain on the independence path** (C6 + 802.15.4). Tier A is clear. None of the 23 is in the substrate — all sit in platform backends, the kernel's FreeRTOS shims, or the shell's peripheral and console drivers. Tier F (build system, startup, heap, linker/memory layout, image format) is untouched and is what actually decides independence: FreeRTOS cannot leave the image while newlib pulls in `esp_timer` and `pthread`, regardless of who schedules.


### Found during hardware validation, 2026-09-07 — all three closed

- ~~`tests/hardware/validate_shell.py` is not target-aware~~: the suite now reads the catalog size from `atlas verify` (`ok=N/M`) and tells three states apart where it previously collapsed two — the probe resolves (assert the Sanctuary Guard), the catalog exists but this name went stale (TEST PROBLEM, pick another name), or this target has no catalog at all (skip). The third is real: `components/goose/CMakeLists.txt` deliberately substitutes `goose_shadow_atlas_stub.c` off-C6 so other targets do not claim C6 hardware knowledge, so `notfound` was the honest answer and the failure blamed the firmware for the test's assumption. `Results.skip` is counted and printed, never silent. **Verified on hardware: ESP32 `170 passed, 0 failed, 1 skipped` exit 0; C6 still asserts `goonies read agency.io_mux.date` -> `#R:-1,guard` at `171 passed, 0 failed`.**
- ~~Peak loom hold exceeds the timeout budget, unexplained~~: the peak now records **where** and **when**, because a bare maximum cannot distinguish a one-off from a steady state. `status` reads `max_us=1534@83.042s(alloc)`. That immediately settled it, and disproved the boot-time hypothesis in the previous entry: the site is always `alloc` (`fabric_alloc_internal`) and the peak tracks **eviction pressure**, not uptime. Measured on one C6: fresh boot `321@0.082s`, after one `bonsai bloat` pass (163 evictions) `568@75.712s`, after two (463 evictions) `1534@83.042s`, unchanged by a third. The original 3x spread between two identical C6s was workload, not boot ordering — one had run `hw-test`, the other only the P0 probe.
- ~~`CONFIG_REFLEX_KERNEL_SCHEDULER` fails at link on esp32~~: the symbol now carries `depends on IDF_TARGET_ESP32C6`, so the combination is refused at configure instead of 200 objects later. **Verified: the exact previously-failing invocation (`set-target esp32` with no `SDKCONFIG_DEFAULTS`) now leaves the symbol absent from sdkconfig despite `sdkconfig.defaults` setting it `=y`, and builds with 0 undefined references. The C6 keeps it `=y`.**

**Open, and newly surfaced by that instrumentation:** at fabric saturation the `alloc` path holds `loom_authority` for up to **1534us** — five times `LOOM_LOCK_TIMEOUT_US` (300us), so any concurrent `try_lock` in that window times out and defers. The eviction scan runs inside the hold. This also sits against the 637us peak recorded in the indexed-registry entry below, measured on the same board at the same churn (three `bonsai bloat` passes, 763 evictions); 1534us is 2.4x that figure and the difference is not yet explained. Not a correctness defect — no contention faults were logged and the deferral path is the designed response — but the budget the lock discipline is reasoned against does not hold under eviction pressure, and now that the site is named this is measurable rather than mysterious.

Closed in the 2026-09-07 P0 audit remediation (see [`P0-07SEP26.md`](P0-07SEP26.md)):

- ~~P0-H1 VM loader syscall selector truncation~~: `reflex_vm_loader_syscall_valid` took `int16_t` while `reflex_vm_instruction_t::imm` is `int32_t`, so the selector was validated at 16 bits and dispatched at 17 — the same defect class already fixed for branch targets, two checks away. `0x10003` truncated to `SYSCALL_DELAY` and passed. Now `int32_t`, with both truncation directions regression-pinned in `tests/host/test_vm_regress.c`. The audit's second claim (that wire selectors >= 4 were wrongly *rejected*) does not hold — no such selector exists; `reflex_vm_syscall_t` stops at `DELAY = 3`.
- ~~P0-M1 mesh replay cache ignored the sender MAC~~: the slot was exactly `nonce & 63` for every peer, contradicting [`../SECURITY.md`](../SECURITY.md) §4. Hash moved to `goose_policy_replay_slot` (host-testable, covers the full MAC, mixes every input bit into the index) and pinned by `[replay]` in `tests/host/test_policy.c`.
- ~~P0-M2 `make format-check` could not fail~~: `clang-format`'s exit status was consumed by a pipe to `head` and the success message printed unconditionally, so the CI gate had been green by construction since it was written. Now line-scoped against the merge-base (`make format-check`), with whole-tree debt reported separately (`make format-check-all`) and `clang-format` pinned in CI.

- ~~P0-H2 LoomScript payload fields copied from the wire unvalidated~~: `orientation` (a multiplicand, so a non-trit propagates states outside the ternary set) and `coupling` (`RADIO` honoured, giving a 100 Hz permanent transmit loop from one upload). Now whitelisted to `{-1,0,+1}` × `SOFTWARE` via `goose_policy_loom_route_acceptable`, pinned by `[loomacc]`, with `_Static_assert`s binding the mirrored constants. `trans_count` is refused rather than silently dropped.
- ~~P0-H3 `goose_supervisor_rebalance` mutated routes with no loom lock~~: now `goose_loom_try_lock` with timeout-and-skip, released before `goose_process_transitions` because the lock is non-recursive. Dual-core (`platform/esp32`) only; benign on the single-core C6.

- ~~P0-H4 `loomc.py` crashed on malformed input and reported success on a typo'd verb~~: rewritten to tokenise properly and refuse rather than guess; every failure exits non-zero. Byte-identical output on both repository examples. Pinned by the new `tests/host/test_loomc.py`, run by `make loomc-test` and CI.
- ~~P0-M5 `tasm.py` robustness cluster~~: all five reproduced and fixed — bare `.entry`, duplicate labels, error line numbers indexing the instruction list rather than the source, `--upload` with no port writing a file named `--upload`, and the serial fd leak plus fixed-sleep race in `upload()`. Five regression tests added.

- ~~P0-M3 system VM soft cache installed then erased~~: `reflex_vm_task_service_init` now preserves `vm.cache` across the re-init, rather than depending on `main.c` line order. `vm/task_runtime.c` joined the host build; pinned by `[vmtask]`, verified to fail without the fix.
- **P0-M4 VM task self-delete lifetime gap — mitigated, not closed.** Analysis differs from the audit. The predicted corruption (a restart reusing `runtime` while the retired task still references it) does not occur: `reflex_vm_task_entry` touches nothing in `runtime` after clearing the handle, so an overlapping restart cannot corrupt the runtime it reuses. The real residual is memory, not correctness — `vTaskDelete(NULL)` defers TCB and stack teardown to the idle task, so the watchdog's stop-then-start briefly holds two VM stacks. `reflex_vm_task_stop` now yields for `REFLEX_VM_TASK_REAP_MS` so the idle task can reap; under sustained load it still may not have run. **Closing the window properly means not self-deleting at all, which is a change to task teardown that wants hardware validation — this is the one P0 item deliberately left open.**

  Found while checking it: the comment at `vm/task_runtime.c` claiming the stop path was "absent from `reflex_os.elf` entirely" was wrong. `reflex_service_watchdog_tick` calls `svc->stop` then `svc->start` for any FAULTED service and `goose_supervisor_pulse` runs it at 1 Hz, so the path executes on every VM fault. Corrected in place.

Low-severity table (§3) closed in the same sweep:

- ~~P0-L2~~ snapshot `learned_orientation` / Hebbian counter clamped on load. NVS is trusted, not infallible — flash wears out, and a partial write is not an attack.
- ~~P0-L3~~ outbound mesh QUERY gated at 10 Hz, matching ingress. Counted as `query_throttled` in `mesh stat`. **The audit's framing was wrong**: `SECURITY.md` §7 only ever claimed an *ingress* limit, which exists and works. The missing egress gate was a real gap, but unstated rather than contradictory. Both §7 and the code now say so.
- ~~P0-L4a~~ lock-free `mesh_stats` documented as deliberate: observability only, no branch reads them, and ~12 mux acquisitions per packet is not worth protecting numbers nothing decides on. Failure mode is a slightly low count on dual-core, never a corrupt one.
- ~~P0-L4b~~ first-boot Aura key health-checked, retried, and replaced by the MAC-derived fallback rather than persisting all-zeros from a latched RNG. `SECURITY.md` §4's `esp_fill_random()` claim was also wrong for the C6 (the primary target reads `RNG_DATA_REG` directly) and is corrected.
- ~~P0-L5a~~ SDK role validated before opening the port; handshake wrapped so any failure closes it.
- ~~P0-L5b~~ `loom_viewer.py` surfaces a dead link instead of rendering its last known graph forever. Its main loop was `while True` and never checked the stop event, so the silent-death window was wider than the audit described.
- ~~P0-L6~~ duplicate `sdk/python/setup.py` removed; `pyproject.toml` verified to build and install standalone. `.DS_Store` / `sdkconfig.old` are already covered by `.gitignore` and `lmm_repo/` is empty, so neither is repository state — no action needed.
- ~~P0-L7~~ `hardware_addr`'s three meanings documented on the field itself, including the FIELD_PROXY/NEURON sub-field name hash and what a future NEURON-over-hardware binding would silently do.
- **P0-L1 does not reproduce.** The claim was that shell-driven weaves stay `PINNED` and permanently consume fabric slots. Both `goose_fabric_alloc_cell` call sites reassign the type on the next line (`VIRTUAL`, `INTENT`), and `goose_policy_cell_evictable` makes both evictable in namespace 0. Recorded so the next pass does not re-investigate it.

Found while checking L1 and fixed: `GOOSE_FRAGMENT_NOT` allocated nothing, wove nothing, logged "Wove Inverter Pattern" and returned `REFLEX_OK`. Unreachable today — the only caller is the shell self-test, which uses HEARTBEAT and GATE — which is why it went unnoticed, not why it was acceptable. It now refuses.

Open from the 2026-09-07 P0 audit: **P0-M4's proper fix only**, pending hardware. Everything else is closed.

Related, observed while fixing P0-H3 and not part of it: `goose_supervisor_check_equilibrium` also reads route state with no loom lock, from the same unlocked `goose_supervisor_pulse` path. Its reads are guarded by the `cached_version` check rather than by the lock, which is the substrate's established staleness discipline, so this is recorded as exposure on dual-core rather than as a defect.

**Formatting debt: 98 of 127 hand-written files do not match `.clang-format`.** Not a defect, but a consequence worth recording: the gate that should have prevented it never ran. The gate now holds changed lines only, so the tree converges as code is rewritten rather than through one mass reformat that would rewrite brace style across the codebase.

Closed in the 2026-08-12 audit remediation (see [`audit-2026-08-12.md`](audit-2026-08-12.md)):

- ~~Warm-boot vital collapse~~: `goose_fabric_alloc_cell` returned NULL for an already-occupied coord, and `fabric_cells[]` is RTC-retained, so after any deep-sleep wake the metabolic vitals, temp vital and LED service all received NULL. The circuit breaker fell through to its literal defaults and reported "thriving" unconditionally, disabling every metabolic gate in the supervisor pulse. Added `goose_fabric_ensure_cell` for idempotent seeding; `alloc_cell` keeps NULL-on-collision for genuine clash detection. Hardware-verified across a `sleep 5` round trip.
- ~~Supervisor NULL dereference~~: `goose_supervisor_check_equilibrium` short-circuited on `cached_source &&` and then dereferenced it anyway. Now skips unresolved and stale-cache routes, matching `internal_process_transitions`.
- ~~Boot0 had no image verification~~: the appended SHA-256 is now verified via the ROM SHA engine before any segment is loaded, and segment destinations are bounds-checked against the true RAM windows. See [`../SECURITY.md`](../SECURITY.md) §0 for the honest limits (the digest is unsigned — corruption detection, not authentication).
- ~~Boot0 retry hung~~: the "software reset to retry" path wrote a reserved bit and spun forever; now uses `LP_AON_HPSYS_SW_RESET`.
- ~~Heap vital over-reported~~: `heap_caps_get_free_size(0)` summed regions `malloc()` cannot serve; now uses `MALLOC_CAP_DEFAULT`.
- ~~Prober bypassed the Sanctuary Guard~~: `goose_supervisor_explore` read raw MMIO before any guard; now consults `goose_fabric_addr_is_sanctuary` at both the probe gate and the re-read.
- ~~LED service dead on every boot~~: it resolved `led_intent`, a name nothing has ever registered (the fabric seeds `agency.led.intent`), so the task deleted itself before weaving the LED field.
- ~~LoomScript loader unsafe~~: `goose_weave_loom` accepted unvalidated `uint16_t` route indices, dereferenced unresolved cells, trusted declared counts past the end of the buffer, and left route fields uninitialised. Hardened ahead of [`prd-tasm-upload.md`](prd-tasm-upload.md) making it reachable.

- ~~Postural consensus was inert~~: `atmosphere_recv_cb` resolved `sys.swarm.posture` and skipped the write when absent — and nothing ever created the cell. The swarm accumulator saturated and crossed its hysteresis threshold exactly as designed while the result was published nowhere. Now seeded in `goose_atmosphere_init` at (0,0,4). Two-board verified: 5 POSTURE arcs at weight 4 drive the peer's cell to state=1.
- ~~Mesh vital false-isolation~~: `REFLEX_METABOLIC_MESH_ISOLATED_TICKS` was a fixed 3 (~3.3s) while the discovery beacon runs every ~10.1s, so a healthy mesh read isolated for ~6.8s of every 10.1s and spuriously halved the discovery interval. Measured on hardware: two paired C6s reported mesh=-1 and mesh=0 simultaneously. Now derived from the discovery and metabolic divisors (tolerates two beacon periods). Verified: 0/10 false isolations over 30s on both boards.
- ~~`mesh stat` hid four counters~~: `goose_mesh_stats_t` carries 12 counters and the shell printed 8, omitting `rx_discover`/`tx_discover`/`rx_mmio_sync`/`tx_mmio_sync` — precisely the ops the supervisor emits autonomously. A healthy mesh with peers registered and `aura_fail=0` reported all-zeros, reading as a dead mesh.

- ~~First `peer.*` lookup was always lost~~: peer phantoms were allocated at `goose_make_coord(5, 0, ghost_counter++)`, sharing a coordinate space with the boot-time atlas weave at `goose_make_coord(i + 5, r, 0)`. The first phantom landed on (5,0,0), already held by atlas node i=0 register r=0, so `alloc_cell` returned NULL and the first peer name resolved on any boot silently failed. Peer phantoms now occupy their own namespace (`trits[8] = -1`), the third alongside seeds (0) and shadow entries (1). Hardware-verified: `peer.alpha.led` previously failed while `peer.beta.led` succeeded; both now resolve.

- ~~ESP32 used a C6-derived MMIO atlas~~: **fixed**. `goose_shadow_atlas.c` is generated from `tools/esp32c6.svd` and was compiled for every target, so the ESP32 resolved C6 register names and reported addresses that mean something else on that silicon. The catalog is now selected per target in `components/goose/CMakeLists.txt`; targets without a scraped SVD get `goose_shadow_atlas_stub.c`, an empty catalog, which is the honest state. The C6 keeps all 12738 entries.
- ~~ESP32 boot banner reported the wrong chip~~: **fixed**. `REFLEX_CHIP_NAME` in `core/boot.c` is now selected per target instead of being hardcoded to `esp32-c6`.
- ~~Lock contention on the dual-core ESP32~~: **diagnosed and fixed 2026-08-12**. Two independent defects:
  1. `goose_snapshot_save` held `loom_authority` across `reflex_kv_set_blob`, an NVS **flash write**, which takes milliseconds against a 300us lock budget. Every save starved the 10Hz pulse. Confirmed by correlation on hardware — faults clustered directly around saves (`FAULT SNAP FAULT FAULT SNAP`). Serialisation now happens under the lock and the flash write outside it, the same discipline `learn_sync` already used for telemetry. Atomicity is per field rather than across all fields, which is the level that means anything since routes belong to a field.
  2. The lock's timeout was measured in CPU cycles via `reflex_hal_cpu_cycles()`. Xtensa `CCOUNT` is **per-core and unsynchronised**, and tasks are created without affinity, so a task migrating mid-spin compared a start value from one core against a current value from the other — and because the subtraction is unsigned, any backwards step became an instant spurious timeout. The budget is now expressed in microseconds against `reflex_hal_time_us()`, which is global on both targets (systimer on C6, `esp_timer` on ESP32). Hold statistics were nonsense for the same reason: the ESP32 reported a 182631-cycle peak (761us at 240MHz) against a documented ~40us. `status` now reports `max_us`/`avg_us`.

  Measured: an identical 45-second shell hammer went from 2 faults to **0**, with peak hold 1021us -> 602us and the same two snapshot saves occurring. C6 regression clean.
- ~~`reflex_service_start_all` aborted on the first failure~~: **fixed**. It now starts every service it can, names each failure individually, and returns the first error at the end; `main.c` logs a degraded service set rather than skipping the atmospheric arcing block. That coupling is how an absent ESP32 temperature sensor cost the board its entire radio.

- ~~`goose_fabric_set_agency` rejected GPIO pin numbers~~: **fixed**. The Sanctuary Guard reasons in 4 KB pages, so a pin index masked to page 0 and every GPIO binding was refused — `led_phys` stayed `PINNED` instead of `HARDWARE_OUT` and the LED's fabric route was decorative. MMIO addresses now go through the Guard while pin indices are range-checked against `GOOSE_AGENCY_GPIO_MAX`.

- ~~`reflex_hal_time_us` returned 0 forever on the C6~~: **fixed 2026-08-12**. Two independent errors in one function. The systimer base was hardcoded `0x60004000`, which is **I2C0** on the ESP32-C6 — `goose_shadow_atlas.c` names that exact address `comm.i2c0.scl_low_period` — so the HAL latched and read I2C registers; the real base is `DR_REG_SYSTIMER_BASE` (`0x6000A000`). With that corrected the clock ran but 2.5x slow, because ticks-per-microsecond is 16, not 40: the C6 clocks the systimer from XTAL through a divider `soc_caps.h` fixes at 2.5. Everything downstream of the clock was inert or wrong:
  - **Aura nonces were always 0**, so every packet with the same (op, coord, name_hash, state) carried an identical MAC — the nonce exists precisely to prevent that.
  - **Replay protection never rejected anything**: `replay_seen_or_record` treats a zero `last_us` as an empty slot, and every slot was zero.
  - **QUERY never produced an ADVERTISE**: the 10Hz throttle computes `now - last_query_processed_us < 100000`, which with a frozen clock is `0 < 100000` on every packet, so the distributed-DNS response path was dead on the C6. Verified restored on two boards — `rx_query=1` on the responder, `rx_advertise=1` on the asker.
  - Peer staleness and the LP heartbeat stall detector could never fire.
  The same wrong base was also in `kernel/reflex_freertos_compat.c` (live, kernel tick ISR) and `kernel/reflex_sched.c`; all three now take it from the SoC headers.

- ~~Input past the line buffer was silently truncated~~: **fixed 2026-08-13**. The excess was dropped and the truncated line dispatched as complete. Now refused as `#R:-1,overflow`. On the ESP32 that guard was initially unreachable: the console reads through `getchar()` from a 128-byte hardware FIFO with no ring buffer, so characters were lost *before* the shell's bound was reached. The UART console now installs a driver with an RX ring sized from `REFLEX_SHELL_LINE_MAX`. Both targets verified at 913/1022/1213/1513/2013 characters.
- ~~Command outcomes were only expressible as prose~~: **fixed 2026-08-13**. The shell computed a three-valued outcome — engaged / latent / withheld — and discarded it, leaving the Python SDK to key `AccessDenied` on `result.startswith("denied:")`. Now emitted as `#R:<trit>,<reason>` after every dispatch, following the `#T:` telemetry convention. Additive, so no existing consumer breaks. Known bound: unmarked handler paths default to `+1,ok`, so the marker is accurate everywhere the hardware suite asserts it and unverified elsewhere — three failure paths that reported success were found by reading outcomes back off a live board.
- ~~The shell's input line was capped at 255 characters~~: **fixed 2026-08-13**. Against an already-allocated 1024-byte buffer, which capped the two commands that can extend a running board at 122 bytes of payload. Raising it required sizing the USB-JTAG RX buffer to match: at the driver's 256-byte default the tail of a long line was silently dropped and arrived as odd-length hex. Verified at 200 and 500 bytes on C6 and ESP32.
- ~~Arc `state` was never validated on the wire~~: **fixed 2026-08-13**. Every consumer treats it as a trit — SYNC and MMIO_SYNC write it into a cell's `state`, POSTURE multiplies it into the swarm accumulator — and neither end checked it. The hysteresis and saturation bounds in [`../SECURITY.md`](../SECURITY.md) §7 are arithmetic valid only for `|state| <= 1`, so `mesh posture 99 4` moved a peer's accumulator by 396 against a threshold of 10. Proven on hardware both ways: on a guard-removed build a single malformed arc flipped the peer's posture; on the fixed build five were counted in `rx_malformed` and discarded with the posture cell unmoved, while valid arcs still worked. Validated at ingress once, after the Aura gate, so no future op can miss it.
- ~~`aura setkey` accepted non-hex and provisioned a zero key~~: **fixed 2026-08-13**. All three operator-facing hex parsers (`aura setkey`, `loom load`, `vm loadhex`) decoded with `strtoul(pair, NULL, 16)`, which returns 0 for any non-hex input without signalling. For `loom load` and `vm loadhex` this was benign — the CRC and `goose_weave_loom`'s structural validation reject the garbage downstream. `aura setkey` has no downstream check, because those 16 bytes *are* the mesh HMAC key: `aura setkey <32 non-hex chars>` silently provisioned an all-zero key and printed "key provisioned". All three now share `shell_parse_hex` in `shell/shell_parse.c`, which validates the whole string before writing anything. Nine mutations verified to fail the suite. The shell's input parsing lives in its own translation unit now, separate from the access-control policy it had been sharing space with.
- ~~The shell had no repeatable hardware test~~: **fixed 2026-08-13**. `tests/hardware/validate_shell.py` (`make hw-test PORT=…`) runs 97 checks per board covering the wire format, the full four-role dispatch matrix, the `tapestry` guards against live cells, and the hex rejection paths. Non-destructive by construction: it never provisions or clears an Aura key — `aura clear` regenerates a *random* per-board key rather than restoring the old one, so it would silently un-pair a working bench — and never reboots or leaves role, vitals or purpose changed.
- ~~The SDK's `AccessDenied` never raised~~: **fixed 2026-08-13**. The shell dispatched on Enter without echoing a newline, so a command's echo and its response shared a line (`led ondenied: requires operator`). The SDK strips the echo line-wise, so `result.startswith("denied:")` was never true and the documented exception never fired — a caller running under `role="agent"` received a string and proceeded as though the command had succeeded. `reflex_shell_run` now echoes the newline before dispatch. Hardware-verified on both C6s.
- ~~Role-Based Access had no host-test coverage~~: **fixed**. The role model in [`../SECURITY.md`](../SECURITY.md) §2 was enforced by one function, `subcmd_min_role`, living in `shell/shell.c` — a file the host suite does not compile, so nothing pinned it. The gating was correct, but a sub-command added without a matching escalation entry would have silently dropped to its base role with no test failing. The table and the escalation rules moved to `shell/shell_policy.c` (`shell.c` keeps the handlers) and are covered by `tests/host/test_shell_policy.c`. The lookup now **fails closed**: a command absent from the policy table requires `admin` rather than defaulting to `observer`, so forgetting the table locks a command down instead of exposing it. Nine mutations — dropped escalations, downgraded roles, a removed argc guard, a command deleted from the table, and both halves of the trit parser — were each verified to fail the suite.
- ~~`vm/cache.c` had no host-test coverage~~: **fixed**. It now compiles in the host suite against a test-supplied memory backend, with the TINV write-back defect pinned by a test that was mutation-verified to fail on the pre-fix code.
- ~~The loom's worst holder was `goose_fabric_get_cell_by_coord`'s linear fallback~~: **fixed**. The coordinate index is now open-addressed with linear probing and backward-shift deletion (`components/goose/goose_lattice.c`), replacing a direct-mapped table that overwrote on collision and left ~21% of cells reachable only by scanning all 256. The fallback scan is gone entirely — a *miss* now terminates at the first empty bucket, which is the case allocation always hits. Measured with real atlas-shaped coordinates at 256/503 occupancy after 20000 churn cycles: hit probes average 1.18 (max 4), miss probes average 1.97 (max 9), against 256 comparisons before. Steady-state peak loom hold fell from 645us to **353us**, average 9us to 5us.
- **`vm run <program>` executes the VM synchronously in the shell task and makes the board unresponsive.** `shell_cmd_vm` calls `reflex_vm_run(&reflex_shell_vm, 100000)` inline, so the shell is busy for up to 100,000 VM steps. With a program whose loop hits the `log` syscall, each iteration blocks on a serial write: measured at ~2 log lines/sec, which puts `blink` in the region of an hour of total unresponsiveness. `vm stop` cannot help, because the shell is *inside* `vm run` and never reaches the dispatcher. The only recovery observed was a hardware reset (DTR/RTS) or a reflash; `reboot` is equally unreachable. Reproduced on both C6s. Pre-existing and unchanged since `f81355d` — the step budget bounds the work but not the wall-clock, because it does not account for syscalls that block. `tests/hardware/validate_shell.py` deliberately never runs a valid program for this reason, which is also why it went unnoticed: the suite only exercises `vm run` on an unknown name and under a denying role.
- **`goose_cell_t` is 48 bytes, not the ~15 its "compact" framing implies.** `reflex_trit_t` is a plain enum, so a 9-trit coordinate costs 36 bytes and `#pragma pack(1)` cannot shrink it. `fabric_cells[256]` is therefore 12288 bytes and the C6 sits at ~93% of its LP RAM budget with 984 bytes spare. Storing trits as `int8_t` would cut the cell to 24 bytes and halve the fabric, but it changes the in-memory layout and would need `GOOSE_FABRIC_MAGIC` bumped.
- **The pulse calls the radio while holding the loom.** `internal_process_transitions` reaches `goose_atmosphere_emit_arc` and `goose_mmio_sync_emit` — and so `reflex_radio_send` — inside the lock. Telemetry hooks in the allocation path (`goose_telem_evict`, `goose_telem_alloc`) sit under both `loom_authority` and `fabric_mux` and reach the USB-JTAG write. Both are bounded now that the FIFO spin is capped, but neither belongs under a spinlock.
- **`internal_process_transitions`' `depth` parameter is vestigial.** Nothing recurses into it — the only caller passes 0 — so the `depth > 3` guard and the `depth == 0` conditions around the lock are always taken. Harmless, but the recursion the code appears to guard against does not exist.

- **There is no shutdown path.** `reflex_service_stop_all` has no callers and is linker-stripped from `reflex_os.elf`, so every `stop` callback registered by a service — including `reflex_vm_task_service_stop` — is unreachable in a running system. The stop implementations and their recent hardening are defensive only. Worth deciding whether an orderly shutdown is wanted (before deep sleep, say) or whether the `stop` half of the service interface should be removed as dead weight.

- ~~`goonies_registry` is an unindexed linear structure, and it is now the loom's worst holder~~: **fixed 2026-08-13**. Both of its scans are now open-addressed indexes with linear probing and backward-shift deletion (`components/goose/goose_registry.c`), the same construction the fabric lattice uses.

  The attribution was measured, not assumed. Per-phase timers under `loom_authority` during `bonsai bloat` put the cost at: registry coordinate scan **688us**, name `strcmp` scan 100us, `goose_shadow_resolve` 149us, evictable-cell scan 116us, `goose_lattice_remove` 88us. The coordinate scan dominated because entries are 76 bytes each — ~19KB — and the walk read only the 36-byte coordinate inside each, missing cache on essentially every step. The earlier guess that flash-cache thrash in `goose_shadow_resolve` was responsible was wrong: that term does not grow during paging.

  `goose_shadow_resolve` is also skipped entirely for system weaves now. Its result only ever gated the protected-name rules, which apply to non-system weavers, so computing it on the paging path was pure cost.

  Measured back-to-back on one C6 at matched churn (761 evictions, three `bonsai bloat` passes): peak loom hold **1640us -> 791us**, average **93us -> 25us**. `atlas verify` stays 12738/12738, paging sustains 300/300, heap flat.

  Note the previously recorded figure of ~2163us was from a differently-conditioned run and did not reproduce; 1640us is the like-for-like baseline re-measured on the same board immediately before the change.

- ~~Long cell names leaked a registry entry on every resolve~~: **fixed 2026-08-13**. Registry entries stored `char name[40]` while 4280 of the 12738 atlas names are longer than that — up to 88 characters. The store truncated silently; lookups used the full name. The two keys never agreed, so a long name was never found and every resolve appended another entry for the same cell. Measured on hardware: five resolves of `agency.gpio.func100_in_sel_cfg.in_inv_sel` grew the registry by five, against one for a short name. The registry would fill with duplicates of a few cells and then refuse further allocation.

  Pre-existing, not introduced by the index — the same test run against the previous commit reproduces it identically, because the old linear scan compared a full name against a truncated entry and missed the same way. It is fixed here because the name became a hash key, where silent truncation is a correctness bug rather than a cosmetic one.

  Names now hold 96 bytes, and anything longer is **refused** rather than truncated, so a future scraper emitting a longer name fails loudly instead of leaking. Matching the *lookup* key to the truncated store would have been cheaper and is wrong: 584 groups of distinct atlas names share their first 39 characters (`...etm_ch0_event_en` vs `...etm_ch0_event_sel`), so it would alias unrelated MMIO registers onto one entry. Cost is +16.8KB BSS (free heap 186.9KB -> 172.1KB). Peak loom hold *improved* again as a side effect, 791us -> 637us, because the leak had been inflating the registry with duplicates and lengthening every probe run.


Remaining (honest limits, not regressions):
- **The eviction ring cannot detect thrashing, despite having been written to do so.** Its comment claimed it tracked "the same cell repeatedly evicted and re-paged". Eviction is round-robin: `last_eviction_idx` advances past each victim and a re-paged cell lands in the slot just vacated, *behind* the cursor, so the same cell cannot be chosen again until the cursor laps the table — ~139 evictions at saturation, far outside a window of 8. A genuine thrash signal would need a re-eviction *rate* over a long window, which is a different mechanism. The window is now exposed as `loom evictions` and documented for what it honestly shows: which cells the substrate is currently discarding, plus a round-robin distinctness invariant. Building real thrash detection remains open, and would want a decision about whether it is worth per-eviction bookkeeping under the loom.
- **Telemetry name buffers still truncate at 40 characters.** The deferred-telemetry and route-name buffers in `goose_supervisor.c` and `goose_runtime.c` are `char[40]`, so atlas names longer than 39 characters are cut short in the telemetry stream. Bounded and safe (`snprintf`), and display-only — the registry itself now stores the full name. Left at 40 deliberately: those buffers are stack-allocated inside the loom hold, and widening them to 96 would grow the stack footprint of the locked region. `fabric_alloc_internal` now carries a third such buffer for the same reason — see the console-I/O-under-the-loom entry below.
- **ESP32 self-arc demo does not manifest**: `manifest_demo_arc` needs an atlas cell at (1,1,0) which the C6-derived weave does not place there on ESP32, so "Geometric Arcing active" never logs. ESP-NOW itself initialises and the board transmits discovery beacons.
- **The ESP32 fabric does not survive deep sleep.** `REFLEX_RTC_DATA_ATTR` expands to `__attribute__((section(".rtc.data")))` on the C6 but to *nothing* on the ESP32 (`platform/esp32/CMakeLists.txt:17`), so `fabric_cells[]`, `fabric_magic` and `loom_authority` live in ordinary DRAM there. The comment in `include/reflex_hal.h` claims the opposite ("On ESP32, this places data in RTC SLOW memory that survives deep sleep"). Benign today — `fabric_magic` reads as zero after any reset, so `goose_fabric_init` always takes its cold-boot branch and rebuilds cleanly — but the warm-boot path is untestable on that target and the header is misleading.
- ~~Telemetry performed blocking console I/O inside the loom lock~~: **fixed** (audit 2026-09-04). `fabric_alloc_internal` emitted `goose_telem_evict` and `goose_telem_alloc` while holding `loom_authority` *and* `fabric_mux`, the latter with interrupts disabled. `goose_telem_*` writes through `reflex_hal_write_raw`, which spins on the USB-JTAG FIFO one byte at a time; the enclosing budget is `LOOM_LOCK_TIMEOUT_US = 300` against the 353us steady-state peak recorded above. With telemetry enabled and the fabric saturated — every allocation also evicting, so two writes per allocation — that is the `LOOM_CONTENTION_FAULT` class already fixed once for flash I/O. These were the only two telemetry sites in the substrate inside a lock; both now snapshot inside and emit after unlock, gated on `goose_telemetry_enabled` so the default path is unchanged. **Not re-measured on hardware:** the reasoning is from the source and the figures above, and a soak with `telemetry on` under `bonsai bloat` churn would confirm it directly.
- ~~The Sanctuary Guard reported success while refusing~~: **fixed** (audit 2026-09-04). All three refusals in `goonies read` returned without setting an outcome, so `#R:+1,ok` followed a refusal — while `shell_outcome.h` documents `SHELL_GUARD` as covering exactly "sanctuary, `sys.*`". Fifteen other shell paths had the same shape, including every handler that printed `rc=0x%x` and reported ok regardless. The hardware contract table had asserted `guard` only for `tapestry signal sys.*`; it now covers the sanctuary case, the parser rejections and the usage branches.
- ~~The HMAC backing the Aura MAC was never tested for correctness~~: **fixed** (audit 2026-09-04). The suite asserted determinism and key/message sensitivity, all of which a wrong digest satisfies — and since both sides of a mesh run the same code, a wrong-but-consistent SHA-256 would have authenticated arcs between two boards indefinitely. The implementation is correct, verified against OpenSSL across key lengths 1–140; RFC 4231 cases 1/2/3/6/7 are now asserted, case 6 being the first thing to execute the `key_len > 64` branch.
- ~~The two platform crypto files were byte-identical copies~~: **fixed**. Consolidated to `platform/reflex_crypto.c`, compiled by both backends and by the host suite. It is portable C with nothing chip-specific, and it backs the Aura MAC — not a place to keep two copies that could silently diverge.
- ~~`reflex_service_stop_all` ignored every `stop()` return~~: **fixed**. It still continues past a failure, which is right on a shutdown path, but now names each one and returns the first error, so a wedged service is distinguishable from a clean stop.
- ~~`reflex_hal_log` used a shared static buffer~~: **fixed**. The buffer is per-call (192 bytes of stack, under 10% of the smallest task stack) so concurrent loggers can no longer overwrite each other's message mid-format. Fixing this also surfaced a worse defect in the same function — see the bounded USB FIFO entry below.
- **The loom lock budget is shorter than the worst legitimate hold.** `LOOM_LOCK_TIMEOUT_US` is 300us, but measured peak holds sit around 590-630us on both targets. The dominant cost is `goose_fabric_get_cell_by_coord`, whose lattice-hash miss falls back to a linear scan of up to 256 cells at 9 trit comparisons each, under `loom_authority` via `goose_fabric_alloc_cell`. A contender arriving during such a hold therefore cannot wait it out and takes the skip-on-contention path by design — but it means the budget cannot currently be satisfied by the slowest legitimate operation. Either the budget should exceed the worst hold, or the fallback scan should move out from under the lock. Left as a tuning decision because it changes real-time behaviour.
- ~~Mesh health could never read +1~~: **fixed 2026-08-12**. `compute_mesh_state` now counts arcs across the isolation window (plus the previous completed window) rather than within a single ~1.1s vital scan, and the threshold is recalibrated to 2 — "heard from someone at least twice in roughly the last 40s". All three ternary states are reachable and the `connected: relax discovery` branch runs. Verified on two paired boards.
- ~~Shell coordinate display was lossy~~: **fixed 2026-08-12**. `goonies find` and `loom list` now render the namespace (`@shadow` / `@peer`) and compose the full 16-bit index from `trits[6]`/`trits[7]`, so distinct cells no longer print the same triple and shadow entries 256 apart are distinguishable. Namespace 0 keeps its signed index.

- **Boot integrity is unsigned**: boot0 verifies the appended SHA-256, which detects corruption but not tampering — the digest is not signed, so an attacker who can rewrite flash can rewrite it too. Secure Boot with an efuse-burned key is the real fix and is not enabled. The partition table is also unverified, and there is no rollback protection.
- **The cooperative scheduler is not linked**: `kernel/reflex_sched.c` compiles but the linker discards every symbol — no `reflex_sched_*` appears in `reflex_os.elf`, and `reflex_task_kernel.c` delegates to FreeRTOS. Note that `CONFIG_REFLEX_KERNEL_SCHEDULER=y` selects the kernel *task backend*, not this scheduler; the naming invites the opposite reading. Its inline-asm stack switch is undefined behaviour and needs an asm trampoline before any revival.
- ~~Shadow paging stopped once the Loom saturated~~: **fixed 2026-08-12**. Eviction eligibility is now a property of the coordinate namespace rather than the cell type — shadow-paged entries (`trits[8] == 1`) and peer phantoms (`-1`) are caches and reclaimable; seeds and the boot atlas weave (`0`) are identity and keep the original type rules. Cell type describes access control and provenance, not lifetime, and two values were leaking into it on the paging path: `alloc_cell` stamps `PINNED` on every system weave, and `set_agency` copies `SYSTEM_ONLY` from the atlas for `sys.*` registers. Letting either veto eviction is what pinned the Loom. `goonies_register`'s `NO_MEM` return is also checked now, so the registry and cell table can no longer desynchronise into nameless cells. Measured: `bonsai bloat` went from 142 resolved once and then permanently stuck, to 300/300 sustained across repeated runs with eviction cycling (158, then 300 per run), while `atlas verify` stays 12738/12738 and every seeded cell survives the churn.
- ~~Supervisor sub-pass rates ran ~9% slow~~: **fixed 2026-08-12**. All eleven dividers now use `++div >= N`, which fires on exactly the N-th pulse, so the documented rates are literal. Constants derived from the divisors (`REFLEX_METABOLIC_MESH_ISOLATED_TICKS`) dropped their correction terms accordingly. Measured on device: 111 balance events in ~11s = 10.09Hz.
- ~~Snapshots truncated past 16 routes~~: **fixed 2026-08-12**. Format v2 raises the cap to 32 (matching `MAX_ROUTES_PER_FRAGMENT`, the largest route_count any field the system can build), and the header now records the number of entries actually written rather than the field's declared `route_count`. Truncation is unreachable in practice and logged rather than silent if it ever occurs.

- ~~Aura wire size~~: **widened 2026-08-12**. The Aura is now 64 bits at protocol epoch `GOOSE_ARC_VERSION = 0x03`, moving collision resistance from roughly 2^16 to 2^32. Verified on two boards: unpaired peers still fail closed with `aura_fail` climbing and `version_mismatch=0`.
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

1. ~~**Purpose-modulated capability matching in `weave_sync`**~~ — shipped. When a PURPOSE cell is active, `weave_sync` biases autonomous fabrication toward sinks matching the purpose domain via segment-bounded name matching (`.domain.` or trailing `.domain`), falling back to generic suffix match.
2. ~~**Purpose name persistence**~~ — shipped. `goose_purpose_set_name` persists to NVS (`goose/purpose`); `goose_fabric_init` restores it on boot, re-allocating the `sys.purpose` cell with `state=1`. `purpose get` reports the stored name.
3. **External I2C sensor on the fabric** — expose a BME280 (or similar I2C peripheral) as addressable GOOSE cells under `perception.i2c0.bme280.*`. The internal temp sensor proves the architecture; an external sensor proves the extensibility.
4. **Automatic snapshot cadence** — call `goose_snapshot_save` on a supervisor-driven timer (e.g., every 60 seconds after the stability marker) rather than requiring an explicit `snapshot save` from the shell.
5. **Multi-VM contexts** — support concurrent execution of multiple ternary task images.
6. **Aura key derivation from efuse** + secure-boot integration.
