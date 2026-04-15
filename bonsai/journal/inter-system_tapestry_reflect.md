# Reflections: Inter-System Tapestry (ESP-NOW)

## Core Insight
Inter-System Tapestry is the **Geometric Projection of Authority** across the Atmosphere. Communication is a side-effect of maintaining a shared posture between disparate Looms.

## Resolved Tensions

### 1. MAC vs. Coord (The Atlas Bridge)
We will not use a symbolic lookup table. Instead, we treat the ESP-NOW MAC address as a **Physical Constant** of a `Remote Region`. 
- Every node has an "Atlas" that defines its local identity.
- When two nodes "Discover" each other, they exchange their `Origin` trits.
- The `Weaver` creates a `Remote Field` (Field = -1).
- Sub-fields in the `Remote Field` are assigned to specific Node IDs (derived from a hash of the MAC, or simply the order of appearance).
- Coordinate `(-1, NodeID, Cell)` becomes the "Geometric Ghost" of the remote state.

### 2. Rhythm vs. Radio (The Async Pulse)
Radio is too slow for the `REACTIVE` pulse. 
- We introduce `GOOSE_RHYTHM_DISTRIBUTED` (1-10Hz).
- The `Radio Bridge` fragment contains a `Buffer Region` in RTC RAM.
- The `REACTIVE` loop writes to the `Buffer Region` (instant, local).
- The `DISTRIBUTED` loop (Radio Pulse) sweeps the `Buffer Region` and propagates changes over ESP-NOW.
- This "Partitions" the high-speed logic from the high-latency transport.

### 3. Loss vs. Logic (Postural Pressure)
We abandon ACK/Retry at the protocol level.
- If a route is `DISTRIBUTED`, the source node maintains a "Pressure" state.
- Every `N` radio pulses, the source node broadcasts its entire `Exported Region` state.
- The sink node "re-aligns" its local shadow cells to match the broadcast.
- This ensures eventual consistency without complex binary state machines.

## Authenticated Geometry (Red-Team Resolution)
- A node only accepts state updates for cells that have an established `Remote Route`.
- "Weaving" an Inter-System Route requires a cryptographic hash of the `(SourceCoord, SinkCoord, Orientation)` triple.
- Without a valid "Weave Key," the incoming radio influence is treated as "Geometric Noise" and discarded.

## Simple Architecture
- `Radio Bridge Fragment`: 
    - `Perception Cell`: Radio Status (Up/Down/Noise).
    - `Agency Cell`: Transmit Power / Channel.
    - `Mirror Region`: A collection of shadow cells that track remote states.
- `ESP-NOW Substrate`: A thin layer in `goose_runtime.c` that maps `GOOSE_COUPLING_RADIO` to `esp_now_send`.
