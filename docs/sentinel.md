# Sentinel — Model-of-Normalcy Sense (Integration Spec, v2)

**Status:** Proposed (v2 — remediated after a code-grounded red-team of v1). **Slots as:** a new
designated-signal sense, complementary to Phase 33 (Self-Expanding Perception) and Phase 31 (Metabolic
Regulation) — *not* a replacement for either.
**Detector core (external, reference):** `~/Projects/tsle/standalone/stream_detect_mcu.c` — a ~1 KB
self-contained model-of-normalcy detector, validated on Cortex-M4F (`tsle/docs/NORMALCY_BENCHMARK.md`:
4/4 documented IMS bearing faults, 0 false alarms in healthy operation). **That validation is FLOAT on a
chip with an FPU; reflex-os requires a fixed-point port — see §4.**

### Changes from v1 (why this is v2)
The v1 red-team (against the actual `components/goose/` code) found three design errors, all fixed here:
1. **v1 used the float core. The substrate is `int8_t`/ternary and the primary target (ESP32-C6, RV32IMAC)
   has no FPU** → v2 specifies a **fixed-point (Q16.16) integer port**; the M4F float numbers do **not** transfer.
2. **v1 raised the global `sys.ai.pain` cell. Code: under `sys.ai.pain` *all* Hebbian counters decay** — one
   sensor fault would erase unrelated learning → v2 uses a **dedicated, region-scoped anomaly signal**, never global pain.
3. **v1 claimed to replace Phase 33's value-changed curiosity. Phase 33 probes arbitrary MMIO `HARDWARE_IN`
   registers; the model-of-normalcy is for physical sensor signals** → v2 reframes Sentinel as a **narrow sense
   over a handful of designated signals**, complementary to the cheap atlas-wide probe, with an explicit
   feature front-end (§3) and a bounded memory budget (§12).

---

