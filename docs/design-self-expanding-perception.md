# Phase 33: Self-Expanding Perception

## The Problem

Reflex OS has 12,738 addressable hardware entries. At boot, 104 are pre-woven into the active Loom. The rest sit dormant in the shadow atlas — invisible to the substrate unless a human types `goonies read`. The OS can feel pain (purpose unfulfilled), but it cannot look for new ways to perceive the world. A nervous system that cannot grow new nerve endings.

## The Vision

Under sustained pain, the supervisor searches the shadow atlas for hardware that correlates with purpose fulfillment. It pages in candidates, samples them, evaluates whether they help, and either weaves them into routes or lets them evict. The OS literally expands its own perception — discovering that a timer register tracks a signal relevant to its goal, or that an ADC channel provides information it didn't know it needed.

## Constraints

- **256-slot Loom ceiling.** 104 pre-woven + ~20 from holons = ~130 active cells. Budget for exploration: ~30-50 slots before the Loom gets crowded and eviction pressure rises.
- **Metabolic awareness.** Do NOT explore when surviving (heap critical, battery dying). Only explore when conserving or thriving with sustained pain.
- **No polling.** Exploration happens inside the supervisor pulse (1Hz), not on a separate task.
- **Eviction is the forget mechanism.** Shadow-paged cells are VIRTUAL — if they don't prove useful (no Hebbian reinforcement), round-robin eviction reclaims them naturally.

## Architecture

### Trigger: Sustained Pain

Not every pain tick triggers exploration. Pain must persist for `REFLEX_EXPLORE_PAIN_THRESHOLD` consecutive ticks (default: 5 seconds) before the supervisor begins searching. This prevents exploration on transient fluctuations.

```
pain_ticks == 0    → no exploration
pain_ticks < 5     → pain is transient, wait
pain_ticks >= 5    → begin exploration
pain_ticks >= 30   → widen search (see Search Strategy)
```

State variable: `s_explore_pain_ticks` (uint16_t), incremented when pain is active, reset to 0 when pain clears.

### Search Strategy: Domain-Guided Shadow Walk

The explorer doesn't randomly sample the atlas. It uses the active purpose to guide the search.

**Step 1: Domain filter.** If purpose is "led", the relevant zones are `agency` (actuators) and `perception` (sensors). The explorer walks the shadow atlas looking for entries whose zone matches the purpose domain. Domain-to-zone mapping:

| Purpose keyword | Primary zones | Rationale |
|----------------|---------------|-----------|
| led, light, display | agency, perception | Actuators + light sensors |
| temp, thermal, heat | perception | Temperature sensing |
| mesh, network, comm | comm, radio | Communication peripherals |
| motor, pwm, drive | agency | Motor/PWM actuators |
| (fallback) | perception, agency | Sensor-actuator exploration |

The fallback is important: if the purpose doesn't match a known keyword, explore perception (new senses) and agency (new actuators) — never sys or logic (those are infrastructure, not purpose-relevant).

**Step 2: Candidate selection.** Within the filtered zone, the explorer picks candidates that:
- Are NOT already in the live Loom (skip allocated cells)
- Are register-level entries (mask == 0xFFFFFFFF), not bit fields (fields are woven as routes later if the register proves useful)
- Are HARDWARE_IN or HARDWARE_OUT type (skip SYSTEM_ONLY — those are protected infrastructure)

**Step 3: Budget.** Page in at most `REFLEX_EXPLORE_BUDGET` candidates per pulse (default: 2). This is the exploration rate — slow enough to not thrash the Loom, fast enough to find useful hardware within 30 seconds of searching.

**Step 4: Widening.** After 30 seconds of sustained pain with no reward, widen the search beyond purpose-domain zones — try comm zone peripherals, then fall back to any HARDWARE_IN/OUT entry. Desperation search.

### Evaluation: Correlation with Purpose

A paged-in exploration cell enters the Loom as a VIRTUAL cell with no routes. The supervisor's existing `weave_sync` will attempt to connect it to need cells if the name matches the purpose domain. Once connected:

- If the cell's hardware state changes AND that change correlates with reward (pain decreasing), Hebbian learning strengthens the route.
- If the cell is static or uncorrelated, Hebbian learning never reinforces it, the counter stays at 0, and the cell remains eviction-eligible.

No new evaluation logic needed — the existing Hebbian learning + eviction system IS the evaluation. Useful cells survive; useless ones are forgotten.

### The Exploration Cycle

