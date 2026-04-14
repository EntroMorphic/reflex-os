# Experiment 004: Regional Geometry (GPIO Matrix)

## Purpose

Prove that a hardware peripheral region can be modeled as a coherent local geometry of cell clusters, rather than a collection of isolated settings.

This experiment targets the **GPIO Matrix** and **IO Mux** of the ESP32-C6.

## Claim Under Test

The "Configuration" of a peripheral can be understood as establishing a **Route** through a **Region** of stateful cells. If the geometry is correct, the behavior emerges from the connection itself.

## Hardware Scope

- IO Mux (Pads)
- GPIO Matrix (Signal Routing)
- Internal peripheral signals (Sources)
- Physical pins (Sinks)

## Chosen Region: The Patch Field

We treat the combined IO Mux and GPIO Matrix as a single **Patch Field**.

### Cells in the Patch Field

1.  **Pad Cell (Sink):** A physical pin pad with its mux orientation (GPIO mode vs Function mode).
2.  **Signal Cell (Source):** An internal signal from a peripheral (e.g., UART TX, PWM Out).
3.  **Route Cell (Matrix):** The junction that couples a Source to a Sink.

## Ternary Geometric Interpretation

Instead of "writing 128 to GPIO_FUNCx_OUT_SEL_REG", we interpret the junction orientation:

-   **+1 (Connected):** Source is coupled to Sink.
-   **0 (Neutral):** Sink is held in GPIO identity (software-driven).
-   **-1 (Inverted):** Source is coupled to Sink with logical inversion.

## The Experiment

1.  Identify a physical Sink (LED pin).
2.  Identify a logical Source (an internal signal, perhaps a simple timer pulse).
3.  Implement a GOOSE-native "Patch" operation that treats the Matrix as a geometric surface.
4.  Switch the Route cell between `-1`, `0`, and `+1`.
5.  Observe whether the LED behavior follows the orientation change.

## Why This Is a Step-Change

In Phase 3, we composed two rhythms. In Phase 4, we are composing **Identity** and **Route**.

It proves that "driver configuration" is just another form of geometric routing in GOOSE.

## Success Result

GOOSE Phase 4: Regional Geometry is proven on hardware.

Validated on ESP32-C6 via GPIO Matrix "Patch Field" routing:

1.  **Structural Route:** Connecting an internal `LEDC` source to a physical `LED` sink through the matrix successfully governed the physical state of the hardware.
2.  **Ternary Orientation:** 
    -   `+1 (Rising/Connect)` produced a dim, steady pulse (1kHz/50%).
    -   `-1 (Falling/Invert)` successfully inverted the pulse geometry at the pad.
    -   `0 (Unchanged/Detach)` returned control to the "Neutral" software identity.

**Critical Red-Team Finding:**
The experiment identified **"Shadow Agency"**. Regional routes physically mask software register writes. When the route was active, the physical LED was pulsing even though the software status claimed it was `off`. This decoupling proves that GOOSE must treat Regional Routing as a higher-order truth than individual cell assertions.

## Natural Next Step

The next step-change should be **Cross-Region Routing** (Phase 5):

-   Prove that a transition in one region (e.g., Timing) can re-orient the route of another region (e.g., Matrix).
-   This creates the first "Governed Geometry" where one field regulates the topology of another.
