# Synthesis: Phase 25 (Postural Swarms)

## Architecture
Moving from "Distributed Mesh" to "Geometric Organism." We will implement a probabilistic consensus mechanism using **Ternary Summation** to coordinate posture across multiple physical boards.

## Key Decisions
1. **Postural Arcs:** A new operation `ARC_OP_POSTURE` is introduced. It carries a **Weight Trit** (-1: Soft, 0: Normal, +1: Hard) indicating the confidence of the state.
2. **Hearth Accumulation:** Every board maintains a single `sys.swarm.accumulator` cell. Arriving postural arcs are summed into this cell (CurrentState * Weight).
3. **Inertial Flip:** The local board's `sys.swarm.posture` cell only flips when the accumulator crosses a "Static Inertia" threshold (e.g., +/- 10).

## Implementation Spec
### 1. New Arc Packet Feature
```c
#define ARC_OP_POSTURE   0xCC // Consensus/Coordination
// Packet.state = the proposed posture (-1, 0, +1)
// Packet.nonce = weight/priority (1-10)
```

### 2. Accumulator Logic
Update `goose_atmosphere.c`:
- If `arc->op == ARC_OP_POSTURE`:
  - `accum_cell->state += (arc->state * arc->nonce)`
  - If `accum_cell->state > THRESHOLD`: `local_posture = +1`
  - If `accum_cell->state < -THRESHOLD`: `local_posture = -1`

### 3. Swarm Regulation
A specialized Supervisor pass `goose_supervisor_swarm_sync` that slowly "bleeds" the accumulator back to 0 over time, ensuring the swarm eventually returns to neutral equilibrium if no fresh arcs arrive.

## Success Criteria
- [ ] Swarm of 3 nodes can reach a consensus on a "Threat" posture within 500ms.
- [ ] A single "Soft" node cannot flip the swarm (Probabilistic protection).
- [ ] No mesh-wide oscillation (Inertial Hysteresis).
