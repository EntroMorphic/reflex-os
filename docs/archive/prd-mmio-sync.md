# PRD: MMIO Sync Layer — Unified Distributed Hardware Surface

## Problem Statement

Each Reflex board operates in isolation. Board A cannot read Board B's temperature sensor or control Board B's LED without manual Python scripting. The mesh carries arcs but no hardware state. The substrate knows how to propagate signals through routes, but only to local cells.

## Goal

Any cell on any board is addressable from any other board in the mesh. The substrate becomes distributed: route propagation crosses board boundaries transparently. The user experience:

```
reflex> goonies find peer.bravo.perception.temp.reading
peer.bravo.perception.temp.reading state=+1 (warm)

reflex> mesh peer add bravo B4:3A:45:8A:C8:24
reflex> purpose set monitor
# Substrate now routes from remote temp cells with priority boost
```

## Non-Goals

- Guaranteed delivery (fire-and-forget broadcast, like UDP)
- Ordered delivery (state is idempotent — latest value wins)
- Full distributed consensus (no Raft/Paxos — single-owner model)
- Remote code execution (VM programs stay local)

---

## Architecture

### Core Mechanism

When the pulse engine evaluates a route and the **sink cell is remote** (has `peer_id != 0`), it emits a sync arc instead of writing a local register. This is ONE conditional branch in `internal_process_transitions`:

```c
if (r->cached_sink->peer_id != 0) {
    goose_mmio_sync_emit(r->cached_sink, new_state);
} else {
    // existing local register write
}
```

### Data Model

**Cell extension:**
```c
// Added to goose_cell_t:
uint8_t peer_id;  // 0 = local, 1-255 = remote peer index
```

**Peer registry:**
```c
#define MAX_PEERS 8
typedef struct {
    char name[12];
    uint8_t mac[6];
    uint64_t last_seen_us;
    bool active;
} reflex_peer_t;

static reflex_peer_t s_peers[MAX_PEERS];
```

**Sync arc format:**
```c
#define ARC_OP_MMIO_SYNC 0x10

typedef struct {
    uint8_t  op;          // ARC_OP_MMIO_SYNC
    uint8_t  direction;   // SYNC_BROADCAST=0, SYNC_WRITE=1
    uint8_t  peer_id;     // Source peer (for broadcast) or target (for write)
    int8_t   state;       // Ternary value (-1, 0, +1)
    char     cell_name[20]; // GOONIES name for routing
} __attribute__((packed)) mmio_sync_arc_t;
```

### Three Operations

| Operation | Direction | When | Security |
|-----------|-----------|------|----------|
| **State Broadcast** | Owner → mesh | Every pulse for hot-tier cells | Read-only (receivers update phantom cells) |
| **Remote Write** | Requester → owner | Route propagation to remote sink | Sanctuary Guard on receiver (agency.* only) |
| **Staleness Reset** | Timer-driven | 5s without update from peer | Phantom cell state → 0 |

### Peer Registry

Peers are registered by name + MAC:

```
reflex> mesh peer add bravo B4:3A:45:8A:C8:24
reflex> mesh peer add charlie B4:3A:45:8B:D9:35
reflex> mesh peer ls
  1: bravo  B4:3A:45:8A:C8:24  active (last: 1.2s ago)
  2: charlie B4:3A:45:8B:D9:35  active (last: 0.8s ago)
```

Auto-registration: when an arc is received from an unknown MAC, a peer entry is created with name `auto_<last4>` (e.g., `auto_C824`).

### GOONIES Resolver Extension

`goonies_resolve_cell("peer.bravo.agency.led.intent")`:
1. Strip `peer.` prefix
2. Extract peer name (`bravo`)
3. Look up peer_id from registry
4. Strip peer name, resolve remaining (`agency.led.intent`) locally
5. Return a **phantom cell** with `peer_id` set

If the cell doesn't exist locally, allocate one from the fabric with `hardware_addr = 0` and `peer_id = registry_index`.

### Broadcast Strategy

| Tier | Cells | Frequency | Bytes/sec |
|------|-------|-----------|-----------|
| Hot | `agency.*` intent cells (max 5) | 10Hz (pulse rate) | ~130 B/s |
| Warm | `perception.*` sensors (max 3) | 1Hz | ~13 B/s |
| Cold | All others | On-change only | ~0 (rare) |

