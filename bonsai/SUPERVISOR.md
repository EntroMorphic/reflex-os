# Phase 9: The Geometric Supervisor (Regulator Field)

## Objective
To implement the **Harmonic Supervisor**: a meta-level GOOSE field that regulates the Tapestry to maintain system equilibrium.

## Core Concepts

### 1. The Regulator Field
A specialized `goose_field_t` that perceives the state of other fields and has agency over their routes.

### 2. Equilibrium
A region is in equilibrium if its `sink` cells are consistent with its `source` cells and `route` orientations. 
- **Disequilibrium:** A state where the physical reality (Sink) deviates from the intended geometry (Source * Route).

### 3. Meta-Routes
Routes where the **Sink** is another route's `orientation`. This allows the Supervisor to "tilt" the system's behavior by flipping routes from `+1` (Admit) to `0` (Inhibit) or `-1` (Invert).

## Technical Specification

### `goose_supervisor_t`
```c
typedef struct {
    goose_field_t *target_field;
    goose_cell_t *health_cell;
    esp_err_t (*rebalance_fn)(goose_field_t *field);
} goose_regulator_t;
```

### The Balance Loop
The Supervisor runs as a high-priority "Pulse" that:
1. Samples the **Perception Nodes** of the Tapestry.
2. Calculates the **System Tilt** (The deviation from equilibrium).
3. Applies **Meta-Route** updates to "re-level" the manifold.

## Success Criteria
- **Detection:** The Supervisor can detect when a route is inhibited.
- **Correction:** The Supervisor can automatically re-enable or invert a route to restore a "Healthy" signal.
- **Zero-Logic Agency:** The correction happens via geometric re-patching, not imperative recovery code.
