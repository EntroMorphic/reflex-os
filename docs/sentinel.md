# Sentinel — Model-of-Normalcy Sense (Integration Spec)

**Status:** Proposed (draft for review). **Slots as:** the next perception phase, building on Phase 33
(Self-Expanding Perception) and Phase 31 (Metabolic Regulation).
**Detector core (external):** `~/Projects/tsle/standalone/stream_detect_mcu.c` — a ~1 KB float,
self-contained (no libm/newlib) model-of-normalcy detector, hardware-validated on Cortex-M4F
(see `tsle/docs/NORMALCY_BENCHMARK.md`: 4/4 documented IMS bearing faults, 0 false alarms in healthy
operation; bit-identical float-on-silicon result).

---

## 1. Summary
The Sentinel is a purpose-activated **holon** that wraps a learned model-of-normalcy and turns its
**prediction residual** into two signals reflex-os already runs on: **curiosity** (a sustained-but-
unconfirmed departure → attend) and **pain** (a confirmed sustained departure → protect). It upgrades
Phase 33's novelty trigger from *"a register value changed = hot"* to *"a value departed from learned
normal and the departure persists = hot,"* and gives the OS a principled "sense of when something is wrong."

## 2. The unifying principle
Curiosity and anomaly are the same gradient — prediction error / surprise — separated only by
**persistence**. reflex-os explores under **zero reward**; the residual is the natural intrinsic-motivation
signal: low everywhere ⇒ the world matches the model ⇒ quiescent; rising ⇒ something to learn (curiosity);
*persisting past confirmation* ⇒ a fault (pain). One signal, two valences, one dividing line.

## 3. Architecture
A holon (`sentinel`) on the 1 Hz supervisor policy tick (or a dedicated faster sensor task). It owns a
detector instance, binds to one signal source, emits one ternary cell, and wires its residual into the
existing explore-bias and `sys.ai.pain` paths. No new substrate — a new sense.

```
signal source (HARDWARE_IN reg via shadow atlas / vital cell / derived feature)
      │  read 1 value per tick
      ▼
  [ Det: frozen normal model + CUSUM ]          ← the model-of-normalcy core (≈1 KB)
      │  residual ev, accumulator S
      ▼
  perception.normalcy.<signal>  =  +1 normal / 0 novel-watch / −1 fault   (ternary GOOSE cell)
      │                         │
      │ cell==0 → bias explore  │ cell==−1 → raise sys.ai.pain
      ▼                         ▼
  goose_supervisor_explore   pain responses (Hebbian decay, probe-rate ×2, metabolic protect, mesh arc)
```

