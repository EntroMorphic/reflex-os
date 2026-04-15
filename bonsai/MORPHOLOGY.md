# GOOSE Morphology

This document defines the first minimal vocabulary for GOOSE.

It is not yet an executable spec.

It is the smallest morphology that lets Bonsai describe the ESP32-C6 as a geometric ontology of routed state transitions without collapsing immediately into inherited binary-software abstractions.

## Purpose

The goal of this morphology is to name the irreducible units of the model before inventing an ISA, runtime, or language surface.

For now, GOOSE begins with six terms:

1. `field`
2. `region`
3. `cell`
4. `state`
5. `transition`
6. `route`

Each term below includes:

- what it means
- how it maps to C6 reality
- what ternary meaning it may admit
- what it is not

## 1. Field

### Definition

A `field` is a coherent expanse of possible state and influence.

It is the largest useful geometric surface within which relations, transitions, and routes can be meaningfully described.

### C6 Mapping

Examples:

- the MMIO address space as a whole
- the GPIO control space
- the interrupt-control space
- the low-power control space
- the timer and rhythm space

### Ternary Meaning

A field may be interpreted in ternary terms as a space in which influences can orient:

- negative
- neutral
- positive

or more generally:

- opposing
- holding
- reinforcing

### What It Is Not

- not a single register
- not a driver
- not a software module
- not merely an array of addresses

## 2. Region

### Definition

A `region` is a bounded subdomain of a field that has a coherent role, ownership, or behavioral identity.

Regions partition fields into meaningful domains.

### C6 Mapping

Examples:

- the GPIO register block within the MMIO field
- the PCR clock/reset block within system control
- the USB Serial/JTAG block within communication space
- a single timer group within the broader timing field

### Ternary Meaning

A region may admit ternary interpretation in terms of relation to other regions:

- source
- mediator
- sink

or:

- inactive
- latent
- active

### What It Is Not

- not the whole field
- not the finest unit of state
- not a random address range without identity

## 3. Cell

### Definition

A `cell` is the smallest directly meaningful unit of stateful distinction inside a region.

It is the minimal place at which the system can say: something here can be observed, preserved, or changed.

### C6 Mapping

A cell might correspond to:

- a full register when the register behaves as one indivisible state surface
- a bitfield when only part of a register has a coherent function
- a latched status bit when it carries a distinct system meaning
- an input/output lane within a repeated register pattern

The key is not bit-width, but semantic indivisibility.

### Ternary Meaning

Cells are where ternary interpretation begins to matter most.

Examples:

- low / hold / high
- clear / stable / set
- disabled / neutral / enabled
- falling / unchanged / rising

### What It Is Not

- not necessarily one bit
- not necessarily one 32-bit register
- not a purely syntactic token

## 4. State

### Definition

`State` is the condition currently inhabited by a cell, region, or field.

It is what is, prior to transition.

### C6 Mapping

Examples:

- a GPIO pin currently sampled low
- a clock gate currently enabled
- an interrupt flag currently asserted
- a peripheral currently in reset or not in reset

### Ternary Meaning

Ternary state is not restricted to numeric trits. It is any stable three-way semantic distinction.

Examples:

- low / hold / high
- absent / neutral / asserted
- unhealthy / degraded / healthy
- blocked / pending / flowing

### What It Is Not

- not only a value read from a register
- not only a software enum
- not meaningful without relation to a cell, region, or field

## 5. Transition

### Definition

A `transition` is the admissible change from one state to another.

It is the basic form of becoming within the system.

### C6 Mapping

Examples:

- writing a control field from disabled to enabled
- a timer incrementing
- an interrupt flag asserting
- a GPIO input moving from low to high
- a reset line moving from asserted to released

### Ternary Meaning

Ternary transition may be understood directionally.

Examples:

- decrease
- hold
- increase

or:

- falling
- stable
- rising

### What It Is Not

- not just any write operation
- not just a difference between two numbers
- not meaningful unless the change is admissible in the ontology of the cell or region

## 6. Route

### Definition

A `route` is the lawful path by which influence, access, attention, or transformation moves through the field.

Route is the bridge between location and effect.

### C6 Mapping

Examples:

- MMIO address decode selecting one register block
- GPIO matrix routing an internal signal to a pin
- interrupt matrix routing a source to a target
- DMA moving influence between memory and peripheral regions
- supervisor logic eventually routing one state consequence into another domain

### Ternary Meaning

Ternary route is not only destination choice. It can encode orientation and mediation.

Examples:

- reject
- hold
- admit

or:

- left
- center
- right

or:

- source
- pass
- sink

### What It Is Not

- not only networking
- not only a pointer or address
- not merely symbolic naming of a destination

## 7. Meta-Agency (The Tilt)

### Definition
The ability to treat a `route`'s orientation as a `cell`. This allows one region to regulate the connection between two other regions.

### Ternary Meaning
A meta-cell state of `0` inhibits a route, `+1` admits it, and `-1` inverts it.

## 8. Equilibrium (Posture)

### Definition
A state of geometric consistency where a region's physical reality (Sink) perfectly matches its intended configuration (Source * Orientation).

