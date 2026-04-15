# Changelog

All notable changes to Reflex OS are documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/)
and this project uses a loose form of [Semantic Versioning](https://semver.org/spec/v2.0.0/).

## [Unreleased]

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
- **Version framing** — README narrowed "MMU-backed shared ternary memory" to "region-protected shared ternary memory" to match the actual `vm/mmu.c` bounds-checker; "9,531 atomic intents" reframed as a 9,531-node shadow catalog with 104 pre-woven at boot and the remainder paged on demand.
- **SECURITY.md §2 Authority Sentry** — wording now matches the skip-and-log behavior (no longer claims to force-break a held lock).
- **SECURITY.md §3 Atmospheric Aura** — rewritten around HMAC-SHA256, replay cache, per-board provisioning, and an honest "Known Limits" paragraph.

### Archived
- `DO_THIS_NEXT.md`, `NEXT_ACTIONS.md`, `OS-BACKLOG.md` — pre-audit planning docs, superseded by the current `docs/implementation-status.md`, `docs/strategy.md`, and `docs/potentials.md`.
- `REMEDIATION.md`, `docs/REMEDIATION-PLAN.md` — completed remediation trackers from this and the prior session. History preserved in git log.
- `sdkconfig`, `test.tasm`, `supervisor.hex`, `supervisor.rfxv`, `test.rfxv`, `cat_log.txt`, `monitor_log.txt`, `session-ses_2716.md` — local-only artifacts, moved to gitignored `archive/`.

### Notes
- This changelog begins from the current remediation arc. Earlier history is preserved via `git log`.
- "Red-team" commits are functionally the same kind of change as "fix" commits; they're tagged differently only to keep the audit story legible.
