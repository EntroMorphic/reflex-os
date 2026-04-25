# LMM: Self-Expanding Perception (Phase 33)

Applying the Lincoln Manifold Method to the design in `design-self-expanding-perception.md`.

---

## Phase 1: RAW

The idea is seductive: the OS feels pain, searches its own hardware catalog for new senses, and grows. But I have doubts.

First, what does "correlates with purpose fulfillment" actually mean at the hardware level? If purpose is "led" and the OS pages in `perception.apb_saradc.ctrl`, what happens? The ADC control register has no causal relationship to LED output. The OS would page it in, read a static value, never get Hebbian reinforcement, and evict it. That's correct behavior — but it means the OS will churn through hundreds of irrelevant registers before finding anything useful. And "useful" is the problem: on a bare board with no external sensors, there might be NOTHING useful to discover. The temperature sensor is already woven. What else is there?

Second, the domain-to-zone mapping feels forced. Why would "led" map to perception? The LED is in agency. If you're trying to control an LED, you don't need new senses — you need your actuator to work. The exploration design assumes that pain comes from missing information, but pain can come from many sources: broken routes, wrong purpose declaration, hardware not connected, metabolic stress. Exploration only helps if the problem is "I don't know about hardware that exists."

Third, the 256-slot Loom ceiling is real. 104 pre-woven + holons + exploration = pressure. The design says "budget 30-50 slots" but that's a lot. If exploration pages in 2 cells per second for 30 seconds, that's 60 cells — a quarter of the Loom. Even if eviction handles cleanup, the churn affects cache locality and the supervisor's route-walk time.

Fourth, I keep wondering: who benefits? On a bare dev board, the interesting hardware is already woven. On a board with external sensors wired to GPIO/ADC/I2C, those sensors are accessed through specific pins that the user would configure via purpose or holons. The scenario where autonomous exploration discovers something useful requires hardware that's connected, relevant to the purpose, and not already in the Loom. That's a narrow window.

Fifth, there's something beautiful about it anyway. The OS reaching out, touching hardware it's never touched, feeling whether it matters. Even if the practical utility is narrow today, it demonstrates something no other embedded OS does: autonomous sensory expansion. And when you connect external sensors — a light sensor to ADC, a vibration sensor to GPIO — the OS could discover them without being told they exist.

The fear: building a feature that looks spectacular in demos but has no real utility until external hardware is connected. The hope: that the mechanism itself is so simple (just alloc + set_agency on shadow entries) that the cost of having it is nearly zero, and the payoff comes when the hardware environment gets richer.

---

## Phase 2: NODES

1. **Exploration is cheap to build.** The mechanism is just `goose_fabric_alloc_cell` + `goose_fabric_set_agency` on shadow entries — both already exist. The new code is a search loop and some state variables. ~80 lines.

2. **The evaluation problem is already solved.** Hebbian learning + eviction = natural selection. No new evaluation logic needed. This is the strongest part of the design.

3. **Domain mapping is the weakest link.** The keyword-to-zone mapping (`"led" → agency, perception`) is a hardcoded heuristic that will be wrong for most purposes. It's trying to solve a semantic problem with a lookup table.

4. **Bare boards have nothing to discover.** Without external sensors, the only perception hardware is temp (already woven), ADC (no input connected), PCNT (no input connected), and timers (already woven). Exploration on a bare board is pure churn.

5. **External sensors are the real use case.** When a light sensor is wired to GPIO 4, exploration could discover that `perception.apb_saradc.ctrl` + `perception.gpio.pin4` provide useful state. But the user has to physically connect the hardware first.

6. **Loom pressure is manageable but real.** 2 cells/sec × 30s = 60 cells. With eviction working correctly, only reinforced cells survive. But during the exploration burst, the Loom is under churn pressure.

7. **The cursor approach is sound.** Walking the sorted shadow atlas with a persistent cursor ensures full coverage without repetition. 12,738 entries at 2/sec = ~106 minutes for a full sweep. That's fine — exploration is a slow background process.

8. **Metabolic gating is essential.** Exploring under heap pressure would be suicidal — exploration ALLOCATES cells, which consumes the resource that's already critical.

9. **TENSION: Exploration vs. purpose clarity.** If the user sets a clear purpose and wires up the right hardware, exploration is unnecessary. If the purpose is vague or the hardware is rich, exploration is valuable. The feature is most useful when the system is least well-configured.

10. **TENSION: Autonomy vs. utility.** The demo value is high (the OS discovers new senses!). The practical value on current hardware is near zero. Building for the demo risks over-engineering for a scenario that doesn't exist yet.

11. **TENSION: Register-level vs. pin-level discovery.** Shadow atlas entries are register-level (`apb_saradc.ctrl`). But external sensors are pin-level (GPIO 4 has a photoresistor). The exploration might page in the ADC control register but miss the specific channel/pin that matters.

