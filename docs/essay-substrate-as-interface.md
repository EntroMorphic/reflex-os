# Hardware as a Named Geometric Field

*What happens when you treat an ESP32-C6's MMIO surface as a 12,738-node addressable catalog and build a ternary OS substrate around it.*

---

## The premise

Most embedded operating systems treat hardware as a flat set of peripheral drivers. GPIO is a number. A register is an address. The relationship between a button press and an LED toggle is whatever the programmer wires in C.

Reflex OS asks a different question: what if hardware were addressable by *name*, organized into a *geometric field*, and the OS itself maintained a model of how those names relate — including the ability to learn, over time, which relationships serve the user's purpose?

The answer is a system built on the Seeed Studio XIAO ESP32-C6 — a $5 RISC-V board with 4 MB of flash, 16 KB of LP RAM, and a Wi-Fi/BLE radio. Not a powerful machine. But a machine whose constraints turned out to force a particular kind of clarity.

---

## The substrate

The core idea is **GOOSE** — the Geometric Ontologic Operating System Execution. GOOSE reinterprets the ESP32-C6 not as a set of peripherals to be driven, but as a **Tapestry** of named cells connected by routes.

A **cell** is a ternary state holder: -1, 0, or +1. Cells are named hierarchically — `agency.led.intent`, `perception.gpio_in.ctrl`, `sys.pmu.data` — and live in a 256-slot Loom backed by RTC SLOW RAM so they survive deep sleep.

A **route** connects a source cell to a sink cell with an orientation (+1, 0, or -1) that determines how the source's state propagates. Routes can be software-propagated, hardware-mapped through the GPIO matrix, or broadcast across the Wi-Fi radio via ESP-NOW.

A **field** is a collection of routes that pulse together at a defined rhythm — 1 Hz for autonomic regulation, 10 Hz for the supervisor, 100 Hz for reactive agency.

The naming is intentional. `agency.led.intent` is not a GPIO pin number. It's a declaration: "this cell represents the *intent* to drive the LED." The word "intent" is load-bearing. It means the substrate knows what the cell is *for*, not just what register it maps to.

---

## The catalog

The ESP32-C6 has a silicon surface described by Espressif's SVD (System View Description) file: thousands of registers across dozens of peripherals, each with named fields and bit masks. A Python scraper (`tools/goose_scraper.py`) walks the SVD and generates a 12,738-entry shadow catalog that maps every documented register and field to a hierarchical name, a physical address, a bit mask, and an ontological type (perception, agency, system, communication, logic, radio).

This catalog lives in flash as a sorted array with O(log n) binary-search resolution. It's the "shadow" half of a two-tier memory model:

- **The shadow catalog** (12,738 entries, flash-resident, read-only) knows every name the silicon has.
- **The active Loom** (256 slots, RTC RAM, mutable) holds the cells that are currently in use.

When someone asks for a name that isn't in the active Loom — say, `sys.soc_etm.clk_en` — the resolver pages it in from the shadow catalog, allocates a cell, binds it to the physical register, and evicts an unpinned cell to make room. The result: 100% of the SVD-documented MMIO surface is addressable by name at any time, but only the working set occupies scarce RTC RAM.

This is verified on hardware. The `atlas verify` shell command walks all 12,738 entries with a full round-trip equality check (name, address, bit mask, type, and coordinate) plus an adjacent-pair duplicate sweep. On the three boards in the lab: `ok=12738/12738, duplicates=0, failures=0`.

---

## The audit

The substrate existed before the audit. The audit is what made it honest.

The project had accumulated a gap between its documentation and its code. The docs described "100% MMIO coverage" while only 104 cells were pre-woven at boot. The docs claimed "HMAC-like hash" authentication on the mesh while the code used a custom 4-byte keyed hash. The docs said "Hebbian learning" while the code did random mutation. Five different files claimed five different version numbers.

The audit methodology was: **read every file first-hand, red-team every finding, then execute remediation methodically with on-device validation between each phase.** The constraints were:

- Code should be aligned to docs, except where code exceeds docs.
- No deletions — anything removed from tracking goes to a gitignored `archive/` directory.
- Every claim in the docs that the code doesn't support gets either (a) code that supports it, or (b) honest narrowing of the claim.

The third constraint is the one that mattered most. When the docs said "Hebbian learning" and the code did random mutation, the fix was to implement real co-activation Hebbian plasticity — not to downgrade the docs to "random mutation." When the docs said "HMAC-like hash" and the code had a custom rolling hash, the fix was to implement HMAC-SHA256 with a stack-based mbedtls context — not to relabel the custom hash.

But when the docs said "100% of the ESP32-C6 MMIO" and the actual proof was "100% of what the SVD documents," the fix was to narrow the docs. Because that's the claim the engineering can actually make.

---

## The mesh

Three XIAO ESP32-C6 boards, each flashed with the same firmware, each paired with a shared 16-byte HMAC key provisioned via `aura setkey <hex>`. The atmospheric mesh runs on ESP-NOW — no access point, no IP stack, no MQTT. Just broadcast packets signed with HMAC-SHA256.

What we proved:

