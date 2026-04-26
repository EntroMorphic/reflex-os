# LMM: Curiosity Attractor (Exploration Reframe)

Applying the Lincoln Manifold Method to the proposal: replace pain-driven exploration with a curiosity attractor that probes hardware for signs of life.

---

## Phase 1: RAW

The current exploration works — 9/9 tests pass, the OS discovers registers autonomously. But the trigger is wrong. It waits for sustained zero progress (pseudo-pain) before looking. That's a defensive posture. The user's instinct: make it appetitive. Attract the OS toward interesting hardware rather than pushing it away from suffering.

The two-read probe idea is elegant on the surface: read a register, wait a second, read again, page in if it changed. But I have doubts.

First, what actually changes on an ESP32-C6? Temperature sensor output changes slowly (maybe 1 LSB per second). Timers increment constantly — systimer at 16MHz, timg0 at whatever its prescaler sets. ADC readings fluctuate with noise even when nothing is connected. GPIO input registers change when buttons are pressed or sensors trigger. So "hot" depends on the timescale of the probe. A 1-second gap catches timers and active sensors but might miss slow temperature drift. Everything that's clocked will look hot. Everything analog might look static over 1 second.

Second, the timer problem is real. The systimer counter register increments 16 million times per second. It will ALWAYS look hot. So will any free-running counter, watchdog timer, or clock register. These are noise, not signal. The OS would page in every timer register and learn nothing useful. We'd need to filter out "trivially hot" registers — but how? You can't distinguish a timer from a sensor without semantic knowledge the OS doesn't have.

Third, there's a cost to probing. Reading MMIO registers is a volatile pointer dereference — fast (one bus cycle, ~100ns). But the shadow atlas has 12,738 entries. Even just probing HARDWARE_IN registers (430 after expansion), at 4 probes per pulse, takes 107 seconds for a full sweep. That's almost 2 minutes of continuous probing to sample everything once. Is that fast enough? For a background process, probably yes. For a demo, it feels sluggish.

Fourth, the two-phase pipeline adds complexity. The current one-pass scan is stateless between pulses (only the cursor persists). The probe approach needs a buffer of "pending probes" that survives across pulses. Not complex, but more state to manage, more edge cases (what if the board reboots between phase A and phase B?).

Fifth — and this is the thing that excites me — the attractor model changes what the OS IS. The pain model says: "I'm a machine that avoids suffering." The curiosity model says: "I'm a machine that seeks novelty." These are philosophically different organisms. The curiosity model is closer to what we want: an OS that actively engages with its hardware environment, drawn toward the parts that are alive. It's not just an engineering reframe — it changes the character of the system.

Sixth, what about combining both? Curiosity runs always (slow background probe). Pain amplifies it (probe faster, probe wider). The OS is always a little curious, and becomes voraciously curious when it's struggling. That's biologically honest — animals explore when they're doing fine (slow baseline curiosity) and explore frantically when they're hungry or lost (amplified by need).

Seventh, there's a registration question. The shadow atlas entries are registers, not pins. The HARDWARE_IN sampling code reads GPIO pins by pin number (addresses < 32 → reflex_hal_gpio_get_level). For external sensors, the "hot" signal would be on a GPIO pin register or an ADC data register. The probe needs to read the right register. The shadow atlas has entries like `perception.apb_saradc.sar1_data_status` — that's the ADC data output. If a sensor is connected, this register would show changing values. So the probe approach naturally finds connected sensors. Good.

Eighth, I wonder about false negatives. Some sensors only change when polled/triggered (I2C sensors need a transaction, SPI sensors need a clock). These wouldn't show as hot through a passive register read. But on-chip sensors (temperature, ADC when enabled) and GPIO-connected sensors DO show as hot. So the probe works for the sensors that matter on a bare or lightly-wired board.

---

## Phase 2: NODES

