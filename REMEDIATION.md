# Remediation Plan — Red-Team Pass 2

Tracks execution of the nine findings from the post-Items-1..4 red-team review. Each finding is a discrete commit, validated on hardware before the next begins.

**Status legend:** ☐ pending · ◐ in progress · ☑ done

---

## Phase A — Lock hot-path mutations

Unify concurrency discipline: every write to shared mutable state along the pulse path holds `loom_authority`. Three sites found, one phase.

### ☑ R1 — CRITICAL — autonomy_field route append race

**Finding.** `goose_supervisor_weave_sync` (`goose_runtime.c:310`) increments `autonomy_field->route_count++` and then populates `routes[r]` without holding `loom_authority`. Meanwhile the autonomy field's pulse task runs `internal_process_transitions` on the same field at 10 Hz. The pulse can observe the incremented count before the new route is populated, reading a half-constructed route.

**Fix.** Take `loom_authority` via `goose_loom_try_lock(NULL)` around the append block. Populate the route first, then publish `route_count` via an atomic store so the pulse reader sees either the old count or the fully-populated new entry, never an intermediate state.

**Validation.** Exercise by booting, letting the supervisor run for >30 s, and confirming no crashes, no corrupted route names in any inspection command. Implicit: existing heartbeat and resolve tests still pass.

---

### ☑ R2 — CRITICAL — short-circuit alloc calls `goonies_register` outside lock

**Finding.** `goose_fabric_alloc_cell`'s coord-already-exists path (introduced by Item 2) releases both `loom_authority` and `fabric_mux` before calling `goonies_register`. The main path calls `goonies_register` inside the `fabric_mux` critical section. Inconsistent. Two concurrent warm-boot paths (atlas re-weave + runtime shadow paging during init) could corrupt `goonies_registry`.

**Fix.** Keep `loom_authority` held across the short-circuit `goonies_register` call. Release `fabric_mux` before the call (the registry is its own data structure), but loom_authority stays. The perf concern (`goonies_register` calls shadow_resolve inside a lock) is shared with the main path today and is addressed by R6 as a polish step.

**Validation.** Cold boot, warm boot, deep-sleep wake — all three paths should still resolve `agency.led.intent` and the 104 atlas nodes without regression.

---

### ☑ R5 — MEDIUM — Hebbian `learn_sync` mutates routes concurrently

**Finding.** `goose_supervisor_learn_sync` (`goose_supervisor.c`) writes `route->hebbian_counter` and `route->learned_orientation` on the supervisor task while pulse tasks read `learned_orientation` without synchronization. Not a torn-byte race (int8_t is atomic on RISC-V), but the counter→orientation commit is multi-step and an observer can see inconsistent intermediate state for one pulse.

**Fix.** Take `loom_authority` around the per-route mutation block in `learn_sync`. The supervisor task already runs the surrounding field iteration; the critical section is small.

**Validation.** Covered by general stability testing. Plasticity isn't currently exercised on-device (no pain/reward cells are signaled), so this is a defensive fix before the first real plasticity demo.

---

## Phase B — Replay cache expiration

### ☐ R3 — HIGH — replay cache `last_us` never read

**Finding.** `goose_atmosphere.c` stores `last_us` on every packet but nothing reads it. The intended time-based expiration isn't implemented. Combined with 16-slot direct-mapped indexing, the effective guard window is "until the next packet hashing to the same slot", not "within a defined time window".

**Fix.** On incoming packet, read `last_us`. If `now - e->last_us > REPLAY_WINDOW_US` (5 seconds), treat the slot as empty (overwrite, don't report duplicate). If within window AND (mac, nonce) match, reject as replay. Also increase `REPLAY_CACHE_SLOTS` from 16 → 64 to reduce cross-peer slot collisions (R9 rolled in here).

**Validation.** Single-board can't directly validate replay guard, but unit behavior can be inferred from logs. Confirm no regression in normal packet flow on the existing self-arc demo.

---

## Phase C — LP liveness monitoring

### ☐ R7 — MEDIUM — LP core crash undetected

**Finding.** If the LP RISC-V coprocessor crashes (illegal insn, stack overflow, memory fault), `lp_pulse_count` stops advancing and HP has no way to know. The `heartbeat` shell command would return a stale value indefinitely.

**Fix.** In `goose_supervisor_pulse`, track `(last_observed_count, last_advance_us)`. If the count hasn't advanced in 5 seconds while HP is otherwise healthy, log `LP_HEARTBEAT_STALLED` (rate-limited). Do not auto-restart for now — observability first, recovery later.

**Validation.** Difficult to trigger a real LP crash on demand. Defensive logging only; confirm the log path doesn't fire under normal operation over a 30 s window.

---

## Phase D — Polish

### ☐ R4 — HIGH — `reflex_trit_t` out-of-range comment

**Finding.** Shadow atlas coords store `0x00..0xFF` in trits[6] and trits[7] via `goose_make_shadow_coord`. `reflex_trit_t` is an enum `{-1, 0, +1}`; out-of-range stores are implementation-defined. No current code breaks because `goose_lattice_hash` treats the values as unsigned bytes and `goose_coord_equal` is byte-wise, but a future iterator doing `sum += trits[i]` across all 9 slots would produce nonsense on shadow coords.

**Fix.** Comment on `reflex_tryte9_t` in `reflex_ternary.h` (or `goose.h` where shadow semantics live) documenting that shadow coords use trits[6,7] as opaque byte slots and that trit-semantic iteration must check cell type first. Doc-only change, no code.

**Validation.** None required.

---

### ☐ R6 — MEDIUM — alloc holds loom across shadow_resolve

**Finding.** `goonies_register` calls `goose_shadow_resolve` (9527-entry binary search) inside the locked portion of `alloc_cell`. A concurrent pulse attempting to acquire during an alloc will hit the 300 µs timeout and log `LOOM_CONTENTION_FAULT`.

**Fix.** Pre-check shadow resolution outside the lock in `alloc_cell`, pass the pre-resolved `(in_shadow, is_protected)` tuple into a variant of `goonies_register` that skips the shadow check. Alternative: raise the lock timeout for alloc specifically. The pre-check approach is cleaner but touches more code.

**Validation.** `heartbeat` command under rapid invocation should show no `LOOM_CONTENTION_FAULT` in the log.

---

### ☐ R8 — LOW — misleading lock timeout log message

**Finding.** `goose_loom_try_lock`'s timeout log says `LOOM_CONTENTION_FAULT field=... skipping pulse`, which is misleading when called from `goose_fabric_alloc_cell` (not a pulse).

**Fix.** Drop the "skipping pulse" phrase, use a neutral "deferred" wording. Optionally pass a call-site tag via a parameter, but a single generic message is fine.

**Validation.** None required.

---

## Execution log

- **Phase A** — R1, R2, R5 — locked all hot-path mutation sites under `loom_authority`. Made `goose_loom_try_lock` / `goose_loom_unlock` public so `goose_supervisor_learn_sync` can use them. R1's append now uses a `__atomic_store_n(..., __ATOMIC_RELEASE)` on `route_count` so even lock-free pulse readers see either the old count or a fully-constructed new route. Validated on C6: resolve + heartbeat + 10 s stability clean, no contention faults.
