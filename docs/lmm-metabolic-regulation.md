# Lincoln Manifold Method: Metabolic Regulation (Phase 31)

---

## Phase 1: RAW — The First Chop

The core idea is seductive: the OS governs itself based on physical constraints. Battery low? Throttle. Overheating? Slow down. Isolated from mesh? Hunt harder. It maps cleanly onto biological metaphors — and that's exactly what worries me.

What scares me: we're adding a meta-regulatory layer on top of a system that was *literally not running its regulation passes* until 48 hours ago. The supervisor task was dormant under the kernel scheduler config. We fixed that. But now we want to add a layer that *modulates the supervisor itself*. If the metabolic computation has a bug — say it evaluates to -1 (surviving) when it shouldn't — it silently shuts down learning, fabrication, and discovery. The system looks alive (shell works, LED works, heartbeat advances) but the substrate is lobotomized. We'd be recreating the exact failure mode we just fixed, but at a higher abstraction layer.

The battery vital is the weakest link. It requires external hardware (voltage divider + ADC). Most dev boards run on USB. So the primary development platform will never exercise the battery path. We'll build it, ship it, and never validate it on real battery hardware unless someone explicitly rigs up a voltage divider. Every other vital (temp, mesh, heap) is software-only and testable on the bench. Battery is the one that matters most in production (battery death = hard shutdown = lost state) and the one we can test least.

Heap pressure as a vital is interesting but has a feedback loop problem. Shadow paging allocates heap. Metabolic regulation blocks shadow paging when heap is tight. But what if the substrate *needs* a shadow cell to resolve a critical route? Now the metabolic system is preventing the substrate from functioning, which could trigger pain signals, which would try to drive learning, which is also suspended... it's a death spiral. The system conserves itself into uselessness.

The mesh health vital has a similar circularity. If we're isolated, we increase discovery. But discovery requires ESP-NOW transmissions, which require WiFi task time and radio bandwidth. In surviving mode, we suspend discovery. So the one mode where we most need to find peers is the mode where we stop looking. That's backwards.

The temperature vital is the most honest. It's the only one where the correct response to stress is genuinely "do less." Thermal throttling is a real thing in every modern processor. The others are more nuanced.

I'm also uncertain about the single `sys.metabolic` derived cell collapsing four vitals into one trit. A board that's thermally stressed but has full battery, good mesh, and plenty of heap gets the same metabolic response as one that's heap-critical with cold silicon. The modulation should probably be per-vital, not a single aggregate. But per-vital modulation means the supervisor needs to understand which operations cost what — radio for mesh, heap for shadow paging, CPU for learning, etc. That's more complex but more honest.

The telemetry event `#T:X` is straightforward. The Loom Viewer integration is trivial — just another scalar. That part doesn't worry me.

What would the naive approach be? Just add the vitals as perception cells and let the existing reward/pain system handle it. If the temp cell goes to -1, the autonomous evaluator sees a mismatch and generates pain. Pain decays Hebbian counters. The substrate naturally de-prioritizes thermal-heavy routes. No explicit metabolic layer needed — the existing machinery handles it implicitly.

But that's slow. Pain-driven decay takes many cycles. Thermal runaway doesn't wait for Hebbian counters to decay. The explicit metabolic override is a circuit breaker — instant response to physical danger. The implicit path (reward/pain) handles long-term adaptation. Both are needed.

### Questions Arising
- Should metabolic modulation be per-vital or a single aggregate?
- How do we prevent the conserve/survive modes from creating death spirals?
- Is the battery vital worth building if we can't validate it on current hardware?
- Should mesh isolation increase discovery (contradicting the survive-mode suspension)?
- Could the existing reward/pain path handle some vitals without explicit gating?
- What happens during the transition between metabolic states — hysteresis or immediate?
- How does purpose interact with metabolic state? Does purpose override survival?

### First Instincts
- The single-aggregate metabolic cell is too coarse. Per-vital gating would be more correct.
- Battery should be stubbed, not fully implemented, until hardware exists.
- Mesh health should have its own logic: isolation increases discovery regardless of other vitals.
- Hysteresis is essential — we can't oscillate between thriving and surviving on every ADC sample.
- Purpose should not override survival. A dying board should not try harder to serve its purpose.

---

## Phase 2: NODES — Identify the Grain

### Node 1: The Lobotomy Risk
The metabolic system can silently disable the substrate's autonomous behavior. This recreates the dormant-scheduler failure mode at a higher layer. The system looks alive but isn't doing its work. We need observable indicators — telemetry must make metabolic state transitions loud and obvious. The Loom Viewer should show metabolic state as a prominent color overlay, not a hidden scalar.
*Why it matters:* Invisible degradation is the worst failure mode in an autonomous system.