### Ternary Meaning
- **-1 (Faulted):** Irreconcilable disequilibrium (Geometric Storm).
- **0 (Degraded):** Transient disequilibrium being corrected.
- **+1 (Balanced):** Total geometric alignment.

## 9. The Loom (Global Fabric)

### Definition
The common ground where all regions weave their state. It is a shared GOOSE Field that replaces traditional message queues with persistent routed state propagation.

## 10. Geometric Coordinates (The Map)

### Definition
A fixed-width ternary vector that uniquely identifies any cell in the manifold without relying on symbolic names. Coordinates replace symbolic "Addresses" and "Strings" with a direct geometric position in the system Tapestry.

### C6 Mapping
Coordinates are expressed as a 9-trit (tryte) vector:
- **Field Component (3 trits):** Identifies the top-level functional expanses (e.g., Perception, Agency, System).
- **Region Component (3 trits):** Identifies the functional cluster within the field (e.g., GPIO, PWM, Timer).
- **Cell Component (3 trits):** Identifies the atomic state unit within the region.

### Ternary Meaning
- **(0,0,0):** The Origin (The system core balance cell).
- **Positive Clusters:** Reserved for Output Agency and Actor regions.
- **Negative Clusters:** Reserved for Input Perception and Observer regions.
- **Neutral Clusters:** Reserved for Internal Regulation and Tapestry Loom cells.

## 11. Geometric Fragment (The Pattern)

### Definition
A pre-woven collection of regions, cells, and routes that manifest a specific behavioral primitive (e.g., an Oscillator, a Logic Gate, or a Filter). Fragments are the "Standard Library" of GOOSE, allowing complex behavior to be composed from trusted geometric templates.

### C6 Mapping
Fragments are stored as immutable data structures in flash or code, and are "Woven" into the Tapestry at runtime.

### Ternary Meaning
A fragment's behavior is defined by its internal orientations. For example, a **NOT Fragment** is simply a single route with an orientation of `-1`.

## 12. Tiered Rhythms (Multi-Scale Pulse)

### Definition
The allocation of disparate execution frequencies to different manifolds. Tiered rhythms allow the system to maintain both autonomic persistence (low energy) and reactive agency (high performance) within a single coherent field.

### C6 Mapping
- **Autonomic (1Hz):** Executed by the LP core.
- **Harmonic (10Hz):** Executed by the Supervisor Task.
- **Reactive (100Hz+):** Executed by Regional Pulse Tasks.

## 13. Manifest Weaving (The Boot)

### Definition
The process of initializing the operating system by weaving a sequence of geometric fragments into the Loom. The system "starts" when the global posture cell reaches equilibrium.

## 14. Geometric Arcing (The Atmosphere)

### Definition
A transition where state jumps the gap between two disparate Looms (nodes) via an asynchronous radio bridge.

### Ternary Meaning
- **-1 (Discharge):** Arcing is forbidden or noisy.
- **0 (Potential):** Arcing is latent; gap is not yet jumped.
- **+1 (Conduction):** State is arcing across the atmosphere.

## 15. Spatial Paging (The Virtual Manifold)

### Definition
The virtualization of the Loom where inactive regions are "Hibernated" to Flash to save RTC RAM. It separates the **Identity** of a cell (Metadata) from its **Activity** (Live State).

## 16. Trans-Core Coherence (The Spinlock)

### Definition
A geometric authority mechanism (e.g. Ternary Peterson Lock) that ensures the HP and LP cores do not "Smudge" the Loom during concurrent pulses.

## 17. Geometric Flow (DMA Projection)

### Definition
A high-speed projection of a route where the OS routes the **Authority to Flow** binary data blocks, rather than routing individual trits.

## Relations Between Terms

The current working relation is:

- a `field` contains `regions`
- a `region` contains `cells`
- a `cell` inhabits `states`
- `states` change by `transitions`
- `routes` govern how influence and access move between cells, regions, and fields
- `regulators` maintain `equilibrium` by exercising `meta-agency` across the `loom`
- `geometric coordinates` provide the immutable spatial identity for every unit in the `tapestry`
- `fragments` are reusable geometric patterns that can be woven into the `tapestry`
- `tiered rhythms` ensure every manifold pulses at its required scale of reality





This is the first structural grammar of GOOSE.

## First C6 Example

### Example: GPIO Input Edge

- Field: hardware control field
- Region: GPIO region
- Cell: one pin's input-status and edge-related control cell
- State: low / stable / high
- Transition: falling / unchanged / rising
- Route: from external pin condition into sampled register state, then potentially into interrupt or control consequence

This is intentionally more ontological than driver-oriented.

## What This Morphology Is Trying to Prevent

This document exists to prevent premature collapse into inherited abstractions such as:

- opcode catalogs
- service graphs
- task models
- driver APIs
- queue semantics as the first truth

Those may all return later.

But they should emerge from the morphology rather than define it.

## Immediate Next Step

The next Bonsai artifact should likely classify one real C6 subsystem using this morphology in detail.

Best candidate:

- GPIO + IO mux + interrupt edge handling

That would give GOOSE its first concrete hardware-grounded ontological case study.
