# Remediation Plan: Purpose-Modulated Routing + Purpose Name Persistence

**Scope:** TODO items 1 and 2 from `docs/implementation-status.md`
**Status:** Complete

---

## Context

Reflex OS's `GOOSE_CELL_PURPOSE` (shipped in Phase 29.5) currently has one concrete effect: when active, `learn_sync` doubles Hebbian reward increments. The purpose cell is a boolean flag — the user-provided name string is acknowledged by the shell but not stored in the substrate. This means:

- The substrate cannot answer "what is the purpose?" — only "is there one?"
- `weave_sync` (autonomous fabrication) cannot orient its routing toward the declared purpose's domain
- Purpose does not survive reboot

These two items are tightly coupled: purpose needs a stored name before `weave_sync` can use its domain for routing bias.

---

## What Exists Today

| Component | Current State |
|---|---|
| `purpose set <name>` (shell) | Allocates `sys.purpose` cell, sets `state=1`, **discards name** |
| `purpose get` (shell) | Reports "active" or "inactive" — cannot report the name |
| `purpose clear` (shell) | Zeros `sys.purpose.state` |
| `weave_sync` | Matches `GOOSE_CELL_NEED` → capability sinks via generic last-segment suffix (e.g., `sys.need.led` → `.led` → any cell ending in `.led`) |
| `learn_sync` | 2x Hebbian amplification when `sys.purpose.state != 0` |
| Persistence | None — purpose is lost on reboot |

---

## Proposed Changes

### A. Purpose Name Storage (Structural)

1. Add `static char purpose_name[16]` in `goose_runtime.c` alongside `autonomy_field`
2. `goose_purpose_set_name(const char *name)` — copies name, persists to NVS (`goose/purpose`)
3. `goose_purpose_get_name()` — returns pointer to the static buffer (read-only)
4. `goose_purpose_clear()` — zeros buffer, erases NVS key
5. Load from NVS at `goose_fabric_init` time (before supervisor starts)
6. Shell `purpose set <name>` calls `goose_purpose_set_name` and sets cell state
7. Shell `purpose get` reports the stored name
8. Shell `purpose clear` calls `goose_purpose_clear` and zeros cell state

### B. Purpose-Modulated Capability Matching in `weave_sync` (Routing Effect)

When PURPOSE is active and `purpose_name` is non-empty, `weave_sync` uses a **two-pass capability search**:

1. **Domain-biased pass**: resolve the capability with the purpose name as a domain hint. For a need named `sys.need.led` with purpose `"sensor"`, prefer sinks whose name contains the purpose domain (e.g., `perception.sensor.led` over `agency.led.intent`).

2. **Fallback pass**: if the biased pass finds nothing, use the existing generic suffix match. Purpose never prevents a need from being wired — it only biases which sink is chosen.

Implementation: add `goonies_resolve_by_capability_in_domain(const char *suffix, const char *domain)` that filters candidates to those whose name contains the domain substring before falling back to the zone-priority scan.

### C. Same-Commit Documentation Updates

- `docs/architecture.md` §6 — document the routing effect
- `docs/implementation-status.md` — move items 1-2 from TODO to completed; add to Hardware-Validated Behaviors
- README shell table — update `purpose` entry
- CHANGELOG.md — new entry

---

## What Does NOT Change

- `learn_sync` 2x amplifier — stays. Purpose now has two effects: faster learning AND biased routing
- `GOOSE_CELL_PURPOSE` enum value — already exists
- Snapshot format — purpose name is metadata, not route plasticity
- Lock discipline — purpose name read is outside `loom_authority` (read-only, single-writer)

---

## Red-Team Concerns

| # | Concern | Assessment |
|---|---|---|
| 1 | **`purpose_name` concurrency** | Shell writes (main task), `weave_sync` reads (supervisor pulse task). Worst case: half-written name → garbled suffix → no domain hit → generic fallback. No crash, no wrong route. Safe by construction. |
| 2 | **NVS wear** | `purpose set` writes to NVS. Users won't cycle purpose rapidly. NVS wear-leveling handles it. |
| 3 | **Domain match ambiguity** | First match in biased pass wins, same priority ordering as existing scan. Deterministic. |
| 4 | **16-byte name limit** | Matches cell name limit used everywhere. Sufficient for domain tokens (`sensor`, `motor`, `comm`). |
| 5 | **Empty name + active cell** | Domain pass skipped when name is empty. Clean degradation to current behavior. |
| 6 | **Purpose loaded before supervisor** | NVS load in `goose_fabric_init` runs before supervisor pulse starts. No race on startup. |
| 7 | **Purpose name vs. purpose cell lifetime** | Cell may be evicted (though PURPOSE is eviction-guarded). Name lives in a static buffer independent of cell lifetime — the name outlives the cell if eviction somehow occurs. |