1. **The two-read probe naturally finds connected sensors.** Any register whose value changes between two reads is attached to something active. ADC data registers, GPIO inputs, and timer counters all qualify. This is the right filter.

2. **Timers are noise.** Free-running counters (systimer, timg0, watchdog) always change. They'll dominate the "hot" list and waste Loom slots on trivially hot registers that carry no useful information for purpose fulfillment.

3. **TENSION: Everything clocked looks hot. Everything useful looks hot. How do you separate them?** The timer problem is the central challenge. Without semantic knowledge, the OS can't distinguish "hot because a sensor is connected" from "hot because it's a counter."

4. **Hebbian learning IS the filter for timer noise.** A timer register changes constantly but never correlates with purpose activity. Under Hebbian learning, its routes get equal +1 and -1 increments (random correlation), never committing to a learned orientation. It stays in the Loom but never gets reinforced. Eventually eviction reclaims it. Timers are "warm but useless" — curiosity attracts the OS to them, but learning discards them.

5. **The pain trigger is still useful but should be secondary.** Curiosity drives baseline exploration. Pain amplifies it — faster probing, wider search. Two speeds: casual curiosity (purpose active, 2 probes/pulse) and urgent curiosity (zero progress, 4 probes/pulse).

6. **Probing is just a volatile pointer read.** ~100ns per read, negligible CPU cost even at 8 probes per pulse. No bus contention concerns — MMIO reads are single-cycle on the APB bus.

7. **The two-phase pipeline is simple.** 4 probe slots × 8 bytes (index + value) = 32 bytes. Phase A: read and store. Phase B: re-read and compare. Cold entries discarded. Hot entries paged in. Cursor advances. State resets on purpose change or reboot.

8. **TENSION: Probe granularity vs. sweep speed.** At 4 probes/pulse at 1Hz, a full sweep of 430 HARDWARE_IN entries takes ~107 seconds. At 8 probes, ~54 seconds. At 16, ~27 seconds. More probes = faster discovery = more CPU in the pulse. The sweet spot depends on how quickly the OS should discover a newly connected sensor.

9. **The attractor model eliminates the pain threshold delay.** Currently the OS waits 5 seconds of zero progress before exploring. With curiosity, exploration starts as soon as purpose is set. The OS immediately begins probing its hardware surface. Discovery latency drops from (5s threshold + 3s cursor scan) to (1s first probe + 1s second read) = 2 seconds to first discovery.

10. **TENSION: Always-on curiosity vs. Loom pressure.** If the OS is always curious (not just under pain), it will continuously page in hot registers. On a board with many active peripherals, this could fill the Loom with exploration cells. The cap (REFLEX_EXPLORE_MAX_ACTIVE = 30) limits this, but 30 exploration cells is significant.

11. **Warm boot consideration.** Probe state (pending reads) is stack-local to the supervisor. On reboot, it's lost. Cursor resets to 0. This is fine — probing restarts from the beginning, re-discovering the same hot registers if they're still hot.

12. **The character shift is real.** Pain-driven = reactive, defensive, suffering. Curiosity-driven = proactive, engaged, alive. The system's personality changes. This matters for how people perceive and interact with Reflex OS.

---

## Phase 3: REFLECT

The central tension is Node 3: timers look hot, sensors look hot, and the OS can't tell the difference.

But Node 4 resolves it: **Hebbian learning is the timer filter.** A timer counter changes every read, but its changes are uncorrelated with purpose activity. Under reward-gated learning, a route from a timer cell to an intent cell accumulates random +1/-1 increments that cancel out. The hebbian_counter oscillates around zero, never reaching the commit threshold. The timer cell occupies a Loom slot but never becomes structurally important. When Loom pressure rises, eviction reclaims it (VIRTUAL cells are evictable).

Meanwhile, a sensor connected to a GPIO pin or ADC channel changes in response to the physical world. If the user's purpose involves that sensor (e.g., "photography" and a light sensor), the sensor's state changes correlate with purpose-relevant activity. Hebbian learning reinforces the route. The cell gets locked into the substrate.