## 4. Detector core (self-contained recap)
Per signal: feature `feat()` (e.g. windowed mean+std for vibration; configurable per signal), z-scored
against an enrollment-derived scale with a **level-scale floor** (so a quiet baseline isn't a hair-trigger);
**evidence** = nearest-neighbour distance to a frozen episodic memory of normal snapshots; **CUSUM**
relax-vs-persist accumulator (`S += clamp(ev/thr − 1, −1, +1)`, fire at `S ≥ confirm`); threshold
**enrollment self-calibrated** (percentile + spread, floored by `absmin`). State = `Det`
(`fm,fsd,mem[128][2],memn,thr,sm,S,init` ≈ 1064 B float). Deterministic; ~2,700 CPU cycles/sample.

## 5. Lifecycle (two-phase, mirrors reflex-os)
- **ENROLL** — on first activation under a stable purpose (= a known-good commissioning window), or via
  `sentinel enroll <signal>`. Accumulate; compute level-scale, memory, threshold; then **LOCK**.
- **LOCK = W_f[hidden] = 0** — the nominal model is frozen; the detect path reads evidence against it with
  **no feedback** from detection back into the model. Persist `Det` to NVS via the snapshot infrastructure
  (Phase 29, `goose_snapshot_*` pattern) so enrollment survives reboot.
- **DETECT** — per tick: compute `ev`, update `S`, set the cell, drive the wiring.

## 6. The ternary cell — `perception.normalcy.<signal>`
| state | meaning | trigger |
|---|---|---|
| **+1** | normal | `ev ≤ thr` and `S ≈ 0` |
| **0** | novel / watching | `ev > thr` but `S < confirm` (the curiosity zone) |
| **−1** | fault | `S ≥ confirm` (the pain zone) |
Same shape as `sys.metabolic` (thriving/conserving/surviving); propagates through GOOSE routes like any cell,
and can feed `GOOSE_CELL_NEURON` quorum aggregation.

## 7. Wiring
**7.1 Residual → curiosity.** When `cell == 0`, the Sentinel nominates its source register/region as an
explore candidate, *replacing* Phase 33's raw value-changed heuristic with residual-hotness. Integration:
`goose_supervisor_explore()` gains a candidate-priority hook; Sentinel feeds it "registers whose residual is
elevated-and-rising." Hebbian/eviction still arbitrate relevance downstream — but the *attractor* is now
"where the world is departing from what I learned," not "where a timer ticked."

**7.2 Residual → pain.** When `cell == −1`, raise `sys.ai.pain`. Existing pain responses then fire for free:
`hebbian_counter` decays off the faulty routes (don't reinforce a broken world), probe rate doubles (focus
perception on the anomaly), and metabolic protection engages if the fault is a hard-constraint vital. When the
residual relaxes back under threshold (`S` decays), pain subsides — consistent with metabolic **hysteretic
recovery**, so there's no oscillation on transients.

**7.3 Reward coupling.** The residual *is* the zero-reward intrinsic drive: with no extrinsic reward, the
Sentinel's `0`-state drives exploration toward novelty; escalation to `−1` only on persistence.

## 8. Mesh (Phase 32 tie-in)
A locked normal model is a shareable **topological delta**: a node that enrolled "normal for pump-A" can arc
the model — or just the fault verdict — to peers monitoring similar assets (`ARC_OP_*`, Aura-secured). Swarm-
wide condition monitoring with **no raw-data transfer**, matching both reflex-os's mesh-learning vision and the
detector's no-cloud design.

## 9. Telemetry & viewer
Emit `#T:`-prefixed lines (`signal, ev, thr, S, cell`) on each tick. The Loom viewer renders the residual and
CUSUM as scalar time series and the normalcy cell as a node colour — the same play-by-play already demonstrated
on serial (`healthy → S climbs → FAULT CONFIRMED`), now inside the substrate visualization.

## 10. Shell & RBA
| command | role |
|---|---|
| `sentinel status` | observer |
| `sentinel enroll <signal>` / `sentinel reset` | operator |
| `sentinel arc <peer>` | operator |
| `sentinel override <state>` (bench) | admin |

## 11. Parameters (a-priori sensitivity priors — not fitted to data)
`W=12` (feature window), `confirm=10` (CUSUM bound), `relfloor=0.20` (min meaningful change vs signal scale),
`absmin=1.0` (absolute excursion floor), `pctl=0.99` (enrollment threshold percentile), `enroll_frac=0.15`.
These transfer from the validated host/silicon config; per-signal feature choice is the one tunable that needs
a deployment decision (see §13).

## 12. Integration points (reflex-os modules touched)
- `components/goose/include/goose.h` — cell id `perception.normalcy.*`, Sentinel holon registration.
- `components/goose/goose_supervisor.c` — tick hook; explore-candidate priority hook (§7.1).
- `components/goose/goose_metabolic.c` — pattern reuse (vital-like cell + hysteresis); optional vital binding.
- `goose_telemetry.c` — `#T:` schema for residual/S/cell.
- `shell/shell.c` — `sentinel` command + RBA classification.
- mesh/ARC layer — `arc_anomaly` / model-delta op.
- NVS/snapshot — `Det` serialize/restore alongside route plasticity.
- **import:** `tsle/standalone/stream_detect_mcu.c` core (vendored as `components/sentinel/` or a thin port).

## 13. Scope, caveats, open questions (honest)
- **Feature choice per signal.** Windowed mean+std suits vibration/RMS-like signals. For raw MMIO registers or
  vitals, the right feature (level? rate? entropy?) is undecided — the first integration question.
- **Enroll-on-healthy is mandatory.** If enrollment captures a degrading asset, the fault is learned as normal
  (same limit as any learn-from-normal method). Enrollment window selection is a deployment decision.
- **It is a model-of-normalcy** (sustained-excursion / global-departure faults), **not** a contextual/
  seasonal detector — the boundary established in `tsle/docs/lmm_phase_routing_*` and `NORMALCY_BENCHMARK.md`.
  Variable operating regimes → route-over-regimes (future), not a single locked envelope.
- **Float-equivalence** is validated on-silicon for one signal class (bearing). New signals should re-confirm
  the float core matches the host before quoting numbers.
- **Single-signal** per holon instance to start; multi-channel = multiple Sentinel holons.

## 14. Milestones
- **M1** Detector-as-holon: bind a signal, enroll/lock, emit `perception.normalcy.<signal>` (no wiring).
- **M2** Residual → curiosity: explore-candidate hook; show novelty attends before fault.
- **M3** Residual → pain: `sys.ai.pain` on confirm; verify Hebbian decay + probe-rate + hysteretic relax.
- **M4** NVS persistence: enroll-once survives reboot.
- **M5** Mesh: arc the fault verdict / model delta to a peer.
- **Falsification gates:** a known transient must raise curiosity then relax **without** pain; a sustained
  departure must escalate to pain; healthy operation must hold `+1` with zero pain events over a long soak.