---

## Validation Plan

1. **Build**: clean `idf.py build` with no warnings in goose_runtime.c or goose_supervisor.c
2. **Boot**: cold boot on Alpha, confirm `purpose get` reports "inactive" (no NVS key yet)
3. **Set + Get**: `purpose set sensor` → `purpose get` reports `sensor`
4. **Reboot persistence**: `reboot` → `purpose get` still reports `sensor`
5. **Routing bias**: create a NEED cell that has matching sinks in multiple domains; confirm `weave_sync` prefers the purpose-domain sink when purpose is active, and falls back to generic when cleared
6. **Clear + reboot**: `purpose clear` → `reboot` → `purpose get` reports "inactive"
7. **Doc audit**: every shell command change reflected in same commit docs

---

## LMM Analysis

### RAW

What's actually happening here? I'm looking at a substrate that already has a concept of purpose — it knows *that* it has one — but can't say *what* it is. The purpose cell is a light switch: on or off. The name the user provides vanishes the moment it leaves the shell parser's `argv[2]`.

That's a deeper gap than it looks. The whole thesis of Reflex OS is "substrate-as-interface" — the machine's awareness of itself and its user's intent exists to make it a better interface. But right now, the awareness is content-free. The substrate knows it should care more (2x Hebbian amplification), but it doesn't know *about what*. It's attention without object.

The purpose name isn't just metadata. It's the first piece of semantic information the user gives the machine about *why they're using it*. If I get this right, the substrate genuinely orients toward what the user is trying to do. If I get it wrong — over-engineer it, make it too clever, add a matching heuristic that fires incorrectly — the substrate will actively mislead its own routing, which is worse than having no purpose at all.

There's tension between making the domain match powerful enough to matter and simple enough to be predictable. A substring match (`"sensor"` matches any name containing `"sensor"`) is easy to reason about but could match spuriously. A zone-prefix match (`"sensor"` only matches names in `perception.sensor.*`) is more precise but assumes the user knows the naming hierarchy. The user typing `purpose set sensor` probably means "I'm doing sensor work" — they don't mean "only route through names in the perception zone that have a second segment called sensor."

The two-pass approach (try domain-biased, fall back to generic) is the right structural choice because it makes purpose *additive*. Purpose can only help, never hurt. A wrong domain match just means the substrate misses the bias and falls through to the same behavior it has today. That's the safety property I need to preserve.

Another tension: where does the purpose name live? I proposed a static buffer in `goose_runtime.c`. That's simple and it works. But it means purpose is a singleton — one machine, one purpose. That's correct for now (the C6 is a single-user device), but it's worth being honest that this is a design choice, not an accident. Multi-purpose would require purpose to be a cell attribute or a per-field annotation, which is a different architecture entirely.

The NVS persistence is straightforward but carries a subtle implication: the machine remembers what it was for across power cycles. That's actually meaningful for the project's vision. Two boards running the same firmware will diverge not just in their learned routes (Tapestry Snapshots) but in their declared purpose. The machine's identity grows from use.

### NODES

1. **Purpose name storage**: singleton static buffer + NVS persistence. Simple, correct for a single-user device, honest about the singleton constraint.

2. **Domain-biased routing**: two-pass search in `weave_sync`. Domain match first, generic fallback second. Purpose is additive — never blocks a route that would otherwise succeed.

3. **Domain match semantics**: the critical design decision. Substring vs. zone-prefix vs. segment match. Must be predictable to the user and safe against false positives.

4. **Concurrency safety**: single-writer (shell task), single-reader (`weave_sync` in supervisor pulse). Worst-case torn read falls through to generic match. No lock needed.

5. **`goonies_resolve_by_capability_in_domain`**: new function. Filters the existing zone-priority scan by requiring the candidate name to contain the domain. This is the only new code in the hot path.

6. **API surface**: three new public functions (`goose_purpose_set_name`, `goose_purpose_get_name`, `goose_purpose_clear`) declared in `goose.h`.

7. **Validation challenge**: routing bias is harder to test than plasticity amplification. I need a scenario where two valid sinks exist for the same need, and the domain-biased one wins only when purpose is active.

