# Experiment 005: The Silicon Loop (Hardware Intersection)

## Purpose

Prove that the ESP32-C6 can perform native ternary geometric operations (Intersections) entirely within its peripheral fabric, without CPU intervention, using an internal silicon loopback.

This experiment brings the "GIE" (Geometric Intersection Engine) concept from `the-reflex` into the GOOSE paradigm.

## Claim Under Test

A hardware **Region** can be configured as a self-advancing **Route** that performs a ternary geometric transformation (Intersection) between a **Source** (DMA stream) and a **Reference** (Peripheral configuration).

## Hardware Scope

-   **GDMA:** The mover (Propagation).
-   **PARLIO:** The emitter (Projection/Loopback).
-   **PCNT:** The sensor (Perception/Intersection).
-   **GPIO Matrix:** The patch field (Region).

## GOOSE Morphological Mapping

1.  **Field:** Perception Space.
2.  **Region:** The Intersection Engine (GIE).
3.  **Route:** The circular DMA chain.
4.  **Cell:** The "Neuron" (a single intersection unit).
5.  **State:** The current Dot Product result.
6.  **Transition:** The completion of a DMA loop.

## The Geometric Operation: Intersection

An intersection is a ternary dot product $D$ between two vectors $A$ and $B$:

$D = \sum (A_i \times B_i)$

In this experiment:
-   Vector $A$ is streamed through PARLIO from memory.
-   Vector $B$ is represented by the **PCNT Level Gating** configuration.
-   The Intersection $D$ is the physical count accumulated in the PCNT unit.

## The Experiment

1.  Configure PARLIO TX to loop back internally to the GPIO Matrix.
2.  Configure two PCNT units (Agree and Disagree) to listen to the looped-back signals.
3.  Set up a DMA buffer containing a "Query Vector".
4.  Execute the hardware loop.
5.  Read the resulting "Geometric Overlap" from the PCNT registers.

## Why This Honors GOOSE

Unlike `the-reflex`, which is a specialized neural computer, this experiment treats the GIE as a **Primitive GOOSE Region**. It proves that the "Soft Silicon" can perform non-binary operations natively.

## Success Criteria

-   The hardware loopback is established without external wiring.
-   The PCNT result matches the expected ternary dot product of the streamed vector.
-   The operation occurs at a deterministic hardware rhythm.
-   The complexity of the operation is contained within the Regional Geometry, not software glue.
