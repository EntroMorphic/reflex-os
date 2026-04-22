# Phase 31: Metabolic Regulation — Design Document

## Intent

The substrate currently learns from reward signals and autonomously fabricates routes, but it does so unconditionally — full speed regardless of physical constraints. Metabolic Regulation adds a second axis of self-governance: the substrate modulates its own behavior based on its own vitals.

When battery is low, the OS inhibits high-cost operations. When thermal headroom is exhausted, it throttles. When mesh connectivity drops, it hunts harder for peers. When heap pressure rises, it stops allocating shadow cells. The machine preserves itself without being told.

---

## Architecture

### Vital Cells

New perception cells projected onto the GOOSE fabric under the `perception.*` namespace:

| Cell | Source | Ternary Mapping | Update Rate |
|------|--------|----------------|-------------|
| `perception.temp.reading` | **Exists** — die temperature sensor | cold (<40C) / normal / warm (>55C) | 5s (existing) |
| `perception.power.battery` | ADC on GPIO (voltage divider) | low (<3.3V) / normal / full (>4.0V) | 10s |
| `perception.mesh.health` | `mesh_stats` rx counters | isolated (0 rx in 30s) / sparse / connected | 10s |
| `perception.heap.pressure` | `xPortGetFreeHeapSize()` or equivalent | critical (<8KB) / tight (<16KB) / comfortable | 10s |

Each vital cell is a standard `GOOSE_CELL_HARDWARE_IN` with a ternary state that the supervisor can read like any other cell. No new cell types needed.

### Metabolic State

A single derived cell `sys.metabolic` computed from the vital cells:

| Vital States | Metabolic State | Meaning |
|-------------|----------------|---------|
| All normal/positive | `+1` (thriving) | Full autonomy |
| Any one stressed | `0` (conserving) | Reduce non-essential activity |
| Two+ stressed or any critical | `-1` (surviving) | Minimum viable operation |

The metabolic state is computed by the new `metabolic_sync` sub-pass in the supervisor, running at 1Hz alongside eval, learn, and swarm.

### Supervisor Modulation

`metabolic_sync` reads `sys.metabolic` and adjusts the supervisor's own behavior:

**Thriving (+1) — no modulation:**
- All sub-passes run at normal cadence
- Hebbian learning at full rate
- Autonomous fabrication enabled
- Shadow paging on demand
- Discovery heartbeat at normal interval

**Conserving (0) — reduce non-essential:**
- Hebbian learning rate halved (double the divisor)
- Autonomous fabrication skips non-purpose-matching sinks
- Shadow paging deferred (existing cells only)
- Discovery heartbeat interval doubled
- Mesh sync arcs throttled to 50%

**Surviving (-1) — minimum viable:**
- Hebbian learning suspended
- Autonomous fabrication suspended
- Shadow paging blocked
- Discovery heartbeat suspended
- Only direct hardware routes and existing learned routes evaluate
- Pulse rhythm reduced to 5Hz (skip every other tick)

### Telemetry

New telemetry event:

```
#T:X,<metabolic_state>,<temp>,<battery>,<mesh>,<heap>
```

Emitted at 1Hz from `metabolic_sync`. The Loom Viewer gets a new scalar panel for metabolic state and individual vital time series.

---

## Implementation

### New Files

| File | Purpose |
|------|---------|
| `components/goose/goose_metabolic.c` | Vital cell registration, ADC reading, metabolic state computation, supervisor modulation |
| `components/goose/include/goose_metabolic.h` | Public API: `goose_metabolic_init()`, `goose_metabolic_sync()`, `goose_metabolic_get_state()` |

### Modified Files

| File | Change |
|------|--------|
| `components/goose/goose_supervisor.c` | Add `metabolic_sync` 1Hz sub-pass; gate learn/weave/discover on metabolic state |
| `components/goose/goose_runtime.c` | Gate shadow paging on metabolic state |
| `components/goose/goose_atmosphere.c` | Gate discover heartbeat and sync arc rate on metabolic state |
| `components/goose/goose_telemetry.c` | Add `goose_telem_metabolic()` emitter |
| `components/goose/include/goose_telemetry.h` | Declare `goose_telem_metabolic()` |
| `components/goose/CMakeLists.txt` | Add `goose_metabolic.c` to SRCS |
| `main/main.c` | Call `goose_metabolic_init()` in boot Stage 4 |
| `shell/shell.c` | Add `vitals` shell command |
| `include/reflex_tuning.h` | Add metabolic thresholds and divisor overrides |
| `tools/loom_viewer.py` | Parse `#T:X` events, add metabolic scalar panel |

### Vital Cell Implementation