## 1. Summary
Sentinel is a purpose-activated **holon** that watches **one designated physical signal** (a sensor feature or
a vital), learns its normal during a known-good enrollment, and turns the **prediction residual** into reflex-os-
native responses: a **ternary cell**, a **scoped curiosity bias** (sustained-but-unconfirmed departure → attend
to *this* signal's region), and a **scoped anomaly response** (confirmed departure → protect *this* region). It
adds a principled "sense of when a monitored signal goes wrong"; it does **not** modify the broad register-probing
curiosity of Phase 33.

## 2. The unifying principle (unchanged)
Curiosity and anomaly are one residual gradient separated by **persistence**: low residual ⇒ quiescent; rising ⇒
attend (curiosity); persisting past confirmation ⇒ fault. reflex-os explores under zero reward, so the residual is
a natural intrinsic-motivation signal — but applied **locally** to the monitored signal, not globally (see §7).

## 3. Architecture & the feature front-end (NEW in v2)
Two layers, at two rates:

```
(A) Feature front-end  — at the SIGNAL's native rate (e.g. kHz ADC for vibration; or a slow vital read)
       raw samples ──► windowed feature (e.g. RMS, or mean+std)  ──► one feature value per window
(B) Sentinel holon     — on the feature cadence (NOT the 1 Hz policy tick unless the signal is slow)
       feature value ──► [ fixed-point Det: frozen normal + CUSUM ] ──► residual ev, accumulator S
                       ──► perception.normalcy.<signal> ∈ {+1,0,−1}
                       ──► scoped curiosity bias (cell==0) / scoped anomaly response (cell==−1)
```

v1 conflated these by feeding *pre-computed per-file RMS* (one value / 10 min). A real deployment must specify the
front-end per signal: **who samples, at what rate, and which feature**. For a slow vital (heap, temp, battery) the
front-end is trivial (read on the 1 Hz tick); for a vibration sensor it is a dedicated kHz acquisition task that
emits a windowed feature, and only the feature stream reaches the holon.

## 4. Detector core — FIXED-POINT (replaces v1's float core)
The reference algorithm (`stream_detect_mcu.c`) must be ported to **integer/fixed-point (Q16.16, `int32_t`)** so it
is (a) native to the `int8_t`/ternary substrate and (b) runs on the FPU-less ESP32-C6. Specifics:
- Features, z-scores, evidence, and CUSUM all in Q16.16; **integer `sqrt`** (e.g. Newton/`__builtin`-free bit method)
  replaces `sqrtf`; the EMA decay constant is a fixed-point constant (no `expf`).
- The threshold percentile uses an integer insertion sort over Q16.16 distances; the level-scale floor and `absmin`
  are fixed-point constants.
- State = `sentinel_det_t` (fixed-point `fm,fsd,mem[N][2],memn,thr,sm,S,init`). With `int16` packed memory this is
  **smaller** than the float `Det`; budget in §12.
- **Mandatory validation (M0):** the fixed-point port must reproduce the validated detection on the bearing data
  (same first-flare sample, same flare count) before any numbers are quoted. Do **not** carry over the M4F
  float footprint/cycle figures — re-measure on the C6.

## 5. Lifecycle (operator-enroll only)
- **ENROLL** — **explicit** `sentinel enroll <signal>` over a known-good window. *(v1's auto-enroll-on-purpose is
  removed: it could silently enroll on a degrading asset and learn the fault as normal.)*
- **LOCK** — the **normal model** (`mem, fm, fsd, thr`) is frozen; this is the `W_f[hidden]=0` separation. Note: the
  *detect state* (`sm`, `S`) is transient and **does** evolve each step — "locked" means no feedback into the model,
  not that the struct is immutable. Persist the frozen-model fields to NVS via a dedicated `sentinel`-blob handler
  (§13), separate from the route-plasticity serializer.
- **DETECT** — per feature value: residual + CUSUM → cell → scoped wiring.

## 6. The ternary cell — `perception.normalcy.<signal>`
| state | meaning | trigger |
|---|---|---|
| **+1** | normal | `ev ≤ thr`, `S ≈ 0` |
| **0** | novel / watching | `ev > thr`, `S < confirm` |
| **−1** | fault | `S ≥ confirm` |
Same shape as `sys.metabolic`; propagates through GOOSE routes; can feed `GOOSE_CELL_NEURON` quorum.

## 7. Wiring — SCOPED (no global pain)
**7.1 Residual → curiosity (scoped).** When `cell == 0`, Sentinel raises attention on **this signal's own
field/region** — biasing probing toward its neighbourhood — *not* hijacking the atlas-wide `goose_supervisor_explore`
heuristic. The cheap value-changed probe (Phase 33) continues unchanged for the rest of the atlas.

**7.2 Residual → anomaly (scoped, NOT `sys.ai.pain`).** When `cell == −1`, Sentinel asserts a **dedicated** anomaly
signal (e.g. `perception.normalcy.<signal> = −1` is itself the signal, optionally a per-region `*.alarm` cell). The
response is **local**: raise an alarm/telemetry event, focus probing on this signal's region, and — if desired —
decay only the `hebbian_counter` of routes **within this signal's holon/field**. It must **not** write the global
`sys.ai.pain` cell, because code shows that decays **all** counters system-wide (a single sensor fault would erase
unrelated learning). Metabolic protection engages only if the monitored signal *is* a hard-constraint vital.
**7.3 Reward coupling.** The residual is the local zero-reward intrinsic drive for *this* signal — escalating to the
scoped anomaly response only on persistence; relaxes back as `S` decays (hysteresis, no oscillation).

## 8. Mesh (Phase 32) — verdict only
Arc the **verdict** — the ternary `perception.normalcy.<signal>` state — as a native `ARC_OP_*` payload
(`int8_t`, Aura-secured). Sharing the **full model** is a future capability: a ~KB blob exceeds one arc and needs
fragmentation/a new op; out of scope for v2.

## 9. Telemetry & viewer
Emit `#T:` lines (`signal, ev, thr, S, cell`) per feature value (fixed-point printed as scaled integers). The Loom
viewer renders residual/CUSUM as scalars and the normalcy cell as a node colour.

## 10. Shell & RBA
`sentinel status` (observer) · `sentinel enroll <signal>` / `sentinel reset` (operator) · `sentinel arc <peer>`
(operator) · `sentinel override <state>` (admin, bench).

## 11. Parameters (a-priori; fixed-point scaled)
`W=12`, `confirm=10`, `relfloor=0.20`, `absmin=1.0`, `pctl=0.99`, `enroll_frac=0.15` — carried from the validated
config, expressed in Q16.16. Per-signal **feature choice** is the one real deployment decision (§13).

## 12. Memory & compute budget
Fixed-point `sentinel_det_t` ≈ 0.5–1 KB per signal (≤ float `Det` 1064 B; `int16` packed memory ~0.5 KB). Budget
for **a small, fixed set of designated signals** (e.g. ≤ 8), not the 12,738-node atlas. Cycle cost must be
**re-measured on the C6** in fixed-point (the M4F ~2,700-cyc float number does not apply).

## 13. Integration points (reflex-os modules)
- `components/goose/include/goose.h` — `perception.normalcy.*` cell ids; Sentinel holon registration.
- `components/sentinel/` (new) — the fixed-point detector + feature front-end; or vendor the ported core here.
- `components/goose/goose_supervisor.c` — **scoped** attention hook (do not alter the global explore heuristic).
- `goose_telemetry.c` — `#T:` schema. `shell/shell.c` — `sentinel` command + RBA.
- NVS — dedicated Sentinel-model blob handler (not the route-plasticity serializer).
- mesh/ARC — verdict arc op. **Do NOT touch `sys.ai.pain`.**

## 14. Scope, caveats, open questions (honest)
- **Fixed-point equivalence is unproven until M0** — the port must reproduce the validated decisions; the float
  numbers are not transferable.
- **C6 has no FPU** (RV32IMAC) — fixed-point is required, not optional. Verify the actual `march` on the target.
- **Feature choice per signal is undecided** — mean/std suits vibration/RMS; vitals and registers need a chosen
  (and possibly different, or no) feature. Sentinel is for **physical signals with a learnable envelope**, not
  arbitrary status registers — that's why it's a narrow designated-signal sense, not an atlas-wide upgrade.
- **Enroll-on-healthy is mandatory and operator-gated.**
- **Model-of-normalcy, not contextual** — sustained-excursion faults; variable operating regimes need route-over-
  regimes (future), per `tsle/docs/lmm_phase_routing_*`.

## 15. Milestones
- **M0** Fixed-point port + **equivalence validation** on bearing data (same first-flare, same count) — gates everything.
- **M1** Detector-as-holon on one designated signal: feature front-end + enroll/lock + emit `perception.normalcy.<signal>`.
- **M2** Scoped residual → curiosity (attend this signal's region; atlas probe untouched).
- **M3** Scoped residual → anomaly response (no global pain); verify a transient relaxes without alarm.
- **M4** NVS persistence of the frozen model (dedicated blob handler).
- **M5** Verdict arc over the mesh.
- **Falsification gates:** a known transient must raise curiosity then relax **without** anomaly; a sustained
  departure must escalate; a healthy soak must hold `+1` with zero anomaly events and **zero global-Hebbian disturbance**.
