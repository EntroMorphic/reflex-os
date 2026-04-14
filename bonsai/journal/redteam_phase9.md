# Red-Team: Phase 9 (Harmonic Supervisor)

## Objective
Falsify the current "Harmonic Supervisor" implementation. Determine if the regulation logic introduces new modes of failure or collapses into circular dependency.

## Vulnerabilities Identified

### 1. The "Oscillation Loop" (Geometric Jitter)
- **Problem:** If a route is inhibited (`0`) for a *purpose* (e.g., intentional silence), the Supervisor's `rebalance` function will interpret it as a "blockage" and force it to `+1`. If the system then tries to silence it again, we enter an infinite "fight" for the orientation.
- **Risk:** High-frequency flipping of hardware routes, potentially damaging peripherals or wasting CPU.
- **Fix:** Introduce "Intent Tags" or differentiate between *Structural Orientation* (The Supervisor's job) and *Signal Orientation* (The Application's job).

### 2. The "Equilibrium Trap" (False Positives)
- **Problem:** `check_equilibrium` assumes that `Sink == Source * Orientation` is the only truth. However, if a sink is mapped to a hardware input that is physically pulled low, the Supervisor might try to "rebalance" it by flipping the source, creating a "Physical Contradiction."
- **Risk:** The Supervisor fighting against physical reality (e.g., a button being held down).
- **Fix:** Distinguish between "Observable Sinks" and "Calculated Sinks."

### 3. Circular Agency (Who Regulates the Regulator?)
- **Problem:** If the Supervisor's own internal routes fall out of equilibrium, its `rebalance` call will process its own transitions.
- **Risk:** If the Supervisor is "broken," it may "fix" itself into a corrupted state.
- **Fix:** Hard-wire the Supervisor's core Field or use a "Watchdog Rhythm" that is physically separate from the Tapestry.

### 4. Memory Leak / Pointer Fragility
- **Problem:** The Supervisor uses raw pointers to `goose_field_t`.
- **Risk:** If a field is re-allocated or removed from the fabric while the Supervisor is checking it, we get a Kernel Panic.
- **Fix:** Use the `fabric_cell` registry exclusively for Supervisor perception.

## Falsification Test (Phase 9.5)
Can I trick the Supervisor into a "Rebalance Storm"?
- Setup: A route where `Sink` is an input pin.
- Trigger: Manually override the `Sink` state.
- **Result:** The Supervisor will perpetually try to "heal" the route, even though the "error" is coming from the external environment, not the internal geometry.
