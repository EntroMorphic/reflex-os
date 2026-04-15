# Synthesis: Spatial Paging (The Virtual Manifold)

## Architecture
Spatial Paging virtualizes the Loom by separating **Identity** (Flash-based metadata) from **Activity** (RTC RAM-based state). It uses a **Geometric TLB** to maintain $O(1)$ performance for routed transitions across a large coordinate space.

## Key Decisions
1.  **Compact Cell Structure**: Remove `name` from the live cell. Use a 19-byte `goose_cell_t` in the active Loom.
2.  **Ontological Catalog**: Maintain a Flash-based index (Spatial Map) that links `Coordinate -> Name + Metadata`.
3.  **Geometric TLB**: `goose_route_t` stores `cached_ptr` + `cached_version`. Lookups only occur when the Fabric Version increments (indicating a swap).
4.  **Autonomic Anchoring**: `Page 0` contains all hardware and autonomic cells; it is immutable and pinned in RTC RAM.

## Implementation Specification

### 1. Updated `goose_cell_t` (goose.h)
```c
typedef struct {
    reflex_tryte9_t coord;   ///< Geometric Primary Key
    int8_t state;            ///< Current ternary state
    int8_t type;             ///< Role (Pinned/Swappable)
    uint32_t hardware_addr;  
    uint32_t bit_mask;       
} goose_cell_t;
```

### 2. Updated `goose_route_t` (goose.h)
```c
typedef struct {
    reflex_tryte9_t source_coord;
    reflex_tryte9_t sink_coord;
    reflex_trit_t orientation;
    
    // Geometric TLB
    goose_cell_t *cached_source;
    goose_cell_t *cached_sink;
    uint32_t cached_version;
} goose_route_t;
```

### 3. The Spatial Catalog
A simple binary file in Flash:
- `Entry: [Coord: 9 trits][Name: 16 bytes][Metadata: 8 bytes]`

## Implementation Plan
1.  **Refactor `goose_cell_t` and `goose_route_t`** in `goose.h`.
2.  **Implement `goose_fabric_get_cell_by_coord` with TLB logic** in `goose_runtime.c`.
3.  **Implement `goose_spatial_swap_in/out`** using the ESP32 NVS or Flash API.
4.  **Update `goose_atlas`** to weave metadata into the Spatial Catalog instead of the live cell.

## Success Criteria
- [ ] Loom capacity increased from 128 to 512+ cells using the same 16KB RTC RAM.
- [ ] Route propagation performance remains consistent ($O(1)$ after cache fill).
- [ ] System survives deep sleep with Page 0 intact.