12. **The "no new files" claim is strong.** This is a sub-pass of the existing supervisor, using existing alloc/agency/learning/eviction. The architectural simplicity is real.

---

## Phase 3: REFLECT

The core tension resolves when I stop thinking about exploration as "finding useful hardware" and start thinking about it as **expanding the substrate's observable surface area.**

The design document frames exploration as purpose-driven discovery: feel pain → search for hardware that helps. But that's the wrong frame. The OS doesn't need to know WHETHER a register is useful before paging it in. It just needs to OBSERVE it. Hebbian learning handles the rest.

**Core insight: Exploration is not search. Exploration is sampling.**

The domain-mapping heuristic is a premature optimization. It tries to be smart about WHAT to explore, but smartness requires knowledge the OS doesn't have. Instead:

- Sample broadly across HARDWARE_IN entries (perception zone). These are the senses.
- Don't try to map purpose keywords to zones. Purpose already guides Hebbian learning — routes that serve the purpose get reinforced, routes that don't get forgotten. The exploration just needs to provide raw material for learning to work on.
- Agency entries (HARDWARE_OUT) don't need exploration — the user declares actuators through purpose. Perception entries (HARDWARE_IN) are where surprise lives.

This simplifies the design dramatically:

1. **Drop the domain-to-zone mapping table.** It's a hardcoded heuristic solving the wrong problem.
2. **Explore HARDWARE_IN entries only.** The OS is expanding its perception — its ability to sense. Actuators are intentional, not discovered.
3. **Budget stays small.** 1-2 cells per pulse under sustained pain. Slow and steady.
4. **The cursor walks perception entries in the shadow atlas.** 523 perception entries (after array expansion). At 2/sec under pain, full sweep in ~4 minutes. That's fast enough to find any connected sensor.

The "bare board problem" (Node 4) actually resolves itself: on a bare board, every perception register reads a static value. Static values never correlate with anything. Hebbian learning never reinforces static routes. Eviction reclaims the cells. The OS explores, finds nothing, and stops (pain eventually clears or the user changes purpose). Cost: a few dozen alloc/evict cycles. Negligible.

The "external sensor" scenario (Node 5) is where it shines: the OS pages in `perception.apb_saradc.sar1_data_status` or `agency.gpio.pin4`, sees the value changing in correlation with purpose activity, Hebbian learning locks it in, and the OS has discovered a new sense without being told.

**Remaining tension: Register-level vs. pin-level (Node 11).** The shadow atlas has `agency.gpio.func4_in_sel_cfg` and `agency.gpio.pin4` but these are configuration registers, not live data registers. The HARDWARE_IN sampling in `internal_process_transitions` reads live GPIO levels via `reflex_hal_gpio_get_level` for addresses < 32. So exploration should also page in low-numbered "virtual" GPIO cells that trigger the GPIO sampling path. This needs thought — the shadow atlas entries have MMIO addresses, but GPIO live reads use pin numbers, not addresses.

**Resolution:** The atlas has entries like `agency.gpio.pin0` through `agency.gpio.pin34`. These have real MMIO addresses (GPIO_PIN0_REG through GPIO_PIN34_REG). But the HARDWARE_IN sampling code checks `if (hw > 0 && hw < 32)` to use `reflex_hal_gpio_get_level`. The atlas addresses are 0x60091000+, so they'd take the MMIO read path, not the GPIO pin path. For external sensors on GPIO pins, we'd need either:
- A way to register a GPIO pin number as the hardware_addr (already supported: addresses < 32 are treated as pin numbers)
- Or: exploration creates cells with pin numbers, not MMIO addresses

This is a real gap. The solution: exploration of GPIO pins should use pin numbers (0-34), not the GPIO_PIN register addresses. But the shadow atlas stores register addresses. So exploration needs a special case for GPIO pins, or the atlas boot-time weaving already handles this (the boot atlas creates `perception.gpio_in` at 0x60091000, which is the GPIO IN register — reading all 32 pins at once).

Actually, re-reading the boot atlas: `perception.gpio_in` at 0x60091000 is the GPIO input register. Reading it gives the state of all GPIO pins simultaneously. Individual pin sensing would need bit masking. The shadow atlas HAS per-pin entries with per-field bit masks. So exploring `perception.gpio.pin4.pad_driver` would read 0x60091010 with the right mask — but that's a configuration register, not the input data register.

The real GPIO input register is at 0x60091004 (GPIO_IN_REG). Shadow atlas entry: needs to exist as a perception entry.

Let me check: does `perception.gpio.in` exist? This depends on whether the SVD names that register.

This is getting into implementation detail. The key REFLECT insight holds: **exploration is sampling, not searching. Page in HARDWARE_IN entries, let Hebbian learning decide what matters.** The GPIO pin question is an implementation detail to resolve during coding.

