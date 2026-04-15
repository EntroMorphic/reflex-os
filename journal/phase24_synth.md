# Synthesis: Phase 24 (Recursive Fields)

## Architecture
Moving from "Flat Tapestry" to "Hierarchical Holons." We will implement a new cell type that allows a Field to project its state as a single trit into a parent Field.

## Key Decisions
1. **Holon Projection:** A new cell type `GOOSE_CELL_FIELD_PROXY` is introduced. Its `hardware_addr` is reused to store a 32-bit hash of the target sub-field name.
2. **Synchronous Nesting:** When the `goose_process_transitions` loop encounters a Field Proxy, it immediately triggers the pulse of the sub-field.
3. **Semantic State Aggregation:** The state of the Field Proxy cell is automatically updated to match the state of the first cell (The Intent) of the sub-field.

## Implementation Spec
### 1. New Cell Type
```c
#define GOOSE_CELL_FIELD_PROXY 0x07 // Project a sub-field here
```

### 2. Holon Resolver
A function `goose_fabric_find_field_by_name(const char *name)` that searches the `global_fields` registry.

### 3. Nested Pulse Logic
Update `goose_process_transitions`:
- If `r->cached_source->type == GOOSE_CELL_FIELD_PROXY`:
  - Find field by name-hash.
  - Call `goose_process_transitions(sub_field)`.
  - Update `r->cached_source->state` from sub-field's first cell.

## Success Criteria
- [ ] Field A can use Field B's health (+1, 0, -1) as an input to its own transitions.
- [ ] Triggering Field A's pulse automatically pulses Field B (Synchronous nesting).
- [ ] No recursive stack overflow (depth limited to 3).