### Node 2: Per-Vital vs. Aggregate Gating
A single `sys.metabolic` cell collapses four independent constraints into one trit. This loses information. A heap-critical board with cold silicon gets the same treatment as a thermally stressed board with abundant heap. Per-vital gating is more honest: thermal stress throttles CPU-heavy operations, heap stress blocks allocations, mesh isolation affects radio, battery affects everything.
*Tension with Node 5:* Per-vital gating is more complex. More conditionals in the supervisor. More tuning knobs. More surface area for bugs.

### Node 3: The Mesh Isolation Paradox
When isolated, the natural response is "hunt harder for peers." But surviving mode suspends discovery. The design contradicts itself. Mesh health should have inverted logic: isolation *increases* discovery effort, even at the cost of battery. Finding the swarm is a survival imperative, not a luxury.
*Why it matters:* The design's default behavior for the mesh vital is exactly wrong.

### Node 4: Battery as Vapor
Battery monitoring requires external hardware that doesn't exist on the dev boards. We'll build the code, never validate it, and ship an untested vital that gates the most critical metabolic decisions. The safer path: implement the architecture with temp, mesh, and heap; design the battery interface but gate it behind a hardware-detect flag.
*Why it matters:* Untested code that controls critical system behavior is worse than no code.

### Node 5: Complexity Budget
The design adds a new source file, a new init call, 4 new perception cells, a new shell command, a new telemetry event, and conditional logic threaded through the supervisor, runtime, and atmosphere. This is the largest cross-cutting change since the GOOSE substrate itself. If we're not careful, the metabolic layer becomes a second supervisor that the first supervisor consults on every decision.
*Tension with Node 2:* Per-vital gating makes this worse. Single aggregate makes this simpler but less correct.

### Node 6: Hysteresis and State Transitions
The design specifies immediate transitions between metabolic states. This means a vital cell oscillating near its threshold causes the supervisor to flap between thriving and conserving every 10 seconds. Need hysteresis: stay in a degraded state for N seconds before recovering, require M consecutive healthy readings before upgrading.
*Why it matters:* Without hysteresis, the metabolic system itself becomes a source of instability.

### Node 7: The Circuit-Breaker Argument
The existing reward/pain system can handle *slow* vital degradation — temperature rising over minutes, heap gradually shrinking. But it can't handle emergencies. A thermal spike needs instant throttling, not a multi-cycle Hebbian decay. The metabolic layer is a circuit breaker: fast, coarse, immediate. The reward/pain path is the fine-grained adaptive response.
*Why it matters:* This frames the metabolic system as a safety mechanism, not a replacement for reward/pain.

### Node 8: Purpose vs. Survival
The design doesn't address the interaction between purpose and metabolic state. If purpose is "sensor" and the board is in surviving mode, should it still prioritize sensor routes? Or should survival override purpose? Biological answer: survival always wins. You don't hunt when you're bleeding out. Purpose should be suspended in surviving mode and reduced in conserving mode.
*Tension with Node 1:* Suspending purpose in surviving mode adds another invisible degradation path.

### Node 9: Existing Perception Pattern
`perception.temp.reading` already proves the pattern: a service registers a cell, a periodic read updates its state, the supervisor and fabric can observe it. The metabolic system doesn't need new infrastructure — it needs new instances of an existing pattern plus a computation pass.
*Why it matters:* This is much less novel than it appears. The hard part is the policy, not the plumbing.

### Node 10: Testing Without Stress
How do you test thermal throttling without a heat gun? How do you test heap pressure without a synthetic allocator? How do you test mesh isolation without physically unplugging boards? We need shell commands that inject synthetic vital states: `vitals override temp -1` to simulate thermal stress. This makes the metabolic system testable on the bench.
*Why it matters:* Untestable features are unshippable features.

---

## Phase 3: REFLECT — Sharpen the Axe

### Core Insight

The metabolic system is not a single thing. It's two things wearing one coat: a **circuit breaker** (instant, coarse, safety-critical) and a **resource governor** (ongoing, fine-grained, efficiency-oriented). The design conflates them into one "metabolic state" cell. Separating them produces a cleaner architecture and avoids most of the tensions above.

### Resolved Tensions

