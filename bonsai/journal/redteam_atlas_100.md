# Red-Team: Full Peripheral Atlas (100/100)

## Objective
Falsify the "Total Hardware Inhabitation" implementation. Determine if mapping the entire MMIO space into the GOOSE Tapestry introduces catastrophic failure modes, resource depletion, or ontological corruption.

## Vulnerabilities Identified

### 1. The "Base Address" Illusion (Resolution Failure)
- **Problem:** `goose_atlas.c` projects only the *Base Address* of each peripheral.
- **Risk:** Most peripherals (like UART or GDMA) have dozens of registers (Control, Status, Data, Interrupt). Projecting just the base address gives the dev access to only the first register in the block.
- **Risk:** Newcomers will be confused when they can "see" `uart0` but cannot actually send data because the `TX_FIFO` is at an offset they haven't mapped.
- **Fix:** Implement "Region Expansion" where a single Atlas entry can manifest a cluster of related cells (e.g., `uart0.ctrl`, `uart0.data`).

### 2. Loom Saturation (Scaling Limit)
- **Problem:** We have ~20 peripherals in the Atlas and 64 max cells in the Loom.
- **Risk:** If we expand each peripheral to include its top 4 registers, we hit 80 cells. The Loom will overflow during the Genesis Weave, leaving the "Radio" and "Logic" fields unmapped.
- **Fix:** Increase `GOOSE_FABRIC_MAX_CELLS` to 128 or 256, or implement a "Page-on-Demand" system for the Atlas.

### 3. The "Power Gate" Paradox
- **Problem:** Many peripherals (WiFi, AES, ADC) are physically powered down or clock-gated by default on the C6.
- **Risk:** Writing to an MMIO address of a powered-down peripheral will trigger a **Hardware Fault** or silent hang.
- **Risk:** The Atlas projects the address but doesn't ensure the "System Rhythm" has enabled the clock for that region.
- **Fix:** The Genesis Weaver must call `system_enable_peripheral(base_addr)` before projecting.

### 4. Privilege Escalation (Shadow Memory)
- **Problem:** `gdma` (General DMA) is projected as `HARDWARE_OUT`.
- **Risk:** A user can route a `POS` signal to a DMA control cell to trigger a memory-to-memory copy that bypasses all software security checks.
- **Fix:** Mark "High-Risk" regions (DMA, PMU, AES) with a `GOOSE_CELL_SYSTEM_ONLY` type that the Gateway refuses to route to from `INTENT` sources.

### 5. Coordinate Sparsity
- **Problem:** `goose_make_coord` uses only trits 0, 3, and 6. 
- **Risk:** 6 trits are "dead space." If a dev accidentally sets trit 1 to `+1`, `goose_coord_equal` will fail to find the cell.
- **Fix:** Normalize coordinates during comparison or use a bitmask/trit-mask.

## Falsification Test (Phase 15.1)
**The "Dark Silicon" Test:**
1. Call `loom list`.
2. Try to `tapestry signal wifi_6 1`.
- **Expected Result:** Potential system hang or zero effect, because the WiFi peripheral clock isn't enabled. This proves the Atlas is currently a "Map of Ghost Towns."