### REFLECT

The deepest architectural question is: **what does "domain" mean in this substrate?**

The GOONIES naming hierarchy has implicit semantic zones: `agency.*` is output, `perception.*` is input, `sys.*` is system. Within those zones, the second segment often names a subsystem (`agency.led`, `perception.gpio_in`, `sys.pmu`). The purpose name maps most naturally to this second segment — it's the part that says *what kind of thing* the cell is about.

So the domain match should be: "does the candidate name contain `.<purpose>.` as a segment boundary?" Not a raw substring (too loose), not a zone prefix (too rigid). A segment-bounded match: `"sensor"` matches `perception.sensor.reading` and `agency.sensor.ctrl` but not `sys.sensors_disabled`.

Wait — that's clever but fragile. What if the user types `purpose set led`? Then `agency.led.intent` matches (contains `.led.`), and so does any other cell with `.led.` in its name. That's actually correct behavior — the user said "I'm doing LED work" and the substrate preferentially routes toward LED-related cells.

The segment-bounded approach (`.domain.`) is better than raw substring because it won't match partial words. `purpose set temp` won't accidentally match `sys.template.config` — it'll only match names with `.temp.` as a whole segment.

But I need to handle the edge case where the domain appears as the *last* segment (no trailing dot). `perception.temp` should match when purpose is `"temp"`. So the match is: name contains `.<domain>.` OR name ends with `.<domain>`.

This is still a simple `strstr`-class operation. No regex, no allocations, no parsing overhead.

The real leverage point is that this matching rule naturally aligns with the GOONIES naming convention the project already uses. Users who name their cells well get better purpose routing. The naming convention becomes load-bearing in a new way — it's not just human-readable, it's machine-navigable.

One more thing: the existing `goonies_resolve_by_capability` already scans in zone priority order (agency, perception, sys). The domain-biased variant should preserve this ordering — within the domain-filtered candidates, agency still beats perception still beats sys. Purpose biases *which candidates* are considered, not *which zone wins*.

### SYNTHESIZE

**Key decisions:**

1. **Domain match rule**: segment-bounded. Match candidates whose name contains `.<purpose>.` or ends with `.<purpose>`. Simple, predictable, aligned with GOONIES naming convention.

2. **Two-pass with zone priority preserved**: domain-biased pass scans zone-by-zone (agency → perception → sys) but only considers candidates matching the domain. Fallback pass is identical to today's generic scan.

3. **Singleton purpose name**: `static char purpose_name[16]` in `goose_runtime.c`, persisted to NVS `goose/purpose`. Honest about the singleton constraint in docs.

4. **API**: `goose_purpose_set_name`, `goose_purpose_get_name`, `goose_purpose_clear` in `goose.h`. Shell calls these; `weave_sync` reads via `goose_purpose_get_name`.

5. **No lock for purpose name**: single-writer, single-reader, worst-case torn read degrades to generic match. Adding a lock would be ceremony without safety gain.

**Action steps:**

1. Add purpose name storage + NVS persistence + three API functions to `goose_runtime.c`
2. Declare the three functions in `goose.h`
3. Add `goonies_resolve_by_capability_in_domain` to `goose_runtime.c`
4. Modify `weave_sync` to call the domain-biased resolver when purpose is active
5. Update shell `purpose set/get/clear` to use the new API
6. Load purpose name from NVS in `goose_fabric_init`
7. Update docs in same commit (architecture.md, implementation-status.md, README, CHANGELOG)
8. Build → flash → validate on Alpha (cold boot, set/get, reboot persistence, routing bias, clear)

**Success criteria:**

- `purpose set sensor` + `purpose get` → reports `"sensor"`
- Reboot → `purpose get` → still `"sensor"`
- `weave_sync` with an active purpose demonstrably prefers domain-matching sinks over generic matches
- `purpose clear` + reboot → `purpose get` → `"inactive"`
- Clean build, no warnings, no regressions in existing shell commands or supervisor behavior
- All doc files updated in the same commit as the code changes

**Resolved tensions:**

- Substring vs. segment match → segment-bounded (`.domain.` or trailing `.domain`) — precise enough to avoid false positives, loose enough that users don't need to know zone prefixes
- Singleton vs. multi-purpose → singleton, honestly documented as a design choice
- Purpose as acceleration vs. direction → both: learn_sync amplifies (acceleration), weave_sync biases (direction)