**Node 2 vs. Node 5 (per-vital vs. aggregate):**
Resolution: Both. The circuit breaker is aggregate — any critical vital triggers immediate protection. The resource governor is per-vital — each vital modulates the operations it directly constrains. The circuit breaker is a single `sys.metabolic` cell that drops to -1 on any emergency. The resource governor reads individual vitals in each sub-pass and adjusts accordingly. The supervisor checks `sys.metabolic` first (cheap, single read) and only consults individual vitals when metabolic >= 0.

**Node 3 (mesh isolation paradox):**
Resolution: Mesh isolation is not a survival threat — it's a connectivity problem. Remove mesh health from the circuit-breaker computation entirely. Instead, the discover sub-pass reads `perception.mesh.health` directly and *increases* its frequency when isolated. Mesh health affects mesh behavior. Thermal health affects CPU behavior. They don't cross.

**Node 4 (battery as vapor):**
Resolution: Ship the battery cell with a detect-or-default pattern. If ADC reads a plausible battery voltage (2.5V–4.5V), the cell is active. If not (USB-powered boards read VCC through the ADC, which is a flat 3.3V with no divider variance), the cell defaults to +1 and stays there. No code is untested — the default path exercises the "battery healthy" branch, which is the only branch USB-powered boards will ever take.

**Node 8 (purpose vs. survival):**
Resolution: Purpose is *not* suspended. Purpose tells the circuit breaker what's essential. In surviving mode, *only purpose-matching routes continue evaluating*. Everything else stops. Purpose becomes more important under stress, not less — it's the triage criterion that decides what survives the throttle.

### Hidden Assumptions Challenged

