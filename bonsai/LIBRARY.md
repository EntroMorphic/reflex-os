# GOOSE Standard Library: Geometric Fragments

## Purpose
To provide a set of trusted, reusable geometric patterns for common hardware and logic behaviors. These fragments are the building blocks of "Woven Complexity."

## Core Fragments

### 1. Heartbeat (Rhythm Pattern)
Generates a persistent oscillating signal.
- **Components:** 1 Cell (State), 1 Transition (Evolver).
- **Behavior:** Cycle through {-1, 0, 1} at a fixed interval.
- **Coordinate Offset:** `(0, 1, x)`

### 2. NOT (Inverter Pattern)
Inverts a signal.
- **Components:** 1 Route.
- **Orientation:** `-1`.
- **Logic:** $Sink = Source \times -1$.

### 3. GATE (Inhibitor Pattern)
Enables or disables a signal flow via a third party.
- **Components:** 1 Route, 1 Control Cell.
- **Agency:** The Control Cell's state sets the route's orientation ($+1, 0, -1$).

### 4. PRODUCT (Intersection Pattern)
Combines two signals into one.
- **Components:** 2 Sources, 1 Sink, 2 Routes.
- **Logic:** $Sink = (SourceA \times SourceB)$.

### 5. LATCH (Memory Pattern)
Persists a transient signal.
- **Components:** 1 Cell, 1 Self-Loop Route.
- **Orientation:** `+1`.
- **Logic:** $Cell = Cell_{current} \times 1$. Requires an external "Reset" route with `0` orientation to break the loop.

## Machine Fragments (Complex)

### 6. WIFI-BRIDGE (Radio Pattern)
A bridge between geometric intent and the physical 802.11 stack.
- **Intent Cell:** `(4, 1, 0)`. POS = Connect, NEG = Disconnect, ZERO = Idle.
- **Status Cell:** `(4, 1, 1)`. POS = Connected, NEG = Fault, ZERO = Searching.
- **Agency:** A "Shadow Bridge" driver that perceives the Intent Cell and drives the binary WiFi stack.

### 7. ENTROPY-FLOW (Logic Pattern)
Routes hardware RNG into the system fabric.
- **Source:** `(0, 4, 0)`.
- **Behavior:** Updates state every pulse with a true random trit.

## Usage
Fragments are "Woven" by the Supervisor or VM using the `goose_weave_fragment()` API.

