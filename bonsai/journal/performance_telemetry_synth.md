# Synthesis: Performance Telemetry (Instrumentation)

## Architecture
Performance Telemetry provides the Mothership with **Temporal Self-Awareness**. It uses cycle-accurate CPU counters to measure the "Friction" of every geometric transition.

## Key Decisions
1. **Phase-Level Granularity:** Instrumentation tracks cycles spent in `Evolution`, `Propagation`, and `Coherence` separately.
2. **Lock-Contention Tracking:** Specifically measures time wasted in trans-core spinlocks to identify "Geometric Jitter."
3. **Low-Overhead:** Uses native RISC-V cycle counters to minimize the observer effect.

## Implementation Spec
- `goose_stats_t`: Structure embedded in every `goose_field_t`.
- `esp_cpu_get_cycle_count()`: The primary measurement tool.
- `goose_field_get_stats()`: The API for real-time monitoring.

## Success Criteria
- [x] Real-time tracking of pulse budgets.
- [x] Identification of "The Swap Storm" and "Lattice Collisions."
- [x] instrumentation overhead <2% of total pulse budget.
