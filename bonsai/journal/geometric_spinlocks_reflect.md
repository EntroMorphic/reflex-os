# Reflections: Geometric Spinlocks

## Core Insight
Geometric Spinlocks are **Coherent Posture Transitions**. The lock is not a "Gate" we wait behind, but a "Structural Inhibition" that ensures only one core can evolve the Loom at a time.

## Resolved Tensions

### 1. Peterson's in the Loom (Ternary Peterson)
Instead of a binary lock word, we will use three reserved cells in the **System Region**:
- `sys_hp_intent`: `-1` (Passive), `+1` (Wants Loom)
- `sys_lp_intent`: `-1` (Passive), `+1` (Wants Loom)
- `sys_loom_turn`: `-1` (HP), `+1` (LP)

This is a **Ternary Peterson Lock**. It is naturally persistent in RTC RAM and visible to both cores without special hardware atomics (as long as trit-writes are word-atomic).

### 2. Assembly CAS (The Physical Backstop)
Since the LP RISC-V might be too slow for a pure C implementation of Peterson's, we will use a **Single Word Lock** in RTC RAM as a low-level primitive for the `lp_pulse`.
- The HP core uses `ux_port_enter_critical` (binary).
- The LP core uses a small RISC-V assembly loop to flip a bit in the authority word.

### 3. Ontological Purity (The Inhibit Route)
To keep the system "Geometric," the lock status is routed to the **Orientation** of all cross-core routes.
- If `sys_loom_lock` is active, the orientation of critical routes is multiplied by `0` (Gated).
- This prevents the LP core from seeing partially updated HP core state.

## Refined Specification
1.  **Loom Lock Word**: A `volatile uint32_t` in RTC RAM.
2.  **`goose_loom_lock()` / `goose_loom_unlock()`**: HP core primitives.
3.  **`ulp_loom_lock()` / `ulp_loom_unlock()`**: LP core (RISC-V) primitives.
4.  **Supervisor Watchdog**: A 10Hz check that resets the lock if held for >10 pulses.
