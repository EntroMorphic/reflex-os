# Synthesis: Phase 28 (Autonomous Fabrication)

## Architecture
Moving from "Static Configuration" to "Reactive Self-Assembly." We will implement a system where the OS perceives unfulfilled "Needs" and autonomously weaves new geometric routes to satisfy them.

## Key Decisions
1. **The NEED Primitive:** A new cell type `GOOSE_CELL_NEED` is introduced. 
   - State +1: Active desire.
   - State +15: Survival mandate.
2. **Capability Matching:** G.O.O.N.I.E.S. resolution is extended to support **Sub-Zone Suffixes** as capability tags (e.g. `*.led` implies light agency).
3. **The Weaver (Supervisor Pass):** A new autonomic regulation loop `goose_supervisor_weave_sync`.
   - Scans all active `GOOSE_CELL_NEED` nodes.
   - For each active need, it searches the Shadow Atlas for a matching capability node.
   - If a match is found, it "hot-weaves" a new route into a dedicated `sys.autonomy` field.

## Implementation Spec
### 1. New Cell Type
```c
#define GOOSE_CELL_NEED 0x09
```

### 2. Capability Query
A function `goonies_resolve_by_capability(const char *suffix)` that returns the first node in G.O.O.N.I.E.S. or the Shadow Atlas that matches the given tag.

### 3. Fabrication Field
A pre-allocated field `sys.autonomy` that can hold up to 16 "Fabricated Routes" that are woven/unwoven on-the-fly.

## Success Criteria
- [ ] Setting `sys.need.light` to +1 causes the OS to autonomously find `agency.led.intent` and weave a route to it.
- [ ] Unsetting the need causes the OS to "Unweave" the route, reclaiming the Loom slot.
- [ ] Survival mandates (+15) correctly prioritize Loom resources over soft desires.
