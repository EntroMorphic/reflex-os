# Phase 7: Minimal GOOSE Runtime (Implementation Plan)

## Goal
To build the smallest possible C-side execution substrate that can manifest the GOOSE notation (`Field`, `Region`, `Route`, `Transition`) on the ESP32-C6.

## Core Components

### 1. Data Structures (`goose_types.h`)
- **Cell:** Atomic state container. Stores current ternary value (-1, 0, +1) and hardware mapping (MMIO address or GPIO bit).
- **Region:** A named collection of cells.
- **Field:** A top-level container for regions.
- **Route:** A persistent connection structure. Stores `source_cell`, `sink_cell`, and `orientation`.
- **Transition:** An evolution rule. Stores `rhythm` (timer handle), `coupling`, and the `rule` (function pointer or bytecode fragment).

### 2. The Fabric (State Manager)
- A global registry of active fields and routes.
- Responsible for "Applying" routes: translating a `route` object into a hardware configuration (e.g., calling `gpio_matrix_in/out`).

### 3. The Interpreter (Loader)
- **Phase A (Immediate):** A C-API for manual GOOSE construction (to verify structures).
- **Phase B (Integration):** A lightweight string parser for the Reflex OS shell to allow live patching.

### 4. The Pulse (Scheduler)
- A low-level interrupt-driven loop that processes "Async" transitions.
- Evaluates geometric products (e.g., $A \times B$) and updates "Sink" cells.

## Success Criteria
- Can load the `Experiment 003A` description via the runtime.
- Emergent LED behavior is driven by the GOOSE engine, not by hardcoded `shell.c` logic.
- Total memory footprint < 8KB.