1. **"All vitals are equally important."** False. Battery and heap are hard constraints (system crashes if violated). Temperature and mesh are soft constraints (system degrades but doesn't crash). The circuit breaker should weight hard constraints more heavily.

2. **"Metabolic state transitions should be instantaneous."** False. Recovery needs hysteresis (Node 6). Degradation should be instant (safety) but recovery should be gradual (stability). Enter surviving immediately; exit surviving only after 30 seconds of stability.

3. **"The metabolic layer is separate from the existing learning system."** Partially false. The metabolic state itself should be visible to Hebbian learning. Routes that perform well under stress should be reinforced. The substrate should *learn to be efficient* over time, not just be told to throttle.

### What I Now Understand

The metabolic system has three layers:
1. **Perception**: vital cells reading hardware/software state (the easy part — proven pattern)
2. **Circuit breaker**: single-cell aggregate, instant degradation, hysteretic recovery, gates the supervisor's top-level tick (the safety part)
3. **Resource governance**: per-vital reads in each sub-pass, modulating specific operations based on the vital they depend on (the nuance part)

The circuit breaker is small and critical. Build it first. The resource governance is larger and can be layered in incrementally — each sub-pass gets its own vital-awareness over time.

### Remaining Questions
- What's the right hysteresis window for recovery? 30 seconds feels reasonable but is untested.
- Should the circuit breaker also reduce the pulse rhythm (10Hz → 5Hz), or just gate sub-passes?
- How should `#T:X` represent the two-layer model? One event or two?

---

## Phase 4: SYNTHESIZE — The Clean Cut

### Architecture

Two-layer metabolic system:

**Layer 1 — Circuit Breaker** (`sys.metabolic`):
- Single `GOOSE_CELL_SYSTEM_ONLY` cell, updated at 1Hz
- Value: `+1` (thriving) / `0` (conserving) / `-1` (surviving)
- Triggers on: any hard-constraint vital (battery, heap) at -1, OR two+ soft-constraint vitals (temp, mesh) at -1
- Recovery hysteresis: 30 seconds of all-clear before upgrading from surviving → conserving → thriving
- Degradation: immediate on threshold breach
- In surviving mode: only purpose-matching routes evaluate; all non-essential sub-passes suspended

**Layer 2 — Resource Governance** (per-vital, per-sub-pass):
- `discover_sync` reads `perception.mesh.health` → isolated increases frequency, connected decreases
- `learn_sync` reads `perception.temp.reading` → warm halves learning rate
- `goose_fabric_alloc_cell` reads `perception.heap.pressure` → tight blocks shadow paging
- No single governor controls everything — each sub-pass is responsible for its own vital

### Key Decisions

1. **Two layers, not one.** Circuit breaker for safety, resource governance for efficiency. Because: the tensions between aggregate and per-vital resolve when you recognize they serve different purposes.

2. **Mesh isolation increases discovery.** Because: isolation is a connectivity problem, not a resource problem. The board should search harder, not less.

3. **Purpose is the triage criterion under stress.** In surviving mode, purpose-matching routes survive; non-matching routes are suspended. Because: purpose tells the system what matters most, and that information becomes more valuable under constraint, not less.

4. **Battery defaults to +1 on USB-powered boards.** Detect via ADC variance: a real battery has voltage fluctuation; USB is flat. Because: untested code that controls critical behavior is worse than no code.

5. **Hysteresis on recovery, not degradation.** Drop to surviving instantly; return to thriving gradually. Because: safety requires fast reaction, but stability requires slow recovery.

6. **Vital overrides for testing.** `vitals override <vital> <state>` injects synthetic values. Because: untestable features are unshippable features.

### Implementation Spec

**Phase A — Perception (vitals on the fabric):**
- Register `perception.power.battery`, `perception.mesh.health`, `perception.heap.pressure` in `goose_metabolic_init()`
- Battery: ADC read with detect-or-default (USB = permanent +1)
- Mesh: delta of `rx_sync + rx_discover` over 30s window
- Heap: `heap_caps_get_free_size(MALLOC_CAP_DEFAULT)` with 8K/16K thresholds
- 10-second update interval for all three (temperature already updates at 5s)
- `vitals` shell command displays all four vitals + metabolic state
- `vitals override <vital> <state>` for bench testing

**Phase B — Circuit Breaker:**
- `goose_metabolic_sync()` runs as 1Hz sub-pass in supervisor pulse
- Reads all four vital cells, computes metabolic state with hard/soft weighting
- Writes `sys.metabolic` cell
- Hysteresis: `metabolic_stable_ticks` counter, requires 30 consecutive stable ticks (30s at 1Hz) before upgrading
- In surviving mode: `goose_supervisor_pulse()` skips learn, weave, swarm, discover, snap, watchdog sub-passes; only equilibrium check runs, and only for purpose-matching fields
- Telemetry: `#T:X,<metabolic>,<temp>,<batt>,<mesh>,<heap>` at 1Hz

**Phase C — Resource Governance (incremental):**
- `discover_sync`: if `perception.mesh.health == -1`, divide discover interval by 2 (faster). If `+1`, multiply by 2 (slower).
- `learn_sync`: if `perception.temp.reading == -1`, double the learn divisor (slower).
- `goose_fabric_alloc_cell`: if `perception.heap.pressure == -1`, return NULL for non-system allocations (block shadow paging).
- Each governance rule is a single `if` statement reading one cell. No centralized governor.

### Files

| File | Change |
|------|--------|
| `components/goose/goose_metabolic.c` | **NEW** — vital registration, ADC reads, metabolic computation, hysteresis, `vitals` data |
| `components/goose/include/goose_metabolic.h` | **NEW** — public API |
| `components/goose/goose_supervisor.c` | Add `metabolic_sync` sub-pass; gate sub-passes on circuit breaker |
| `components/goose/goose_runtime.c` | Gate `goose_fabric_alloc_cell` on heap vital |
| `components/goose/goose_atmosphere.c` | Modulate discover interval on mesh vital |
| `components/goose/goose_telemetry.c` | Add `goose_telem_metabolic()` |
| `components/goose/include/goose_telemetry.h` | Declare `goose_telem_metabolic()` |
| `components/goose/CMakeLists.txt` | Add `goose_metabolic.c` |
| `main/main.c` | Call `goose_metabolic_init()` after atlas weave |
| `shell/shell.c` | Add `vitals` and `vitals override` commands |
| `include/reflex_tuning.h` | Metabolic thresholds, hysteresis window, vital update intervals |
| `tools/loom_viewer.py` | Parse `#T:X`, add metabolic + vital scalar panels |

### Success Criteria

- [ ] `vitals` command shows all four vitals with correct ternary states on a USB-powered board
- [ ] `vitals override temp -1` → metabolic drops to conserving within 1 second
- [ ] `vitals override temp -1` + `vitals override heap -1` → metabolic drops to surviving
- [ ] In surviving mode, `#T:H` (Hebbian) and `#T:W` (weave) events stop; `#T:B` continues
- [ ] Recovery: clear overrides → metabolic stays surviving for 30s → then recovers to thriving
- [ ] `telemetry on` → `#T:X,1,0,1,1,1` at 1Hz (thriving, temp normal, battery full, mesh connected, heap comfortable)
- [ ] Mesh isolation (power off all peers) → discover heartbeat frequency increases observably
- [ ] Board with no battery hardware boots normally with metabolic = thriving
- [ ] All 26 C tests + 9 TASM tests still pass
- [ ] Pre-metabolic behavior is byte-identical when all vitals are at +1