**Battery (ADC):**
- ESP32-C6 has a 12-bit SAR ADC on GPIO pins
- Use ADC1 channel on an available GPIO (e.g., GPIO 0 or 1)
- Voltage divider: 2x 100K resistors, midpoint to ADC pin, full range to battery terminals
- Raw ADC → voltage via calibration: `V = (adc_raw / 4095) * 3.3 * 2`
- Ternary mapping: <3.3V = -1 (low), 3.3-4.0V = 0 (normal), >4.0V = +1 (full)
- If no battery hardware is detected (USB-powered), cell state stays `+1` (full) permanently

**Mesh Health:**
- Read `goose_atmosphere_get_stats()` → compare `rx_sync + rx_discover` against previous snapshot
- If delta == 0 over 30 seconds → isolated (-1)
- If delta > 0 but < 5 → sparse (0)
- If delta >= 5 → connected (+1)

**Heap Pressure:**
- `heap_caps_get_free_size(MALLOC_CAP_DEFAULT)` on ESP-IDF
- < 8192 bytes → critical (-1)
- < 16384 bytes → tight (0)
- >= 16384 bytes → comfortable (+1)

**Temperature:**
- Already exists as `perception.temp.reading`. Metabolic sync reads it directly.

### Metabolic Computation

```c
int8_t goose_metabolic_compute(void) {
    int8_t temp  = perception_temp->state;     // existing cell
    int8_t batt  = perception_battery->state;
    int8_t mesh  = perception_mesh->state;
    int8_t heap  = perception_heap->state;

    int stressed = (temp == -1) + (batt == -1) + (mesh == -1) + (heap == -1);
    int critical = (batt == -1) + (heap == -1); // battery and heap are hard constraints

    if (critical >= 1 || stressed >= 2) return -1; // surviving
    if (stressed >= 1) return 0;                    // conserving
    return 1;                                       // thriving
}
```

### Supervisor Gating

In `goose_supervisor_pulse()`, the metabolic state modulates existing sub-passes:

```c
int8_t metabolic = goose_metabolic_get_state();

// Pulse rhythm reduction in surviving mode
if (metabolic == -1 && (pulse_count & 1)) {
    // Skip odd ticks — reduce from 10Hz to 5Hz
    return REFLEX_OK;
}

// Existing sub-passes with metabolic gating:
if (metabolic >= 0) {  // conserving or thriving
    if (weave_div++ >= weave_divisor) {
        goose_supervisor_weave_sync();
        weave_div = 0;
    }
}

if (metabolic > 0) {  // thriving only at full rate
    if (learn_div++ >= learn_divisor) {
        goose_supervisor_learn_sync();
        learn_div = 0;
    }
} else if (metabolic == 0) {  // conserving: half rate
    if (learn_div++ >= learn_divisor * 2) {
        goose_supervisor_learn_sync();
        learn_div = 0;
    }
}
// metabolic == -1: learning suspended entirely
```

### Shell Command

```
reflex> vitals
temp=52.3C(0) battery=3.8V(0) mesh=connected(1) heap=42KB(1) metabolic=thriving(1)
```

### Hardware Requirements

- **Battery monitoring**: one GPIO with ADC capability, two 100K resistors as voltage divider. Optional — if absent, battery vital stays +1.
- **All other vitals**: software-only, no additional hardware.

---

## Constraints

1. **No new cell types.** Vitals are `GOOSE_CELL_HARDWARE_IN`. Metabolic state is `GOOSE_CELL_SYSTEM_ONLY`.
2. **No new task types.** `metabolic_sync` runs as a 1Hz divider inside the existing supervisor pulse — no new FreeRTOS task.
3. **Battery is optional.** USB-powered boards default to `+1`. The metabolic system degrades gracefully to temp + mesh + heap.
4. **Thresholds are tunable.** All voltage, heap, and timing thresholds go in `reflex_tuning.h` with `#ifndef` guards for Kconfig override.
5. **Backward compatible.** With no battery hardware, the default metabolic state is `+1` (thriving) — the system behaves identically to today.

---

## Verification

1. `idf.py build` — compiles clean
2. `make test` — all existing tests pass
3. Flash → `vitals` → shows temp, heap, mesh vitals with correct ternary states
4. `telemetry on` → `#T:X,1,...` appears at 1Hz
5. **Thermal stress test**: heat the board with a heat gun → temp cell goes to -1 → metabolic drops to conserving/surviving → observe reduced telemetry rate and suspended learning
6. **Heap stress test**: allocate many shadow cells → heap drops → metabolic responds
7. **Mesh isolation test**: power off all peers → mesh health drops to -1 → metabolic responds
8. **Recovery test**: remove stress → metabolic returns to thriving → full autonomy resumes

---

## Success Criteria

- The substrate visibly throttles itself under stress without any user intervention
- Recovery is automatic and complete when stress is removed
- The Loom Viewer shows metabolic state as a time series correlating with vital changes
- USB-powered boards with no battery hardware behave identically to the pre-metabolic firmware
- Zero new FreeRTOS tasks, zero new cell types, zero new dependencies
