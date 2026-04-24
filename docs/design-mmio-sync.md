# MMIO Sync Layer — Unified Distributed Hardware Surface

## Design

### Vision

Every board in the Reflex mesh exposes its hardware to every other board. The 12,738-node Atlas on each device merges into a **single distributed Tapestry** where `peer.bravo.perception.temp.reading` is as addressable as `perception.temp.reading` on the local board — and the value is live.

### Architecture

```
Board A                          Board B
┌─────────────────┐              ┌─────────────────┐
│ Atlas (12,738)   │              │ Atlas (12,738)    │
│ Local cells     │──── Arc ────→│ Remote cells     │
│ Remote cells    │←─── Arc ─────│ Local cells      │
│                 │              │                  │
│ MMIO registers  │              │ MMIO registers   │
└─────────────────┘              └─────────────────┘
         ↕                                ↕
      GPIO, LEDC,                     GPIO, LEDC,
      Temp, RNG...                    Temp, RNG...
```

### Sync Arc Format

A new arc type (`ARC_OP_MMIO_SYNC`) carries hardware state:

```c
typedef struct {
    uint8_t  op;           // ARC_OP_MMIO_SYNC (new opcode)
    uint8_t  direction;    // SYNC_READ_REQUEST, SYNC_WRITE, SYNC_STATE_BROADCAST
    uint32_t addr;         // MMIO register address
    uint32_t mask;         // Bit mask (which bits are relevant)
    uint32_t value;        // Register value (for writes/broadcasts)
    char     cell_name[24]; // GOONIES name for routing
} mmio_sync_arc_t;
```

### Three Operations

1. **State Broadcast** — A board periodically broadcasts its hardware cell states. Receivers update their `peer.*` shadow cells. No write occurs — read-only mirroring.

2. **Remote Write** — Board A writes to `peer.bravo.agency.led.intent`. The arc carries the write to Board B, which applies the value to its local `agency.led.intent` cell and the underlying GPIO register.

3. **Read Request** — Board A requests the value of `peer.bravo.perception.temp.reading`. Board B responds with a state broadcast for that cell.

### Ownership Model

Each MMIO register has exactly ONE owner — the board whose physical hardware it maps to. Rules:

