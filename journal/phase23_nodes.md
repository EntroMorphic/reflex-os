# Nodes of Interest: Phase 23 (Atmospheric Discovery)

## Node 1: The Asynchronous Gap
Binary C code expects `resolve` to be instant. Radio discovery is slow (~10-50ms).
*Tension:* Blocking `app_main` vs. returning a "Null-but-Waking" cell that populates later.

## Node 2: Ghost Cell Identity
A Ghost Cell must store more than just a coordinate. It needs a **MAC Address Target**.
*Impact on Hearth:* Do we need a new struct for `goose_ghost_cell_t`? We have no more RTC RAM. We must reuse the `hardware_addr` field to store a pointer to a "Remote Descriptor."

## Node 3: The "Aura" of Trust
If Node A asks for a fan, Node B must prove it owns the fan.
*Discovery Security:* Discovery responses must be Aura-checked to prevent a rogue node from redirecting all local intent to its own pins.

## Node 4: Scoping (Local vs. Global)
`agency.led.intent` (Local) vs. `node_b.agency.led.intent` (Global).
*Naming Convention:* G.O.O.N.I.E.S. needs a "Peer Prefix" system to avoid name collisions in the distributed mesh.

## Node 5: Atmospheric Saturation
Too many discovery queries will drown the "Postural Signal" (the 5Hz sync pulse).
*Bandwidth:* Discovery needs to be throttled or moved to a dedicated "Control Channel" in ESP-NOW.