**Core insight: curiosity provides candidates. Learning provides judgment. Eviction provides forgetting.**

The two-read probe is the curiosity mechanism. It's attracted to volatility — anything alive. It doesn't know what's useful. It doesn't need to. It just says: "this register is doing something." Learning and eviction handle the rest.

This three-layer separation is clean:
1. **Curiosity** (exploration) → finds what's alive
2. **Learning** (Hebbian) → finds what's relevant
3. **Forgetting** (eviction) → discards what's not

The pain amplification (Node 5) maps naturally: under zero progress, probe faster. This doesn't change the mechanism — just the rate. The OS is always a little curious (2 probes/pulse) and becomes intensely curious under stress (4-8 probes/pulse). This is exactly how biological curiosity works: baseline exploration is slow, need-driven exploration is fast.

**Remaining question: what about registers that are hot but never read as HARDWARE_IN cells?**

The probe reads registers directly (volatile pointer). But once paged into the Loom as a HARDWARE_IN cell, the cell's state is sampled via `internal_process_transitions` which reads the register on every route evaluation. So the transition from "probed" to "paged in" means the cell becomes continuously sampled, not just probed once per second. Good — the hot register gets promoted from occasional probing to continuous observation.

**Sweep speed (Node 8):** 4 probes per pulse at 1Hz covers 4 entries per second. With 430 HARDWARE_IN register-level entries (after filtering bit fields), full sweep takes ~107 seconds. But the cursor wraps — on the second pass, it skips entries that are already paged in. So the effective sweep narrows over time. First pass: 107s. Second pass: maybe 90s (40 entries already paged). This is acceptable for a background process.

Under pain amplification (8 probes/pulse), the sweep halves to ~54 seconds. First discovery still happens within 2-3 seconds (cursor hits the first perception entry fast with the 2000 max_scan).

**The probe buffer is simple.** I don't actually need a persistent buffer across pulses. I can do both reads in a single pulse with a microsecond gap:

```c
uint32_t v1 = *(volatile uint32_t *)addr;
// ... process other probes ...
uint32_t v2 = *(volatile uint32_t *)addr;
if (v1 != v2) → hot
```

Two reads in the same pulse, with other probes interleaved as a natural delay. This eliminates the two-phase pipeline entirely. No state between pulses. No probe buffer. Just read, interleave, re-read, compare.

Wait — the "delay" between reads matters. If both reads happen within microseconds, only very fast-changing registers (timers, clocks) will differ. A sensor that updates at 10Hz might have the same value in two reads 100µs apart. The 1-second gap between pulses catches slower signals.

