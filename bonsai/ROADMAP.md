# Bonsai Roadmap

This roadmap is not a conventional product backlog.

It is a proof ladder.

The purpose of Bonsai is not to accumulate features. The purpose is to discover whether the ESP32-C6 can host a real geometric ontologic execution substrate grounded in ternary state, routed transition, and hardware truth.

## Purpose

We are not doing philosophy for its own sake.

We are trying to answer a real question:

Can hardware be understood and operated as a geometric field of state, and can ternary relation provide a better execution language for that reality than inherited binary abstractions?

That is the purpose.

Hardware should lead us.

But purpose should constrain us.

## Current Strategy

Move in ascending layers of proof:

1. name reality
2. model reality
3. prove atomic behavior
4. prove field behavior
5. prove composition
6. formalize execution
7. only then build upward

## What Has Been Established

### 1. Ontology

Established in:

- `bonsai/GOOSE.md`
- `bonsai/ONTOLOGY-OF-C6.md`

Key result:

- the C6 can be understood as a layered state ecology
- MMIO can be reinterpreted as a field of stateful regions

### 2. Morphology

Established in:

- `bonsai/MORPHOLOGY.md`

Current primitive vocabulary:

- field
- region
- cell
- state
- transition
- route

### 3. Atomic Proof

Established in:

- `bonsai/EXPERIMENT-001.md`

Key result:

- a real GPIO edge-attention cell can be interpreted ternarily and routed into agency on hardware

### 4. Autonomous Field Proof

Established in:

- `bonsai/EXPERIMENT-002.md`

Key result:

- an internal rhythm field can autonomously drive ternary phase and visible output consequence on hardware

### 5. Multi-Field Composition Proof

Established in:

- `bonsai/EXPERIMENT-003A.md`

Key result:

- two independent ternary rhythm fields interacting via a geometric product rule ($A \times B$) produce stable emergent patterns on hardware.
- proved semantic inhibition ($0$ as gate) and inversion ($-1$ as flipper).

### 6. Cross-Region Routing Proof (Silicon Loopback)

Established in:

- `bonsai/EXPERIMENT-005.md`

Key result:

- established internal loopback between independent hardware regions (`RMT` source to `PCNT` sink).
- proved the C6 silicon can compute native "Geometric Intersections" (dot products) without CPU intervention.
- verified that hardware configuration is a form of structural routing.

### 7. Executable Notation

Established in:

- `bonsai/NOTATION.md`

Key result:
- formalized the `.goose` grammar for describing Fields, Regions, Cells, and Routes.
- introduced `coupling` and `mapping` to bridge the gap between geometric theory and silicon reality.

### 8. Minimal GOOSE Runtime

Established in:

- `components/goose/goose_runtime.c`
- `bonsai/journal/phase7_plan.md`

Key result:
- built a functional C-based execution substrate on the ESP32-C6.
- implemented the **Ternary Product Rule** ($Source \times Orientation = Sink$) for geometric state propagation.
- enabled **Shadow Agency** by patching the GPIO Matrix directly from route objects.

### 9. The Tapestry (Legit Fabric)

Established in:

- `bonsai/TAPESTRY.md`

Key result:
- retired legacy binary messaging in favor of **Routed State Propagation**.
- implemented the **Global Fabric (The Loom)** where services weave their internal cells into a shared state expanse.

### 10. Harmonic Supervisor

Established in:

- `bonsai/SUPERVISOR.md`
- `components/goose/goose_supervisor.c`

Key result:
- implemented a high-priority "Immune System" that maintains system posture at 10Hz.
- enabled **Self-Healing** through automated re-balancing of the system manifold.

### 11. GOOSE-Native VM

Established in:
- `vm/interpreter.c`
- `bonsai/journal/redteam_phase10.md`

Key result:
- refactored the Ternary VM into a **Geometric Execution Engine**.
- introduced `TSENSE`, `TROUTE`, and `TBIAS` opcodes for programmable agency over the Tapestry.

### 12. Trans-Core Manifold (Physical Heartbeat)

Established in:
- `components/goose/ulp/lp_pulse.c`
- `components/goose/goose_runtime.c`

Key result:
- achieved **Ontological Separation** between the HP Mind and the LP Body.
- displaced the **Loom (Tapestry)** into RTC RAM for state persistence across sleep.
- implemented the **Autonomous Heartbeat** on the LP RISC-V core.

