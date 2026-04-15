# Synthesis: Phase 27 (Geometric AI)

## Architecture
Moving from "Static Topologies" to "Learning Manifolds." We will implement a self-sculpting substrate where the Supervisor acts as a neural optimizer, adjusting the Tapestry to minimize a "Pain Trit."

## Key Decisions
1. **The Probabilistic Neuron:** A `FIELD_PROXY` can now be configured as a `GOOSE_CELL_NEURON`. It aggregates state from all incoming routes and flips state based on a **Geometric Quorum** (Inertia).
2. **Topological Plasticity:** A new Supervisor pass `goose_supervisor_learn_sync`.
   - If `sys.ai.pain` is -1 (Fault): Incrementally shift the orientation of the manifold's routes until equilibrium is restored.
   - If `sys.ai.reward` is +1 (Success): "Pin" the current orientations to prevent further plasticity.
3. **Silicon Summation:** Implement **Masked Writes** in the propagation loop to allow multiple routes to "vote" on a single hardware register bitfield without overwriting neighbors.

## Implementation Spec
### 1. New Cell Type & Fields
```c
#define GOOSE_CELL_NEURON 0x08
// Cell.bit_mask = The "Inertia" (how many route hits needed to flip)
// Cell.state = The aggregated result (+1, 0, -1)
```

### 2. Learned Orientation
Update `goose_route_t` to store `learned_orientation` in DRAM. The Supervisor modifies this, while the original `orientation` remains as the "Innate Instinct" (the baseline).

### 3. Masked Propagation
Update `goose_process_transitions`:
- When writing to a `HARDWARE_OUT` sink:
  - `*reg = (*reg & ~bit_mask) | (new_state ? bit_mask : 0)`

## Success Criteria
- [ ] A 3-node manifold can "learn" to avoid a `sys.ai.pain` trigger by inhibiting its own routes.
- [ ] Sub-100ns propagation for neural layers using Silicon Agency.
- [ ] Zero-instruction inference (no TASM loop needed for signal flow).
