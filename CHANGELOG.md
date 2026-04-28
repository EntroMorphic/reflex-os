# Changelog

All notable changes to Reflex OS are documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/)
and this project uses a loose form of [Semantic Versioning](https://semver.org/spec/v2.0.0/).

## [Unreleased]

### Added
- **Role-Based Access (RBA)** — capability-based session roles for the shell and Python SDK. Four roles: observer (read-only), agent (AI guardrail: purpose + snapshots), operator (day-to-day: LED, VM, mesh), admin (everything: reboot, aura, config). Every command classified by minimum role. `auth role <role>` restricts the session. SDK: `ReflexNode(port, role="agent")` with `AccessDenied` exception. Telemetry: `#T:U,<role>`. Default: admin (backward compatible). No PINs — voluntary capability declaration; PIN auth deferred to remote-shell phase.
- **Phase 33 Self-Expanding Perception (Curiosity Attractor)** — the OS is curious when purpose is active. A two-phase probe reads HARDWARE_IN shadow atlas registers 1 second apart; registers whose values changed are *hot* (alive) and get paged into the Loom. Hebbian learning decides which are relevant; eviction forgets the rest. Pain amplifies curiosity (doubled probe rate), not triggers it. Metabolic-gated (suspended in surviving, halved in conserving). Telemetry: `#T:D,<name>`. Shell: `status` shows curious/urgent/idle. No new files — a sub-pass of the existing supervisor.
- **SVD array register expansion** — the shadow atlas scraper now reads `<dim>` and `<dimIncrement>` from the SVD and generates entries for every array index. Shadow catalog grows from 9,527 to 12,738 entries: all 6 LEDC channels, all 3 DMA channels, all 35 GPIO pins, 128 GPIO input mux selectors, all RMT TX/RX channels, all PCNT units, and all crypto memory banks. Atlas bumped to v3.0.
- **Getting Started guide** — `docs/GETTING_STARTED.md`: 12-step guide from hardware purchase through build, flash, shell, Python SDK, and Loom Viewer. Covers macOS, Linux, and Windows with verification steps and troubleshooting at each stage.
- **Python SDK: vitals, telemetry, goonies read** — `vitals()`, `vitals_override()`, `vitals_clear()`, `telemetry_on()`, `telemetry_off()`, `goonies_read()` methods added to `ReflexNode`.

### Fixed
- **Zone mapping: TIMG0/TIMG1 case mismatch** — `zones.json` had mixed-case keys (`Timg0`/`Timg1`) that silently fell through to the default `sys` zone. 84 timer registers reclassified from `sys/SYSTEM_ONLY` to `perception/HARDWARE_IN`.
- **Zone mapping: 18 missing SVD peripherals** — IEEE802154 (radio), DMA (logic), EXTMEM/HMAC/HP_APM/HP_SYS/SLCHOST (sys), I2S0 (comm), INTPRI (logic), TWAI1 (agency), and 8 others were missing from `zones.json` and silently defaulting to `sys`. All 65 SVD peripherals now have explicit zone assignments.
- **Shadow atlas comm/radio cell types** — regenerated `goose_shadow_atlas.c` so 2,022 comm entries are `HARDWARE_OUT` and 175 radio entries are `SYSTEM_ONLY` (were `GOOSE_CELL_VIRTUAL`, making them evictable despite real hardware).

- **Phase 31 Metabolic Regulation** — two-layer self-governance. Circuit breaker (`sys.metabolic`): +1 thriving / 0 conserving / -1 surviving with instant degradation and 30s hysteretic recovery. Per-vital resource governance: mesh isolation increases discover frequency, heap pressure blocks shadow paging, conserving halves learning rate, surviving suspends learning/fabrication/swarm/snapshots. Vital cells: `perception.power.battery`, `perception.mesh.health`, `perception.heap.pressure`. Shell: `vitals`, `vitals override <vital> <state>`, `vitals clear`. Telemetry: `#T:X`. Mesh excluded from circuit breaker (connectivity ≠ resource constraint). USB-powered boards default to thriving.
- **Phase 30 Loom Viewer** — real-time substrate visualization via Rerun.io. Push-based telemetry: `goose_telemetry.c` emits `#T:`-prefixed lines on serial at mutation points (cell state, route propagation, Hebbian learning, mesh arcs, autonomous evaluation). `tools/loom_viewer.py` reads serial and logs to Rerun (GraphNodes/GraphEdges for topology, Scalars for time series, TextLog for events). Shell command: `telemetry on/off`. Deferred emission outside `loom_authority` locks. Direct USB JTAG register writes via `reflex_hal_write_raw`. Hardware-validated: 10Hz balance, 1Hz eval, 15/15 on-device tests.
- **`reflex_hal_write_raw`** — direct serial write bypassing stdio buffering. Used by telemetry to ensure output from all task contexts (supervisor, pulse, shell).
- **Reflex kernel port assembly** — owns interrupt context switching via `--wrap` on `rtos_int_enter/rtos_int_exit`. Hardware stack guard management with proper TCB bounds (pxStack=0x30, pxEndOfStack=0x44). Compile-time assertions prevent silent offset drift.
- **Kernel supervisor (purpose-modulated scheduling)** — 1Hz policy tick adjusts FreeRTOS task priorities based on purpose, Hebbian maturity, holon lifecycle, and pain signals. Domain-specific: `reflex_domain_match` uses dot-boundary matching (purpose "led" matches "agency.led.intent" but not "misled").
- **Holonic lifecycle management** — named field groups with domain tags. Holons matching the current purpose stay active; non-matching ones are deactivated and drop to base priority. Three holons created at boot: autonomy, comm (mesh), agency (led). `reflex_holon_create`/`reflex_holon_add_field` API.
- **ESP32 (Xtensa) platform** — Reflex OS builds and runs on original ESP32 as mesh peer. New `platform/esp32/` backend with HAL, task, KV, radio, crypto. Build system auto-selects platform by IDF_TARGET.
- **802.15.4 radio independence** — in 802.15.4 mode, WiFi/ESP-NOW/esp_event/esp_netif completely absent. Net component no-ops. Platform REQUIRES: `ieee802154` only.
- **Own interrupt allocation** — `reflex_hal_intr_alloc` uses direct PLIC register writes (interrupt matrix routing, priority, type, enable). Bitmap allocator for CPU interrupt numbers. `esp_intr_alloc.h` eliminated.
- **Own logging** — `reflex_hal_log` writes directly to USB-JTAG CDC-ACM FIFO registers (0x6000F000). `esp_log.h` eliminated.
- **Own temperature sensor** — direct APB_SARADC TSENS register reads. `driver/temperature_sensor.h` eliminated.
- **Own key-value storage** — `reflex_kv_flash.c` uses ROM spiflash functions for page-based KV store. `nvs_flash` eliminated in 802.15.4 mode.
- **Own sleep** — wakeup cause via PMU register read, sleep entry via LP AON + software reset. `esp_sleep.h` eliminated.
- **802.15.4 extraction shim** — `radio/ieee802154/reflex_ieee802154_shim.h` maps all ESP-IDF runtime dependencies to Reflex OS primitives (16 call sites).
- **Complete kernel task backend** — `kernel/reflex_task_kernel.c` implements all 13 `reflex_task.h` functions with zero FreeRTOS: create, delete, delay, yield, get_by_name, set/get_priority, queue create/send/recv, critical enter/exit, mutex init.
- **Portable task API extensions** — `reflex_task_get_by_name`, `reflex_task_set_priority`, `reflex_task_get_priority` added to `reflex_task.h`.
- **`goonies_resolve_name_by_coord`** — reverse lookup from geometric coordinate to registered cell name.
- **Paper** — `docs/paper-novel-contributions.md`: 6 novel contributions with code evidence, red-teamed and remediated. 24,500 LOC, two architectures.
- **Purpose name persistence** — `goose_purpose_set_name` persists the user-declared purpose name (max 15 chars) to NVS under `goose/purpose`. `goose_fabric_init` restores it on boot.
- **Purpose-modulated routing in `weave_sync`** — autonomous fabrication preferentially routes needs to sinks in the purpose domain.
- **Phase 29 Tapestry Snapshots** — `goose_snapshot_save/load/clear` serialize learned_orientation + hebbian_counter to NVS.
- **Internal temperature sensor service** — die temperature projected into fabric as `perception.temp.reading`.

### Fixed
- **Kernel task backend dormant** — `reflex_task_kernel.c` delegated to the cooperative scheduler (`reflex_sched.c`) which was never started. Supervisor task, field pulse tasks, and all substrate tasks were silently not running under `CONFIG_REFLEX_KERNEL_SCHEDULER=y`. Rewrote to delegate task management to FreeRTOS while preserving kernel ownership of interrupt context switching and scheduling policy. Supervisor and pulse task stacks increased from 4096 to 6144 bytes for telemetry deferred-emission arrays.
- **Port assembly ra clobber** — `call vTaskSwitchContext` clobbered return address; `ret` jumped to garbage. Fixed by saving ra/mstatus in callee-saved registers (s10/s11).
- **Port assembly mstatus passthrough** — was reading from CSR instead of passing caller's value through. ISR vector expects mstatus returned in a0.
- **LOOM_CONTENTION_FAULT spam** — rate-limited from per-interrupt to once per 5 seconds with contention count.
- **FreeRTOS leaks** — eliminated all direct FreeRTOS calls from shell (vTaskDelete→reflex_task_delete, vTaskDelay→reflex_task_yield) and net/wifi.c (dead includes removed).
- **SYSTIMER interrupt source number** — was 38 (wrong, worked by accident via esp_intr_alloc remapping), corrected to 58 (actual interrupt matrix register offset).
- **Multi-holon priority logic** — field keeps boost if ANY containing holon is active; only suppressed when ALL are inactive.
- **NULL validation** — added to `reflex_holon_add_field` and `goose_supervisor_register_field`.

### Changed
- **Kernel compat layer** — rewritten as policy-on-mechanism architecture. Supervisor calls registered policy function at 1Hz via weak/strong symbol linkage.
- **CMakeLists** — target-gated platform selection, conditional ULP/bootloader, WiFi requirement conditional on radio backend.
- **Documentation** — all stale comments updated, PRD/independence-plan/Kconfig reflect completed milestones.

## [2.6.0] — 2026-04-15

### Added
- **Coherent Heartbeat** — LP RISC-V coprocessor program that ticks in parallel to HP at 1 Hz via `ULP_LP_CORE_WAKEUP_SOURCE_LP_TIMER`. HP mirrors `agency.led.intent` into LP-local globals each supervisor pulse. New `heartbeat` shell command reads the LP pulse counter.
- **HMAC-SHA256 Aura** — replaces the custom rolling-hash keyed signature on atmospheric arc packets. Computed over `(version, op, coord, name_hash, state, nonce)`, truncated to 32 bits for wire compatibility, stack-allocated mbedtls context (no per-packet heap alloc).
- **Aura replay cache** — 64-slot time-bounded guard window (5 seconds) indexed by a hash blending nonce and the last two MAC bytes. Stale entries are treated as empty; cross-peer slot collisions are minimized.
- **Aura protocol version byte** — `GOOSE_ARC_VERSION=0x02`. Cross-firmware packets log `AURA_VERSION_MISMATCH` (rate-limited) instead of silent drops.
- **First-boot Aura key auto-provisioning** — every fresh board generates a unique 16-byte key via `esp_fill_random()` on first NVS open. New `aura setkey <hex>` shell command for explicit pairing.
- **NEURON quorum aggregation** — `GOOSE_CELL_NEURON` cells now aggregate all sub-field routes via ternary sum-and-majority via `neuron_quorum` in `goose_runtime.c`, replacing the previous `routes[0]`-only copy.
- **Reward-gated Hebbian plasticity** — `goose_supervisor_learn_sync` implements fire-together-wire-together with a per-route `hebbian_counter`, reward-gated increment under `sys.ai.reward`, decay under `sys.ai.pain`, and commit to `learned_orientation` at threshold.
- **Generic autonomous fabrication** — `goose_supervisor_weave_sync` now derives the capability suffix from the need cell's own name (e.g. `sys.need.led` → `.led`) instead of the previous hardcoded `.led` lookup.
- **Deep-sleep name persistence** — `goose_fabric_alloc_cell` re-registers names when the coord is already occupied, so the warm-boot atlas re-weave repopulates the BSS-resident `goonies_registry` correctly. Seed cells (`sys.origin`, `agency.led.intent`) are now re-registered on both cold and warm boot paths.
- **LP heartbeat liveness monitor** — `goose_supervisor_pulse` tracks LP pulse count advancement and logs `LP_HEARTBEAT_STALLED` (rate-limited) if LP stops ticking for more than 5 seconds while HP is healthy.
- **Public loom lock API** — `goose_loom_try_lock` / `goose_loom_unlock` are now exported from `goose.h` so any module outside `goose_runtime.c` can serialize hot-path mutation sites against the pulse reader.
- **`sleep <secs>` shell command** — thin wrapper around `esp_deep_sleep_start` with an LP-timer wakeup for on-device wake-path testing.
- **GitHub Actions build workflow** — `.github/workflows/build.yml` runs `idf.py build` on every push and pull request against `main`.
- **Full-surface MMIO name resolution** — the shell's `goonies find <name>` now falls through from the live `goonies_registry` to `goose_shadow_resolve`, so every one of the 12738 SVD-documented entries is addressable by name without having to page a cell into the active Loom first. Live vs shadow hits are labeled in the output (`[live]` vs `[shadow]`).
- **`atlas verify` shell command** — walks the entire shadow catalog with a full round-trip equality check (name, addr, bit_mask, type, and coord via `goose_make_shadow_coord`) plus an adjacent-pair duplicate sweep. Emits a progress dot every 1000 entries and yields via `vTaskDelay(0)` between chunks. Validated on Alpha: `ok=12738/12738 (100% of SVD-documented MMIO catalog); duplicates=0, failures=0`.
- **Public shadow catalog API** — `shadow_node_t`, `shadow_map`, `shadow_map_count`, and `goose_shadow_resolve` are now public in `goose.h` instead of reached via inline `extern` declarations inside `goose_runtime.c` functions. The scraper no longer emits its own local typedef.
- **Scraper ASCII invariant** — `tools/goose_scraper.py` now asserts every catalog name is pure ASCII at scrape time and sorts via an explicit `encode("ascii")` byte key, matching `goose_shadow_resolve`'s `strcmp` byte ordering. Reciprocal sync-discipline comments added to both the scraper and `shadow_node_t` in `goose.h` so struct-layout drift can't silently break regeneration.

### Fixed
- **Silent lock break** — `goose_loom_lock_with_stats` used to force-clear a held lock on timeout and then silently continue running unsynchronized. Replaced with `goose_loom_try_lock`, which skips the affected pulse and logs `LOOM_CONTENTION_FAULT`.
- **Cold-boot NULL deref** — removed post-`goose_fabric_alloc_cell` `->type = GOOSE_CELL_PINNED` assignments that dereferenced a NULL returned by `goose_fabric_get_cell` while `lattice_stable=false`. `alloc_cell` already pins system cells via `is_system_weaving`.
- **RTC cold-boot reset** — added `GOOSE_FABRIC_MAGIC` (0xF11BFABE) and unconditional `loom_authority = 0` at init so brown-out / undefined-wakeup resets don't leave stale cells or a stuck software spinlock bit.
- **Boot-order ESP-NOW crash** — `manifest_demo_arc()` now runs after `reflex_service_start_all()` so `esp_now_init` finds the Wi-Fi stack initialized.
- **Shadow atlas int8_t overflow** — widened `shadow_node_t::c` to `int16_t` and added `goose_make_shadow_coord` that encodes wide cell indices across `trits[6]` and `trits[7]` without truncation. Closed ~100 `-Woverflow` warnings.
- **Alloc/pulse lock cycle** — `goose_fabric_alloc_cell` now takes `loom_authority` before `fabric_mux`, serializing against the pulse reader. Lock order: `loom → fabric`.
- **autonomy_field route append race** — `goose_supervisor_weave_sync` now holds `loom_authority` around the append block and publishes `route_count` via `__atomic_store_n(..., __ATOMIC_RELEASE)`.
- **Hebbian learn_sync mutation race** — the plasticity pass now holds `loom_authority` across all per-route mutations.
- **Short-circuit alloc goonies_register** — the coord-already-exists path now keeps `loom_authority` held across the `goonies_register` call for consistency with the main path.
- **Replay cache window** — `last_us` is now actually read; stale entries are treated as empty, and the slot index blends the last two MAC bytes into the hash to reduce cross-peer false positives.
- **Aura fail-closed** — HMAC computation errors return a non-matchable sentinel (`0xDEADBEEF`) plus `ESP_LOGE`, preventing a transient mbedtls failure from producing `aura=0` on both sides.
- **Posture weight cap** — `ARC_OP_POSTURE` handling clamps the per-packet weight at `SWARM_WEIGHT_MAX=4` on both emit and receive, so a single rogue peer cannot cross `SWARM_THRESHOLD=10` alone.
- **Unchecked malloc in `goose_supervisor_weave_sync`** — autonomy_field allocation now frees partial state and returns `ESP_ERR_NO_MEM` on failure.
- **Signed/unsigned compare** in `goose_fabric_get_cell_by_coord` cleaned up with an explicit `(uint32_t)idx` cast.
- **LP core variant** — ESP32-C6 uses the `LP_CORE` ULP variant, not `RISCV`. Earlier attempts at enabling the wrong variant are now corrected in `sdkconfig.defaults`.

### Changed
- **Repository layout** — design documentation moved to `docs/` with a `docs/vm/` subdirectory. Only the GH-canonical files remain at the root (`README.md`, `LICENSE`, `CONTRIBUTING.md`, `SECURITY.md`, `CHANGELOG.md`, `CMakeLists.txt`, `partitions.csv`, `sdkconfig.defaults`).
- **`sdkconfig` is now generated** — the previously committed snapshot has been archived; fresh builds regenerate from `sdkconfig.defaults`, which now pins `CONFIG_IDF_TARGET="esp32c6"`, `CONFIG_IDF_TARGET_ESP32C6=y`, `CONFIG_ULP_COPROC_ENABLED=y`, `CONFIG_ULP_COPROC_TYPE_LP_CORE=y`, `CONFIG_ULP_COPROC_RESERVE_MEM=2048`.
- **`tools/` consolidation** — `tasm.py` moved alongside `loomc.py` and `goose_scraper.py`.
- **`examples/` consolidation** — `supervisor.tasm` moved alongside `button_blink.loom` / `self_heal.loom`.
- **Version framing** — README narrowed "MMU-backed shared ternary memory" to "region-protected shared ternary memory" to match the actual `vm/mmu.c` bounds-checker; "12,738 atomic intents" reframed as a 12,738-node shadow catalog with 104 pre-woven at boot and the remainder paged on demand.
- **SECURITY.md §2 Authority Sentry** — wording now matches the skip-and-log behavior (no longer claims to force-break a held lock).
- **SECURITY.md §3 Atmospheric Aura** — rewritten around HMAC-SHA256, replay cache, per-board provisioning, and an honest "Known Limits" paragraph.
- **Shell `goonies find` output schema** — previously emitted a single line `GOONIES: Resolved '<name>' to (f,r,c)` on hit. Now emits either `GOONIES: Resolved '<name>' to (f,r,c) [live]` on a live-registry hit or `GOONIES: Resolved '<name>' [shadow] addr=0x... mask=0x... type=<TYPE>` on a shadow-catalog fall-through. Any external script parsing the old format should be updated to handle both forms and the `[live]` / `[shadow]` label.

### Archived
- `DO_THIS_NEXT.md`, `NEXT_ACTIONS.md`, `OS-BACKLOG.md` — pre-audit planning docs, superseded by the current `docs/implementation-status.md`, `docs/strategy.md`, and `docs/potentials.md`.
- `REMEDIATION.md`, `docs/REMEDIATION-PLAN.md` — completed remediation trackers from this and the prior session. History preserved in git log.
- `sdkconfig`, `test.tasm`, `supervisor.hex`, `supervisor.rfxv`, `test.rfxv`, `cat_log.txt`, `monitor_log.txt`, `session-ses_2716.md` — local-only artifacts, moved to gitignored `archive/`.

### Notes
- This changelog begins from the current remediation arc. Earlier history is preserved via `git log`.
- "Red-team" commits are functionally the same kind of change as "fix" commits; they're tagged differently only to keep the audit story legible.
