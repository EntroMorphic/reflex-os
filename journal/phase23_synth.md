# Synthesis: Phase 23 (Distributed GOONIES)

## Architecture
Moving from "Local Loom" to "Atmospheric Mesh." We will implement a reactive discovery protocol that allows geometric name resolution to traverse physical devices via ESP-NOW.

## Key Decisions
1. **The Peer Prefix:** Names starting with `peer.` (e.g., `peer.worker_a.agency.led`) trigger atmospheric discovery.
2. **Lazy Ghosting:** The resolver instantly returns a virtual "Ghost Cell" mapped to a unique remote coordinate.
3. **Ghost Solidification:** A background task broadcasts the `GOOSE_ARC_QUERY`. When a response arrives, the Ghost Cell's `hardware_addr` is updated with a pointer to the Peer MAC, enabling state synchronization.

## Implementation Spec
### 1. New Arc Packet Types
```c
#define ARC_OP_QUERY     0xDE // "Who has this name?"
#define ARC_OP_ADVERTISE 0xAD // "I have it, here is my MAC/Coord."
```

### 2. Peer Catalog (DRAM)
A 16-entry table mapping short IDs to MAC addresses to keep the Ghost Cells compact.

### 3. Atmospheric Probe Task
A low-priority task that re-broadcasts pending discovery queries every 500ms until solidified or timed out.

## Success Criteria
- [ ] Resolving a `peer.*` name creates a RAM Loom entry immediately.
- [ ] Board A can toggle Board B's LED by name without knowing Board B's MAC.
- [ ] Discovery responses are Aura-checked and MAC-verified.
