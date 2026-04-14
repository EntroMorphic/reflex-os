# Bonsai Glossary

This document unifies the terminology of the GOOSE paradigm (Geometric Ontologic Operating System Execution) as established during the Phase 1-5 proofs.

## Core Morphology

### Field
A coherent expanse of possible state and influence (e.g., the MMIO address space).

### Region
A bounded subdomain of a field with a coherent role or identity (e.g., the GPIO register block).

### Cell
The smallest semantically meaningful unit of distinction within a region. A "Cell Cluster" (like a pin) groups related cells.

### State
The condition currently inhabited by a cell, region, or field (e.g., high / low / hold).

### Transition
The admissible change from one state to another (e.g., rising / falling / stable).

### Route
The lawful path by which influence, access, or attention moves through the field.

## Advanced Concepts

### Intersection
A ternary dot product $D = \sum (A_i \times B_i)$ computed natively in hardware. In GOOSE, an intersection is the geometric overlap between a Source (stream) and a Reference (gate).

### Orientation
 The geometric direction of a state or route, typically expressed in ternary as:
 - **+1 (Positive):** Rising, Connected, Direct, Agree.
 - **0 (Neutral):** Stable, Detached, Hold, Quiescent.
 - **-1 (Negative):** Falling, Inverted, Oppose, Disagree.

### Shadow Agency
A condition where a structural hardware route (Phase 4) physically masks software register writes. Physical reality is decoupled from the software's register-view.

### Silicon Loop
The internal peripheral-to-peripheral loopback on the ESP32-C6 (e.g., RMT to PCNT) that allows native hardware compute of intersections.

### Tapestry
A global GOOSE Field (The Loom) that replaces traditional message queues with persistent routed state propagation. It allows system services to weave their internal logic into a shared geometric expanse.

### Meta-Agency
The ability to treat a route's orientation as a cell state. This allows for hierarchical regulation, where one field can "tilt" the manifold to enable, disable, or invert the connections between other fields.

### Equilibrium
A state of geometric consistency where physical reality (Sink) aligns with the intended manifold slope (Source * Orientation). The Harmonic Supervisor maintains equilibrium by re-balancing the system posture.

### Harmonic Supervisor
The "Immune System" of the OS. A high-priority meta-field that perceives disequilibrium across the Tapestry and exercises meta-agency to restore system posture.

### Self-Choreography
Behavior that emerges from the structural topology of regions and routes rather than being manually authored in imperative code. In a mature GOOSE system, complexity is not written; it is woven.

### Tiered Rhythm
The system-wide allocation of pulse frequencies:
- **Autonomic:** 1Hz (LP Heartbeat).
- **Harmonic:** 10Hz (Supervisor Balance).
- **Reactive:** 100Hz+ (IO Agency).

### Geometric Coordinate
A 9-trit ternary vector $(F, R, C)$ that defines an immutable spatial location within the system Tapestry, replacing symbolic names and binary addresses.


