# Experiment 003A: Multi-Field Rhythm Composition

## Purpose

Prove that two independent ternary state fields can interact naturally under GOOSE to produce stable, emergent behavior on hardware.

This experiment removes the human from the loop and tests the **compositional** power of the paradigm.

## Claim Under Test

Ternary fields can be composed through geometric rules (such as multiplication or routing) to produce behavior that is simpler to describe and more robust than equivalent binary state-machine logic.

## Hardware Scope

- two internal rhythm sources (A and B)
- one LED output pin
- direct shell-controlled task

## Chosen Fields

1.  **Field A (Slow Rhythm)**
    - cycles through `{-1, 0, +1}` every 2 seconds.
    - acts as the "Base Orientation".
2.  **Field B (Fast Rhythm)**
    - cycles through `{-1, 0, +1}` every 500 ms.
    - acts as the "Modulator".

## Geometric Composition Rule

We define the output agency state $O$ as the **Ternary Product** of A and B:

$O = A \times B$

### Ternary Multiplication Truth Table

| A \ B | -1 | 0 | +1 |
| :--- | :--- | :--- | :--- |
| **-1** | +1 | 0 | -1 |
| **0** | 0 | 0 | 0 |
| **+1** | -1 | 0 | +1 |

### Interpretation into Agency (LED)

- **+1 (Positive):** LED Solid On
- **0 (Neutral):** LED Off
- **-1 (Negative):** LED Blinking (or Pulse)

## Expected Emergence

- When Field A is Neutral ($0$), the output is always $0$ (LED Off), regardless of Field B.
- When Field A is Positive ($+1$), the output tracks Field B directly: LED On, Off, Blink.
- When Field A is Negative ($-1$), the output is the **Inversion** of Field B:
  - If B is $+1$ (On), Output is $-1$ (Blink).
  - If B is $-1$ (Blink), Output is $+1$ (On).

This creates a complex "rhyming" pulse pattern that would be tedious to code as a flat binary state machine, but emerges naturally from the interaction of two orientations.

## Minimal Procedure

1.  Initialize two phase-stepping rhythm cells.
2.  Advance them at their respective cadences.
3.  Apply the ternary product rule at every tick.
4.  Route the result into LED agency.
5.  Observe the rhythm patterns on device.

## Success Result

GOOSE Phase 3: Multi-Field Composition is proven on hardware.

Validated on ESP32-C6 via autonomous rhythm interaction:

1.  **Semantic Inhibition:** When Field A (Base) was neutral, the output was always neutral, proving that ternary $0$ correctly acts as a geometric "hold" or "gate" in composition.
2.  **Tracking and Inversion:**
    -   When Field A was $+1$, Field B was passed through directly to the LED.
    -   When Field A was $-1$, Field B was geometrically inverted.
3.  **Emergent Rhythm:** The physical LED produced a complex, rhyming pulse pattern derived purely from orientation rules rather than ad hoc timing code.

This confirms that GOOSE composition holds on hardware and produces more expressive behavior than equivalent binary logic with less implementation complexity.

## Natural Next Step

The next step-change should be **Regional Geometry** (Phase 4):

-   Move from loose tasks into a structured hardware-near region.
-   Define a region of related cell clusters (e.g., GPIO control + IO mux) as a single geometric entity.
-   Test whether regional structure can yield behavior more fundamentally than isolated register writes.