---

## Phase 4: SYNTHESIZE

### Revised Architecture

**What changed from the original design:**

1. **Dropped domain-to-zone mapping.** No keyword tables. Purpose already guides learning.
2. **Explore HARDWARE_IN only.** The OS expands perception, not agency. Senses, not actuators.
3. **Simplified trigger.** Sustained pain → sample perception entries. No "widening" phase.
4. **Cursor walks perception entries only.** 523 entries, not 12,738. Full sweep in ~4 minutes under sustained pain.

### Implementation Spec

**New sub-pass in `goose_supervisor_pulse()`:**

```c
// After metabolic_sync, before snap
if (s_metabolic_state > -1) {  // not surviving
    goose_supervisor_explore();
}
```

**`goose_supervisor_explore()` (~50 lines):**

```
1. If no purpose active → return
2. If pain not active → reset s_explore_pain_ticks to 0, return
3. Increment s_explore_pain_ticks
4. If s_explore_pain_ticks < REFLEX_EXPLORE_PAIN_THRESHOLD → return
5. If s_explore_active >= REFLEX_EXPLORE_MAX_ACTIVE → return (cap reached)
6. budget = (s_metabolic_state == 0) ? 1 : REFLEX_EXPLORE_BUDGET  // halved when conserving
7. Walk shadow_map[] from s_explore_cursor:
   a. If entry.type != GOOSE_CELL_HARDWARE_IN → skip
   b. If entry.mask != 0xFFFFFFFF → skip (bit field, not register)
   c. If cell already allocated for this name → skip
   d. Allocate cell via goose_fabric_alloc_cell(name, coord, false)
   e. Set agency via goose_fabric_set_agency(cell, addr, GOOSE_CELL_HARDWARE_IN)
   f. s_explore_active++
   g. TELEM_IF(goose_telem_explore(name))
   h. budget--; if budget == 0 → break
   i. Advance s_explore_cursor (wrap at shadow_map_count)
8. If cursor wrapped fully → exploration exhausted this cycle
```

**State variables (static in goose_supervisor.c):**
- `s_explore_cursor` (uint16_t) — index into shadow_map[]
- `s_explore_pain_ticks` (uint16_t) — consecutive pain ticks
- `s_explore_active` (uint16_t) — cells paged in by exploration

**Tuning constants (reflex_tuning.h):**
- `REFLEX_EXPLORE_PAIN_THRESHOLD` = 5 (seconds before exploring)
- `REFLEX_EXPLORE_BUDGET` = 2 (cells per pulse)
- `REFLEX_EXPLORE_MAX_ACTIVE` = 30 (hard cap)

**Telemetry:**
- `goose_telem_explore(const char *name)` → `#T:D,<name>`

**Shell (status command):**
- When exploring: `explore: pain=12, cursor=142/12738, paged=7`
- When idle: `explore: idle`

### Success Criteria

1. On a bare board with `purpose set led`: exploration triggers after 5s of pain, pages in perception entries, finds nothing useful (all static), entries evict naturally. No crash, no Loom overflow.
2. On a board with an analog sensor on ADC: exploration discovers the relevant ADC register, Hebbian learning reinforces it, the cell survives eviction. The OS gained a new sense without being told.
3. Metabolic gating: `vitals override heap -1` (surviving) → exploration stops immediately.
4. `purpose clear` → exploration stops, pain ticks reset.
5. Telemetry: `#T:D` events visible in Loom Viewer during exploration.

### Files to Modify

| File | Change |
|------|--------|
| `components/goose/goose_supervisor.c` | Add `goose_supervisor_explore()` + state vars + call in pulse |
| `components/goose/include/goose_telemetry.h` | Add `goose_telem_explore()` |
| `components/goose/goose_telemetry.c` | Implement `goose_telem_explore()` |
| `include/reflex_tuning.h` | Add 3 exploration constants |
| `shell/shell.c` | Add exploration state to `status` output |
| `tools/loom_viewer.py` | Parse `#T:D` events |
| `README.md` | Update highlights |
| `docs/architecture.md` | Add exploration sub-pass description |
| `CHANGELOG.md` | Add entry |

### What the LMM Changed

The original design had a domain-to-zone keyword mapping table trying to be smart about what to explore. The LMM exposed this as premature optimization — solving a semantic problem with a lookup table. The revised design trusts the existing Hebbian learning to evaluate candidates and limits exploration to HARDWARE_IN entries (perception). Simpler, more honest, and architecturally cleaner.

The original design also had a "widening" phase that escalated to broader zones after 30 seconds. The LMM showed this is unnecessary: if perception entries don't help, exploring agency entries won't either — the problem is elsewhere (wrong purpose, no hardware connected, metabolic stress). The OS should explore its senses, not its actuators.
