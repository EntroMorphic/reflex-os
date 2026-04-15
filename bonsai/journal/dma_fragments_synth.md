# Synthesis: Geometric DMA Fragments (The Flow)

## Architecture
Geometric DMA moves the Tapestry from a **Control Plane** to a **Data Plane**. It introduces the concept of **Authority-led Projection**, where a Route governs the high-speed flow of binary data blocks without CPU intervention.

## Key Decisions
1. **Flow Authority:** The `Orientation` of a `GOOSE_COUPLING_DMA` route acts as a gate for binary flow (`+1` = Flow, `0` = Inhibit).
2. **GDMA Substrate:** Uses the ESP32-C6 AHB DMA channels.
3. **SRAM Manifestation:** Descriptors are woven in DMA-accessible SRAM, while the intent (Route) remains in the sleep-safe Loom (RTC RAM).

## Implementation Spec
- `goose_dma_init()`: Allocates GDMA TX/RX channels.
- `goose_dma_apply_route()`: Translates route orientation into `gdma_start()` or `gdma_stop()`.
- `GOOSE_COUPLING_DMA`: New coupling mode for authority-led transfers.

## Success Criteria
- [x] Zero-CPU data transfer between SRAM regions after weaving.
- [x] Persistent state tracking of flow status in the Loom.
