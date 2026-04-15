# Nodes of Interest: Inter-System Tapestry (ESP-NOW)

## Node 1: Spatial Node-Mapping
The 9-trit coordinate system (`tryte9`) has enough entropy ($3^9$) to include a Node ID.
- **Current Map:** `[F:3 trits][R:3 trits][C:3 trits]`
- **Proposed Global Map:** `[Node:3 trits][Field:2 trits][Region:2 trits][Cell:2 trits]`?
- **Alternative:** Keep the 3/3/3 structure but reserve `Field` trits `[-1, -1, -1]` to `[-1, 1, 1]` for Remote Nodes.
Why it matters: We need a way to refer to "The LED on Node 4" using only trits.

## Node 2: The Atmosphere Field
The "Air" between nodes is a `Field` where influence propagates with high latency and potential loss.
- Routes across this field are `GOOSE_COUPLING_RADIO`.
- The ESP-NOW MAC address is the "Physical Bridge" but should be hidden behind the geometric coordinate.

## Node 3: State Pressure (The Immune System for Radio)
Traditional networking uses ACK/Retry. GOOSE uses **Postural Equilibrium**.
- If `Source (Node A)` and `Sink (Node B)` are out of sync, the "Atmosphere" is in disequilibrium.
- Node A will continue to pulse the state change (Pressure) until the field settles.
Tension: Power consumption. We can't pulse radio at 100Hz.

## Node 4: The Radio Bridge Fragment
A standard pattern for "Exporting" or "Importing" state.
- **Exporter:** Watches a local cell, projects to ESP-NOW.
- **Importer:** Listens to ESP-NOW, updates a local shadow cell.
- This keeps the core runtime unaware of the radio specifics.

## Node 5: Authenticated Geometry
If Node A can change Node B's state, it must be "Lawful."
- In GOOSE, routes are established by the `Weaver`. 
- An Inter-System Route must be "Woven" on BOTH ends to be valid.
- This replaces traditional binary handshakes with "Mutual Weaving."

---

## Tensions
1. **MAC vs. Coord:** How to map a 48-bit MAC to 3 or 9 trits without a lookup table (which is "software")? Or is the lookup table part of the "Atlas"?
2. **Rhythm vs. Radio:** Radio requires long-running tasks or async callbacks. This conflicts with the synchronous `pulse` of the Loom.
3. **Loss vs. Logic:** If a transition is "Momentary" (Pulse), and the packet is lost, the state is lost. GOOSE favors "Persistent State" over "Events."