**Total bandwidth per board:** <150 B/s. ESP-NOW capacity: ~30KB/s. Margin: 200×.

### Staleness

In `goose_supervisor_pulse`:
```c
for (each peer) {
    if (now - peer.last_seen_us > 5_000_000) {
        // Reset all phantom cells for this peer to state=0
        peer.active = false;
    }
}
```

State=0 is neutral — routes depending on stale cells naturally dampen without explicit error handling.

### Security

| Rule | Enforcement point |
|------|------------------|
| Remote writes only to `agency.*` | Receive handler checks namespace before applying |
| `sys.*` never remotely writable | Receive handler rejects |
| `perception.*` broadcast-only | Receive handler rejects writes |
| Authentication | Existing Aura HMAC on all arcs |
| Replay protection | Existing 16-slot replay cache |

---

## Implementation Plan

### Files to Create

| File | Purpose | ~LOC |
|------|---------|------|
| `components/goose/goose_mmio_sync.c` | Sync logic: broadcast, receive, staleness | ~120 |
| `components/goose/goose_mmio_sync.h` | API: init, emit, peer registry | ~25 |

### Files to Modify

| File | Change | ~LOC |
|------|--------|------|
| `components/goose/include/goose.h` | Add `peer_id` to cell, `ARC_OP_MMIO_SYNC`, peer API | ~15 |
| `components/goose/goose_runtime.c` | Remote-sink check in propagation + phantom cell alloc in resolver | ~25 |
| `components/goose/goose_atmosphere.c` | Register sync arc handler in receive callback | ~10 |
| `components/goose/goose_supervisor.c` | Staleness timeout in pulse | ~15 |
| `shell/shell.c` | `mesh peer add/ls`, `mesh sync` commands | ~30 |

### Total: ~240 LOC

---

## Validation Plan

### Unit Tests (host)

Add to `tests/host/test_integration.c`:
- Phantom cell allocation with peer_id
- Sync arc encode/decode round-trip
- Staleness timeout resets state to 0
- Security: write to `sys.*` rejected, `agency.*` accepted

### Hardware Validation (two boards)

1. Flash Alpha and Charlie with same firmware + same aura key
2. `mesh peer add charlie <mac>` on Alpha
3. `mesh peer add alpha <mac>` on Charlie  
4. Charlie: `purpose set led` → LED activity
5. Alpha: `goonies find peer.charlie.perception.temp.reading` → shows Charlie's temp state
6. Alpha: verify state updates every 1s (warm tier broadcast)
7. Unplug Charlie → Alpha's phantom cells reset to 0 after 5s
8. Replug Charlie → state resumes

### Python SDK Validation

```python
from reflex import ReflexNode
alpha = ReflexNode("/dev/cu.usbmodem1101")
alpha.raw("mesh peer add charlie B4:3A:45:8B:D9:35")
# Wait for sync
out = alpha.goonies_find("peer.charlie.perception.temp.reading")
assert "state=" in out
```

---

## Risks

| Risk | Severity | Mitigation |
|------|----------|-----------|
| Bandwidth exceeds ESP-NOW capacity | LOW | <150 B/s << 30KB/s capacity |
| Phantom cells fill 256-slot fabric | MEDIUM | Eviction policy: phantom cells evicted first |
| Two boards write same remote cell | LOW | Last-write-wins (v1); lease model (v2) |
| Bravo still wedged (only 2 boards) | MEDIUM | Alpha + Charlie sufficient for validation |
| Arc parsing bug corrupts local state | HIGH | Validate arc length + cell name before apply |

---

## Success Criteria

1. Two boards with same aura key see each other's sensor states within 2s of boot
2. Setting purpose on one board changes priority of tasks reading remote cells on the other
3. Unplugging one board causes the other's phantom cells to reset to 0 within 5s
4. Unauthorized remote writes (to `sys.*`) are rejected silently
5. Total mesh bandwidth < 500 B/s with 2 boards + 3 hot cells each

---

## Dependencies

- R1 (KV compaction) — DONE
- R2 (integration test) — DONE
- Two operational boards — Alpha + Charlie confirmed
- Same aura key on both boards

## Effort

~240 LOC implementation + ~50 LOC tests + ~30 LOC shell commands = **~320 LOC total. 1 day.**