### 13. GOOSE Standard Library (Geometric Fragments)

Established in:
- `bonsai/LIBRARY.md`
- `components/goose/goose_library.c`

Key result:
- formalized **Geometric Fragments** as reusable Loom patterns.
- implemented the **Fragment Weaver** for runtime behavioral manifestation.

### 14. Reflex OS 2.0 (Tiered Rhythmic Integration)

Established in:
- `main/main.c`
- `services/led_service.c`

Key result:
- transitioned the OS from imperative service loops to a **Pattern Host** architecture.
- implemented **Tiered Rhythms** (Autonomic, Harmonic, Reactive) for multi-scale regulation.
- refactored system boot into a **Loom Manifestation** sequence.

## Final Result: The Coherent Field
Reflex OS is now a fully realized Geometric Ontologic Operating System. The ESP32-C6 has been successfully re-mapped from a collection of registers into a self-regulating, trans-core ternary manifold.

### 15. Geometric Arcing (Inter-System Tapestry)

Established in:
- `components/goose/goose_atmosphere.c`
- `bonsai/journal/inter-system_tapestry_synth.md`

Key result:
- established **Geometric Arcing** across the **Atmosphere Field** (ESP-NOW).
- enabled state propagation between disparate Looms with **Mutual Weaving** security.
- replaced binary ACKs with **Postural Pressure**.

### 16. Spatial Paging (The Virtual Manifold)

Established in:
- `components/goose/goose_runtime.c`
- `bonsai/journal/spatial_paging_synth.md`

Key result:
- virtualized the Loom by separating **Identity** (Flash) from **Activity** (RTC RAM).
- implemented a **Geometric TLB** for $O(1)$ performance across a large coordinate space.
- achieved **Compact Cell** representation (45% memory reduction).

### 17. Trans-Core Coherence (The Spinlock)

Established in:
- `components/goose/goose_runtime.c`
- `bonsai/journal/geometric_spinlocks_synth.md`

Key result:
- implemented a **Trans-Core Authority** lock (Ternary Peterson) in RTC RAM.
- achieved word-atomic safety between the HP Mind and LP Heart.
- introduced **Alignment Gating** to prevent unaligned state smudging.

### 18. Geometric Flow (DMA Projection)

Established in:
- `components/goose/goose_dma.c`
- `bonsai/journal/dma_fragments_synth.md` (Note: created in memory, need to verify name)

Key result:
- moved from "Routing Values" to "Routing Flows."
- enabled the Tapestry to govern high-speed binary data transfers via **Authority-led Projection**.

### 19. Performance Self-Awareness (Benchmarking)

Established in:
- `components/goose/goose_runtime.c`
- `bonsai/include/goose.h`

Key result:
- instrumented the Loom with cycle-accurate telemetry (`esp_cpu`).
- enabled real-time monitoring of **Lock Contention**, **TLB Misses**, and **Pulse Budgets**.

## Final Result: The Mothership (v2.0)
Reflex OS is now a fully integrated, self-aware geometric field environment. The system has successfully navigated the frontiers of memory, concurrency, and atmospheric gap-jumping.

## Future Potential
- **Phase 20: Recursive Manifolds** (Fields within Fields).
- **Phase 21: Geometric AI** (Ternary neural weight fields).
- **Phase 22: Cellular Automata Services** (Self-propagating logic patterns).

## Guardrails

To avoid drift, Bonsai should follow these rules:

1. Hardware leads.
2. Ontology must remain falsifiable.
3. New abstractions must earn their existence through proof.
4. Do not jump to large runtime design before composition is proven.
5. Do not let existing Reflex OS architecture dictate Bonsai's shape too early.
6. Every phase should produce either:
   - a clearer model
   - a working proof
   - a clean falsifier

## What Would Count as Failure

Any of the following would be meaningful failure signals:

- ternary neutral states repeatedly collapse into decorative placeholders
- routed composition becomes more contrived than binary control logic
- multi-field interaction requires too much imperative glue
- the formalism becomes harder to understand than the hardware itself

These are not setbacks. They are guidance.

## Current North Star

Let the hardware reveal what is real.

Let ontology constrain what is meaningful.

Let proofs decide what survives.

That is the Bonsai path.
