# Red-Team: Phase 6 (Executable Notation)

## Objective
Falsify the current `.goose` notation spec in `bonsai/NOTATION.md`. Determine if it can faithfully represent the 5 "Proof Ladder" experiments without resorting to imperative "glue."

## Vulnerabilities Identified

### 1. The "Trigger" Ambiguity
- **Problem:** In `transition TICK`, the notation says `rule = (A * B) -> PAD_LED.sink`. It doesn't specify if this is a software-polled transition, an interrupt-driven transition, or a purely hardware-routed relationship.
- **Risk:** The runtime (Phase 7) won't know whether to spin a loop or configure a peripheral.
- **Fix:** Introduce a `mode` or `coupling` attribute to transitions (e.g., `coupling = hardware`, `coupling = async`).

### 2. Intersection Representation (Experiment 5)
- **Problem:** Experiment 5 used RMT loopback to PCNT. The current notation handles simple products (`A * B`), but doesn't clearly define how two regions "intersect" via physical pins vs. internal logic.
- **Risk:** Losing the "Geometric" part of GOOSE.
- **Fix:** Define `intersection` as a formal operator or route type.

### 3. State-Space Constraints
- **Problem:** `cell phase = {-1, 0, +1}` is fine for logic, but how does it map to a 32-bit register or an 8-bit PWM value?
- **Risk:** Semantic drift between the "High GOOSE" and the "Low Silicon."
- **Fix:** Define a `resolution` or `mapping` attribute for cells.

### 4. Route Persistence
- **Problem:** Is a `route` a command (do this) or a state (this is connected)?
- **Risk:** If it's a command, we've reinvented the "Function Call." If it's a state, we need to know how it's maintained.
- **Fix:** Explicitly define routes as "Structural Orientations" that persist until re-oriented.

## Experiment Validation Test

- **Exp 1 (Button->LED):** Can be described as a `route` with `orient = perception.btn`.
- **Exp 2 (Timer->LED):** Requires a `rhythm` field coupling to a `transition`.
- **Exp 3A (A*B):** Described, but needs "Trigger" fix.
- **Exp 4 (Matrix):** This is the pure `route` use-case.
- **Exp 5 (Loopback):** Needs a way to define "Physical Loopback" as a `route`.