But the interleaved approach catches timers (which we DON'T want) better than sensors (which we DO want). The 1-second gap catches sensors better but requires the two-phase buffer.

**Resolution:** Use the two-phase pipeline (probe on pulse N, check on pulse N+1). The 1-second gap catches real sensor activity while filtering out sub-microsecond noise. Timers still look hot (they change in 1 second), but Hebbian learning handles that.

The 32-byte probe buffer is trivial cost. The two-phase pipeline is the right design.

---

## Phase 4: SYNTHESIZE

### Revised Exploration: Curiosity Attractor

**What changed from the pain-driven model:**

| Aspect | Pain model | Curiosity model |
|--------|-----------|-----------------|
| Trigger | Sustained zero progress (5s) | Purpose active (immediate) |
| Mechanism | One-pass filter on cell type | Two-phase probe: read, wait, re-read |
| Attractor | None — push from suffering | Volatility — pull toward the alive |
| Pain role | Primary trigger | Rate amplifier (2× probe speed) |
| Discovery latency | ~8s (5s threshold + 3s scan) | ~2s (1s first probe + 1s re-read) |
| Character | Defensive, reactive | Curious, engaged |

### Implementation Spec

**Probe buffer (static in goose_supervisor.c):**

```c
#define EXPLORE_PROBE_SLOTS 4
typedef struct {
    size_t atlas_idx;     /* index into shadow_map[] */
    uint32_t last_value;  /* value read on probe pulse */
} explore_probe_t;

static explore_probe_t s_probes[EXPLORE_PROBE_SLOTS];
static uint8_t s_probe_count = 0;   /* pending probes from last pulse */
```

**Two-phase pipeline in `goose_supervisor_explore()`:**

```
Phase B (check): for each pending probe from last pulse:
  - Re-read the register at shadow_map[probe.atlas_idx].addr
  - If value != probe.last_value → HOT → page in via alloc_cell
  - If value == probe.last_value → cold → discard
  - Clear probe slot

Phase A (probe): walk shadow_map from cursor:
  - Skip non-HARDWARE_IN, skip bit fields, skip already-in-Loom
  - Read the register value via volatile pointer
  - Store (atlas_idx, value) in probe slot
  - Advance cursor
  - Stop when probe slots full or scan limit reached
```

**Rate modulation:**

```c
int probe_count = REFLEX_EXPLORE_PROBE_COUNT;  /* default 4 */
bool urgent = (s_explore_pain_ticks >= REFLEX_EXPLORE_PAIN_THRESHOLD);
if (urgent) probe_count *= 2;           /* 8 under pain */
if (metabolic == 0) probe_count /= 2;   /* halved when conserving */
```

**Trigger: purpose active, not pain.**

```c
if (!purpose || !purpose[0]) { reset; return; }
/* Always probe when purpose is active. Pain just makes it faster. */
```

### Tuning Constants

| Constant | Default | Rationale |
|----------|---------|-----------|
| `REFLEX_EXPLORE_PROBE_COUNT` | 4 | Probes per pulse (baseline curiosity) |
| `REFLEX_EXPLORE_PAIN_THRESHOLD` | 5 | Pain ticks before rate doubles |
| `REFLEX_EXPLORE_MAX_ACTIVE` | 30 | Hard cap on exploration cells |
| `REFLEX_SUPERVISOR_EXPLORE_DIV` | 10 | 1Hz explore rate |

### Telemetry

```
#T:D,<name>         — cell paged in (hot register discovered)
```

No change to telemetry format. The #T:D event means "the OS found something alive."

### Shell Status

```
explore: curious (probes=4, cursor=2400/12738, discovered=7)
explore: urgent (probes=8, pain=12, cursor=8100/12738, discovered=22)
explore: idle
```

### Success Criteria

1. `purpose set led` → exploration starts probing within 1 second (no pain delay)
2. Timer registers (systimer) detected as hot, paged in, but never Hebbian-reinforced → eventually evicted
3. Connected sensor (ADC, GPIO) detected as hot, paged in, Hebbian-reinforced → survives eviction
4. Pain amplification: under zero progress, probe rate doubles
5. `vitals override heap -1` (surviving) → probing stops
6. `purpose clear` → probing stops, state resets

### Files to Modify

| File | Change |
|------|--------|
| `components/goose/goose_supervisor.c` | Rewrite explore with probe buffer + two-phase pipeline |
| `include/reflex_tuning.h` | Add `REFLEX_EXPLORE_PROBE_COUNT` |
| `shell/shell.c` | Update status to show curious/urgent/idle |

### What the LMM Changed

The original proposal was "read twice, page in if different." The LMM exposed the timer noise problem (Node 2/3), resolved it through existing Hebbian learning (Node 4), and reframed the pain trigger as a rate amplifier rather than a binary gate (Node 5). The two-phase probe pipeline (read on pulse N, check on pulse N+1) emerged from REFLECT as superior to single-pulse double-reads because the 1-second gap catches real sensor activity.

The deepest shift: exploration is no longer gated by suffering. The OS is curious by default. Pain just makes it more curious. Three layers — curiosity finds what's alive, learning finds what's relevant, eviction forgets what's not — each doing exactly one job.
