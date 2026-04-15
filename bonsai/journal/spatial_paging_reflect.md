# Reflections: Spatial Paging

## Core Insight
Spatial Paging is **Ontological Decoupling**. We separate the *Identity* of a cell (Semantic Name + Metadata) from its *Activity* (Current State + Hardware Mapping). By moving Identity to Flash, we free up the "Live" RTC RAM for thousands of concurrent active states.

## Resolved Tensions

### 1. Lookup vs. Latency (The Geometric TLB)
To avoid $O(N)$ lookups during high-frequency pulses, we will implement a **Spatial TLB**.
- The `goose_route_t` will keep a `cached_ptr` to the `goose_cell_t`.
- We add a `global_fabric_version`. 
- Every time a swap occurs (eviction or load), the `global_fabric_version` increments.
- If `route->cached_version != global_fabric_version`, the route performs a fresh lookup and updates its cache.
- This makes lookups $O(1)$ most of the time.

### 2. Page Fault in Pulse (Passive Consistency)
We will not block the pulse for a Flash read. 
- If a route source coordinate is not in the active Loom, the route is simply **Suppressed** (Orientation acts as 0).
- The Supervisor detects "Geometric Suppression" and schedules a background swap.
- The state "Flickers" into existence once the page is loaded.
- This maintains real-time timing at the cost of transient latency.

### 3. Power vs. Persistence (The LP Sentinel)
- The LP core only operates on **Page 0** (The Autonomic Hearth).
- All cells required for deep-sleep regulation must be woven into Page 0.
- The HP core manages the swapping of higher-order manifolds (Pages 1-N).
- This ensures that sleep-safe persistence is never broken for critical functions.

## Refined Specification
1.  **Compact Cell**: Remove `name[16]`. Reduce `goose_cell_t` to 19 bytes.
2.  **Geometric Routes**: `goose_route_t` stores `source_coord` and `sink_coord`.
3.  **Active Loom**: A fixed-size array in RTC RAM (e.g., 256 cells).
4.  **Spatial Store**: A partition in Flash that stores the full `goose_cell_metadata_t` (including names).