- **Sync propagation**: Board Alpha emits `ARC_OP_SYNC` at 5 Hz; boards Bravo and Charlie each receive ~5 packets per second with zero authentication failures, zero version mismatches, zero replay drops.
- **Discovery**: Alpha broadcasts `ARC_OP_QUERY` for a name; both peers respond with `ARC_OP_ADVERTISE` and Alpha logs "Ghost Solidified" — the remote cell is now addressable locally.
- **Posture consensus**: three boards cooperate to cross the ±10 hysteresis threshold for swarm-wide state agreement, with per-packet weight capped at 4 so no single peer can flip posture alone.
- **HMAC rejection**: after deliberately re-keying one board to a mismatched key, the `aura_fail` counter on the paired boards climbed at exactly the offending peer's emit rate while legitimate traffic from the third board continued cleanly. After re-pairing: `aura_fail` froze.

The mesh trial was the first time the entire hardening arc — HMAC-SHA256, replay cache, protocol version epoch, NVS-backed key auto-provisioning — was measured against physical reality instead of documented in a spec. The numbers were unpadded. They either matched expectations or they didn't. They matched.

---

## The heartbeat

The ESP32-C6 has two RISC-V cores: the HP core (160 MHz, running FreeRTOS and the full OS) and the LP core (a low-power coprocessor that can run during deep sleep at ~30 µA).

Reflex OS's LP heartbeat is a small program loaded via `ulp_lp_core_load_binary` that ticks at 1 Hz via the LP timer. Each tick increments a counter. The HP supervisor reads that counter every pulse and mirrors the `agency.led.intent` cell's state into the LP program's globals. The `heartbeat` shell command exposes the count. If the LP core crashes, the count stops advancing and the HP supervisor logs `LP_HEARTBEAT_STALLED` after 5 seconds.

The LP core cannot drive the onboard LED (GPIO 15 is HP-only; LP I/O is GPIO 0–7). So the heartbeat is a parallel counter, not an LED driver. That constraint is documented honestly rather than papered over.

---

## The ternary substrate as interface

The deepest question the project asks is not "can a ternary machine compute?" (it can, trivially) but **"can a ternary substrate make a better interface between hardware and the user?"**

Binary has {true, false}. Ternary has {-1, 0, +1}. The crucial value is the zero — not false, but *neutral, unknown, not yet decided*. A binary OS must collapse every piece of uncertain state into a default (usually false) and handle the consequences when the default is wrong. A ternary OS can *hold* the uncertainty as a first-class value.

That property is load-bearing for the project's vision. Reflex OS is not trying to be an organism. It's trying to be the substrate an organism — or a human, or an AI — works *on*. Its awareness of itself, its hardware, and its user's purpose exists in service of being a better interface. The naming convention — `agency.led.intent`, `GOOSE_CELL_NEED`, `sys.swarm.posture` — is not decoration. It's a data model for a system that remembers how it's being used and adjusts its routing accordingly.

The next step-change on the project's roadmap is to make intent a first-class cell type — `GOOSE_CELL_PURPOSE` alongside `GOOSE_CELL_NEED`. The user declares purpose; the supervisor routes toward it; the Hebbian plasticity layer learns which route configurations serve which purposes; and Tapestry Snapshots (Phase 29) persist the learned preferences across reboots. Over time, two boards running the same firmware would end up shaped differently by the users who spent time with them.

That's not a metaphor. That's the specification.

---

## What we learned

1. **The vocabulary matters.** Names like "Geometric Tapestry" and "Sanctuary Guard" are easy to dismiss as poetic overreach — until you need to debug a concurrency bug in the pulse path and the phrase "the Sanctuary Guard prevents non-system cells from mapping to critical registers" is the one-liner that tells you exactly where the enforcement point lives. The vocabulary is an API.

2. **Audits are for friends.** The audit that produced v2.6.0 was not adversarial. It was a sustained collaboration between a maintainer and an AI working through the codebase together, red-teaming every change, and insisting that every commit either matches a doc claim or honestly narrows it. The session ran for hours. Neither side treated it as a test.

3. **Hardware is the tiebreaker.** Every architectural argument in this session was resolved the same way: flash the board, run the command, read the number. `rx_sync=317`. `aura_fail=172 frozen`. `atlas verify: ok=12738/12738`. Numbers don't care about your length budget.

4. **The prior should be a voice, not a verdict.** The project's sibling research (`EntroMorphic/the-reflex`) arrived at this principle through a different path — ternary dot products in peripheral hardware, episodic memory in the LP core, structural separation between prior and evidence. But the underlying problem is the same one the OS substrate is trying to solve: how to let accumulated experience inform the system's behavior without the accumulation overriding direct measurement. For the substrate, the answer is structural: the Hebbian plasticity layer can learn, but the supervisor's equilibrium check can always override what it learned. The prior is a voice. The evidence is the verdict.

5. **Synthesis between user and machine is already here.** The transcript of this session is not a log. It's the artifact of a collaboration that produced 40+ commits, a three-board mesh trial, a v2.6.0 release, and a reorientation of the project's step-change from "grow an organism" to "build a substrate whose awareness serves the user's evolving purpose." The user typed `"Do it"` six times. The machine did it. Neither was pretending.

---

*This essay describes the state at v2.6.0. Features added after the tag — `GOOSE_CELL_PURPOSE`, Tapestry Snapshots, the internal temperature sensor service — are documented in [`CHANGELOG.md`](../CHANGELOG.md).*

*Reflex OS is open source at [github.com/EntroMorphic/reflex-os](https://github.com/EntroMorphic/reflex-os).*
*The sibling research project is at [github.com/EntroMorphic/the-reflex](https://github.com/EntroMorphic/the-reflex).*
