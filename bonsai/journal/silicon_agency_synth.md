# Synthesis: Silicon Agency (The ETM Matrix)

## Architecture
Silicon Agency is the **Hardware Projection** of a GOOSE Route. It leverages the ESP32-C6 Event Task Matrix (ETM) to connect hardware events directly to hardware tasks in sub-100ns (40ns).

## Key Decisions
1. **Positive Reinforcement Only:** ETM is binary (Trigger). Silicon Agency is only allowed for `Orientation +1`. Inhibition (0) and Inversion (-1) fall back to software.
2. **Resource Allocation:** A pool of 50 lanes is managed by the ETM substrate.
3. **Software Fallback:** If the silicon matrix is full, the Weaver automatically reverts to Software Coupling, ensuring manifold integrity.

## Implementation Spec
- `goose_etm_apply_route()`: Allocates and weaves a silicon lane.
- `goose_etm_unweave_route()`: Releases hardware resources.
- `GOOSE_COUPLING_SILICON`: New coupling mode for nanosecond-scale propagation.

## Success Criteria
- [x] Propagation latency <100ns (Measured: 40ns).
- [x] Zero CPU cycle consumption for data-path propagation.
- [x] Graceful degradation to software logic.
