# Nodes of Interest: Spatial Paging

## Node 1: Semantic Displacement (Name vs. Coord)
The `name[16]` field in `goose_cell_t` consumes 45% of the cell's spatial footprint in RTC RAM.
- **Decision:** Separate the **Ontological Catalog** (Names + Full Metadata) from the **Live Fabric** (States + Addresses).
- Names move to Flash. Coordinates become the primary key for all Loom operations.

## Node 2: Immutable Hardware Anchors
Hardware-mapped cells (`HARDWARE_IN`, `HARDWARE_OUT`) cannot be swapped because their physical state is volatile and externally driven.
- **Constraint:** Page 0 is "The Solid Earth" and is never evicted.
- Virtual cells (`VIRTUAL`, `INTENT`) are "The Clouds" and can be drifted to Flash.

## Node 3: Geometric Pointers (The Dangling Link)
`goose_route_t` currently uses C pointers (`goose_cell_t*`). If a cell is swapped out and later swapped back into a different RTC RAM slot, the pointer breaks.
- **Decision:** Routes must use **Geometric Coordinates** instead of pointers.
- This introduces a lookup overhead. We need a **Spatial TLB** (Translation Lookaside Buffer) to cache recent coordinate-to-address mappings.

## Node 4: Swapping Rhythms
Flash writes are expensive and wear out the silicon.
- **Decision:** "Geometric Hibernation." Only swap regions when the system enters deep sleep or when a field's rhythm is significantly reduced (e.g., moving from `REACTIVE` to `AUTONOMIC`).

---

## Tensions
1. **Lookup vs. Latency:** $O(N)$ lookup for every route pulse is unacceptable at 1000Hz. Can we use a sorted Loom or a hash map?
2. **Page Fault in Pulse:** If `goose_process_transitions` hits a coordinate that is in Flash, should it block (causing jitter) or return `0` (Neutral) and trigger a background swap?
3. **Power vs. Persistence:** Swapping to flash requires the HP core to wake up. This kills the "Sleep-Safe" benefit of the RTC RAM Loom.
