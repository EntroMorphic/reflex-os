# Nodes of Interest: Phase 28 (Autonomous Fabrication)

## Node 1: Intent-Driven Weaving (The NEED Cell)
Autonomy requires a "Why." We define this using `sys.need.*` cells.
*Why it matters:* This prevents the OS from weaving random routes. It only weaves if there is a goal (e.g. `sys.need.light`).

## Node 2: Ontological Matching (Semantic Glue)
How does the OS know that `agency.led` fulfills `sys.need.light`?
*Innovation:* We need a **Semantic Tag** system in G.O.O.N.I.E.S. where names carry "Capabilities."
*Tension:* Complex metadata vs. limited RAM. We will use **Sub-Zone Suffixes** (e.g. `*.light`, `*.temp`) to imply capabilities.

## Node 3: Dynamic Manifold Synthesis
The machine must create new `goose_field_t` structures at runtime.
*Constraint:* Heap memory fragmentation. We need a "Fabrication Pool" of pre-allocated fields that can be "hot-swapped."

## Node 4: The "Self-Weaving" Supervisor
The Supervisor becomes the "Weaver." It continuously monitors the `sys.need` zone and scans the `G.O.O.N.I.E.S.` registry for matches.
*Red-Team Factor:* What if a fake node advertises a "Need-Fulfilling" capability? (Aura validation required).

## Node 5: Autonomous Discovery Loop
Combining Phase 23 and 28. If a local board has a "Need" it can't fulfill, it broadcasts a **Capability Query** to the mesh.
*Evolution:* From "Who has this name?" to "Who can do this task?"