```
Supervisor pulse (1Hz)
  └─ goose_supervisor_explore()
       ├─ if metabolic_state == SURVIVING → return (no budget)
       ├─ if pain_ticks < EXPLORE_PAIN_THRESHOLD → return
       ├─ walk shadow_map[] from s_explore_cursor
       │    ├─ skip if zone doesn't match purpose domain
       │    ├─ skip if mask != 0xFFFFFFFF (bit field)
       │    ├─ skip if type == SYSTEM_ONLY
       │    ├─ skip if already allocated in Loom
       │    ├─ page in via goose_fabric_alloc_cell + goose_fabric_set_agency
       │    ├─ decrement budget
       │    └─ if budget == 0 → break
       ├─ advance s_explore_cursor (wraps at shadow_map_count)
       ├─ if pain_ticks >= EXPLORE_WIDEN_THRESHOLD → expand zone filter
       └─ TELEM_IF(goose_telem_explore(name, zone))
```

### Cursor and State

- `s_explore_cursor` (uint16_t): index into `shadow_map[]`. Advances by 1 per examined entry. Wraps at `shadow_map_count`. Persists across pulses so we don't re-examine the same entries.
- `s_explore_pain_ticks` (uint16_t): consecutive pain ticks. Reset on reward.
- `s_explore_allocated` (uint16_t): total cells paged in by exploration this session. For telemetry and diagnostics.

### Telemetry

New telemetry event:
```
#T:D,<name>,<zone>     — Discovery: exploration paged in a new cell
```

The Loom Viewer can highlight exploration cells distinctly — these are the OS reaching out to touch new hardware.

### Shell Observability

Extend `status` output to show exploration state:
```
explore: active (pain=12 ticks, cursor=4521/12738, paged=7)
```

Or when idle:
```
explore: idle (no sustained pain)
```

## Files to Create

None. This is a sub-pass of the existing supervisor.

## Files to Modify

| File | Change |
|------|--------|
| `components/goose/goose_supervisor.c` | Add `goose_supervisor_explore()` sub-pass + state variables |
| `components/goose/include/goose_telemetry.h` | Add `goose_telem_explore()` declaration |
| `components/goose/goose_telemetry.c` | Add `goose_telem_explore()` implementation |
| `include/reflex_tuning.h` | Add `REFLEX_EXPLORE_PAIN_THRESHOLD`, `REFLEX_EXPLORE_BUDGET`, `REFLEX_EXPLORE_WIDEN_THRESHOLD` |
| `shell/shell.c` | Add exploration state to `status` output |
| `tools/loom_viewer.py` | Parse `#T:D` events, highlight exploration cells |

## Tuning Constants

| Constant | Default | Rationale |
|----------|---------|-----------|
| `REFLEX_EXPLORE_PAIN_THRESHOLD` | 5 | 5 seconds of pain before exploring |
| `REFLEX_EXPLORE_BUDGET` | 2 | Max cells paged in per 1Hz pulse |
| `REFLEX_EXPLORE_WIDEN_THRESHOLD` | 30 | 30 seconds before widening search |
| `REFLEX_EXPLORE_MAX_ACTIVE` | 40 | Hard cap on exploration cells (protects Loom budget) |

## What This Does NOT Do

- **Does not write to hardware.** Exploration pages in cells and reads their state. It never writes to unexplored registers — that would be dangerous. The Sanctuary Guard still protects writes.
- **Does not bypass metabolic regulation.** Surviving mode blocks exploration entirely. Conserving mode could halve the budget (1 cell per pulse instead of 2).
- **Does not require new data structures.** Exploration cells are normal VIRTUAL cells in the Loom. The only new state is the cursor, pain counter, and allocation counter.
- **Does not persist exploration discoveries.** If the board reboots, exploration starts over. Hebbian snapshots already persist learned routes — if an exploration cell gets reinforced enough to commit a learned orientation, that route survives via `snapshot save`.

## Verification Plan

1. `idf.py build` — compiles with new sub-pass
2. `make test` — existing 26 tests still pass
3. On-device: `purpose set led` → observe `#T:D` events in telemetry after pain threshold
4. On-device: `purpose clear` → exploration stops
5. On-device: under `vitals override heap -1` (surviving) → exploration blocked
6. On-device: verify explored cells appear in `goonies ls` output
7. On-device: verify useless exploration cells get evicted under Loom pressure
8. Loom Viewer: exploration cells visible as distinct node type