- **Local writes**: always permitted (you own your hardware)
- **Remote writes**: permitted only for cells in the `agency.*` namespace (intent cells that the Sanctuary Guard allows external control of)
- **Perception cells**: read-only broadcast (sensors report, they don't accept writes)
- **System cells**: never remotely writable (`sys.*` namespace protected)

### Integration Points

| Existing component | Role in MMIO Sync |
|---|---|
| Atlas (`goose_shadow_atlas.c`) | Provides addr+mask for any named MMIO node |
| Atmosphere (`goose_atmosphere.c`) | Transports sync arcs between boards |
| GOONIES (`goose_runtime.c`) | Resolves `peer.*` names to remote cells |
| Aura (`reflex_crypto.h`) | Authenticates sync arcs (HMAC-SHA256) |
| Sanctuary Guard | Enforces ownership (blocks unauthorized remote writes) |
| Pulse engine | Triggers periodic state broadcasts |

### New Code Required

| File | Purpose | ~LOC |
|------|---------|------|
| `components/goose/goose_mmio_sync.c` | Sync arc encode/decode, broadcast scheduler, receive handler | ~150 |
| `components/goose/goose_mmio_sync.h` | Public API: `goose_mmio_sync_init`, `goose_mmio_sync_write_remote` | ~30 |
| Modify `goose_atmosphere.c` | Register new arc op handler | ~10 |
| Modify `goose_runtime.c` | Resolve `peer.*` to remote cells transparently | ~20 |

**Total: ~210 lines of new code.**

### Broadcast Strategy

Not every register broadcasts every cycle. Three tiers:

| Tier | Frequency | Cells | Example |
|------|-----------|-------|---------|
| Hot | Every pulse (10Hz) | Intent + active output | agency.led.intent, agency.rmt.ch0 |
| Warm | 1Hz | Sensors + status | perception.temp.reading, sys.purpose |
| Cold | On-change only | Config + rare state | sys.boot_count, aura key hash |

This keeps mesh bandwidth ~50 bytes/sec per board even with full Atlas sync.

### Security

- All sync arcs carry Aura authentication (existing HMAC)
- Sanctuary Guard rejects writes to protected namespaces
- A board can declare cells as "sync-private" (never broadcast)
- Replay cache prevents stale state injection

### User Experience

After MMIO sync is enabled:

```
Board A> goonies find peer.bravo.perception.temp.reading
peer.bravo.perception.temp.reading coord=[remote] state=+1 (warm)

Board A> purpose set monitor
# Board A now boosts tasks that route from peer temperature cells

# From Python SDK:
from reflex import ReflexNode
a = ReflexNode("/dev/cu.usbmodem1101")
print(a.goonies_find("peer.bravo.perception.temp.reading"))
# → "state=+1 (warm)"
```

### Latency

| Operation | Expected latency |
|-----------|-----------------|
| State broadcast → peer receives | 5-20ms (ESP-NOW), 10-50ms (802.15.4) |
| Remote write → hardware changes | 10-40ms (one arc round-trip) |
| Read request → response | 20-80ms (request + response arcs) |

---

## Lincoln Manifold Method

### RAW

Brain dump — everything about this idea, unfiltered:

- This is essentially distributed shared memory but for MMIO registers. Every board sees every other board's hardware.
- The Atlas already knows every register address and its bit mask. The naming system already handles `peer.*` prefixes. The atmosphere already sends authenticated packets. We're just wiring them together.
- Bandwidth is the real constraint. 12,738 registers × 4 bytes × 10Hz = 510KB/s. That's way more than ESP-NOW (250Kbps effective). Must be selective.
- The tiered broadcast (hot/warm/cold) solves bandwidth. Most registers don't change. Only broadcast deltas or on-change.
- Security: who gets to write my LED? The Sanctuary Guard already has namespace-based access control. `agency.*` is writable, `sys.*` is not, `perception.*` is read-only. This maps perfectly.
- What about conflicts? Two boards both try to write the same remote LED at the same time. Last-write-wins is simple. Or: only one board "owns" each remote cell at a time (lease model). For v1: last-write-wins.
- The GOONIES registry already supports peer resolution. `goonies_resolve_cell("peer.bravo.agency.led.intent")` should return a cell whose hardware_addr is zero (remote) with a flag indicating it needs arc-based access.
- The pulse engine already evaluates routes. If a route's sink is a remote cell, the propagation should emit a sync arc instead of a local register write. This is the key integration point.
- What if a board goes offline? Its peer cells go stale. Need a heartbeat-based timeout — after 5s without broadcast, mark peer cells as state=0 (neutral/unknown).
- The LP coprocessor heartbeat already ticks at 1Hz. Could piggyback sync state on heartbeat arcs for zero-additional-cost.
- This turns N boards into one machine. Physically separate but logically unified. The substrate doesn't care where the hardware lives — it just propagates state.
- Risk: debugging becomes harder when effects are non-local. Board A's LED turns on because Board B's temperature crossed a threshold and a route propagated across the mesh. Observability is critical.
- Existing `mesh status` command shows arc counts. Could add `mesh sync` to show which remote cells are active and their last-update timestamps.
- The VM could address remote hardware too. A TASM program running on Board A could syscall `write_cell("peer.bravo.agency.led.intent", +1)` and the LED on Board B turns on. One program, distributed hardware.
- Power: broadcasting state costs radio TX energy. Boards in deep sleep shouldn't broadcast. The sync layer should respect the sleep state.
- The implementation is small because the infrastructure exists. The arc transport, authentication, naming, and access control are all done. We're adding a new arc type and a handler.

### NODES

Key points, tensions, and constraints extracted:

1. **Core mechanism**: Route propagation to remote cells emits sync arcs instead of local register writes.
2. **Bandwidth constraint**: Full Atlas sync is impossible (381KB/s > radio capacity). Must be selective.
3. **Solution to #2**: Three-tier broadcast (hot/warm/cold) + delta-only for cold tier.
4. **Ownership is singular**: Each physical register has exactly one owner board. Remote writes go through the owner.
5. **Security maps cleanly**: `agency.*` = writable, `perception.*` = broadcast-only, `sys.*` = local-only.
6. **Staleness problem**: Boards go offline. Peer cells must timeout to neutral after N seconds.
7. **Existing infrastructure covers 90%**: Atlas, Atmosphere, GOONIES, Aura, Sanctuary Guard.
8. **New code is small**: ~210 lines — arc format, broadcast scheduler, receive handler.
9. **Debugging complexity increases**: Non-local effects require observability tooling.
10. **Conflict resolution**: Last-write-wins for v1. Lease model possible for v2.
11. **Power management**: Sync must respect sleep state — don't broadcast from sleeping boards.
12. **VM integration**: TASM programs can address distributed hardware through the same cell API.

**Tensions:**
- Simplicity vs completeness (broadcast everything vs selective sync)
- Latency vs bandwidth (more frequent = more responsive but more radio traffic)
- Security vs utility (strict namespace rules vs flexible remote access)
- Reliability vs complexity (guaranteed delivery vs fire-and-forget)

### REFLECT

Underlying structure and leverage points:

**The key insight**: Routes already propagate state from source to sink cells. The ONLY change needed is: when the sink cell has `hardware_addr = 0` and a `peer_mac` field, the propagation engine emits a sync arc instead of a register write. Everything else — naming, security, authentication, transport — is already solved.

**Leverage point**: The route propagation is the single point of integration. We don't need a separate "sync engine" that runs alongside the substrate. The substrate IS the sync engine. Every route evaluation naturally handles both local and remote cells.

**The tiered broadcast is secondary**: The primary mechanism is route-driven sync (on-change, deterministic). The background broadcast is a consistency fallback for cells that change via direct register manipulation (hardware interrupts, DMA) rather than substrate routes.

**Staleness timeout maps to existing pain signal**: A stale peer cell (no update in 5s) sets its state to 0, which is equivalent to the pain/neutral signal in the Hebbian system. Routes depending on stale peers naturally dampen — the system degrades gracefully.

**The ownership model is the Sanctuary Guard extended to the mesh**: Today, Sanctuary Guard protects local `sys.*` cells from local writes. Tomorrow, it also protects them from remote writes. The enforcement point is the same code path — just add a check for arc source MAC.

**Dependencies**:
- The sync arc handler needs to be in the atmosphere receive callback chain
- The route propagation engine needs a 1-line check: "is sink remote? → emit arc"
- The GOONIES resolver needs to allocate "phantom cells" for unresolved peer names
- The pulse scheduler needs to include broadcast responsibility for hot-tier cells

### SYNTHESIZE

Clear, actionable decisions:

**Decision 1: Route-driven sync is the primary mechanism.**
No separate sync engine. When the pulse evaluates a route and the sink is a remote cell, it emits a sync arc. This is ONE conditional branch in `internal_process_transitions` (~5 lines).

**Decision 2: Background broadcast is a consistency fallback only.**
Hot-tier cells (intent, active output) broadcast at pulse frequency. Warm-tier (sensors) broadcast at 1Hz. Cold-tier never broadcasts proactively — only on-change via route propagation. This bounds bandwidth to ~50 bytes/sec/board.

**Decision 3: Ownership enforced at receive, not send.**
Any board can SEND a write arc to any peer. The RECEIVING board's Sanctuary Guard decides whether to apply it. This keeps the send path simple and the security enforcement at the point of action.

**Decision 4: Phantom cells for unresolved peers.**
`goonies_resolve_cell("peer.bravo.agency.led.intent")` allocates a cell with `hardware_addr = 0` and stores the target peer MAC + cell name. The first sync arc received for that cell populates its state.

**Decision 5: Staleness → neutral.**
If a peer cell hasn't been updated in 5 seconds, its state resets to 0. This triggers natural route dampening without explicit error handling.

**Decision 6: v1 scope is broadcast + remote write. No read-request.**
State broadcast (receiver updates phantom cells) and remote write (sender emits arc, receiver applies to local hardware) cover 95% of use cases. Request-response adds complexity for marginal gain.

**Implementation order:**
1. Define `ARC_OP_MMIO_SYNC` and the arc format struct
2. Add phantom cell allocation in GOONIES resolver for `peer.*` names
3. Add remote-sink check in route propagation (emit arc instead of register write)
4. Add receive handler in atmosphere callback (apply state to phantom or local cell)
5. Add staleness timeout in supervisor pulse
6. Add `mesh sync` shell command for observability

**Total: ~210 lines. No architectural changes. The substrate naturally becomes distributed.**
