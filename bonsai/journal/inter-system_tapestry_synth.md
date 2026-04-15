# Synthesis: Inter-System Tapestry (ESP-NOW)

## Architecture
The Inter-System Tapestry virtualizes the `Atmosphere` as a shared GOOSE field. It enables **Geometric Bleed**, where the Loom of one node propagates into another via asynchronous radio routes.

## Key Decisions
1. **The Remote Field (-1):** All remote state is addressed via `Field = -1`. The `Region` component (-13 to +13) maps to discovered `Node IDs`.
2. **Coupling:** `GOOSE_COUPLING_RADIO` handles the asynchronous transition from local RTC RAM to the radio hardware.
3. **Consistency:** **Postural Pressure** replaces ACKs. The system assumes "Eventually Consistent Geometry." If a radio pulse fails, the state simply remains in disequilibrium until the next `DISTRIBUTED` rhythm pulse (5Hz).
4. **Security:** **Mutual Weaving** ensures that only pre-authorized geometric paths can influence local hardware.

## Implementation Specification

### 1. Data Structures (goose.h)
```c
typedef enum {
    GOOSE_COUPLING_RADIO = 3 // New: ESP-NOW bridge
} goose_coupling_t;

typedef enum {
    GOOSE_RHYTHM_DISTRIBUTED = 5 // New: 5Hz Radio Pulse
} goose_rhythm_t;
```

### 2. ESP-NOW Packet Format (Geometric Packet)
The "Packet" is just a serialized trit-vector:
- `SourceCoord (9 trits)`
- `State (1 trit)`
- `Nonce (for authentication/freshness)`

### 3. The Radio Bridge Fragment
A reusable pattern that nodes "weave" to enable sharing.
- **Cells:**
    - `[0] Status`: -1 (Disconnected), 0 (Searching), 1 (Synced)
    - `[1] PeerID`: The assigned Node ID in the local Loom.
    - `[2..N] ShadowCells`: Local mirrors of remote states.

## Implementation Plan
1. **Scaffold `goose_atmosphere.c`**: Implement ESP-NOW init and the basic send/recv loop.
2. **Update `goose_process_transitions`**: Add logic to catch `GOOSE_COUPLING_RADIO` routes.
3. **Weave a "Blinky Bridge"**: Create a demo where Node A's button coordinate is routed to Node B's LED coordinate via a `Radio Bridge`.

## Success Criteria
- [ ] State propagation between two C6 nodes with <200ms latency.
- [ ] Disconnection results in `Status` cell moving to -1 and `ShadowCells` returning to 0 (Neutral).
- [ ] No symbolic names are used in the transmission; only geometric coordinates.
