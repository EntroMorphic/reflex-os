# Red-Team: Phase 11 & 12 (Trans-Core & LP Pulse)

## Objective
Falsify the "Trans-Core Manifold" and "Autonomous Heartbeat" implementation. Determine if the shared state between HP and LP cores is stable, semantically consistent, and physically safe.

## Vulnerabilities Identified

### 1. Structural Mismatch (Struct Alignment)
- **Problem:** `goose_cell_t` in `goose.h` uses `reflex_trit_t` (int8_t), while `lp_goose_cell_t` in `lp_pulse.c` uses `int32_t` for state and coordinates.
- **Risk:** The LP core will read the wrong offsets in RTC RAM. It will be looking for a 4-byte `state` where the HP core wrote a 1-byte `state`.
- **Fix:** Use explicit fixed-width types (e.g., `int8_t`) and `#pragma pack(push, 1)` to ensure identical binary layout across both cores.

### 2. The "Cold Heart" (Lifecycle Failure)
- **Problem:** The LP core binary is embedded via CMake, but `main.c` contains no code to `ulp_riscv_load_binary` or `ulp_riscv_run`.
- **Risk:** The HP core enters sleep expecting the LP core to take over, but the LP core is never started. The system effectively dies.
- **Fix:** Implement the ULP loader and startup sequence in `goose_runtime.c`.

### 3. GPIO Ownership Contention
- **Problem:** `lp_pulse.c` calls `ulp_riscv_gpio_output_level` directly.
- **Risk:** If the HP core has previously configured that pin via the GPIO Matrix (Phase 4 Shadow Agency), the LP core's simple bit-bang may be ignored or cause an electrical conflict.
- **Fix:** The HP core must explicitly release "Agency Pins" to the LP core before entering sleep.

### 4. Coordinate "Trash" (Geometric Precision)
- **Problem:** `goose_fabric_get_cell_by_coord` compares all 9 trits of a `reflex_tryte9_t`.
- **Risk:** If a VM register contains non-zero trits in positions 1, 2, 4, 5, 7, 8 (which are unused by the current `goose_make_coord` logic), the comparison will fail.
- **Fix:** Implement a `coord_mask` or ensure `goose_make_coord` explicitly clears the unused manifold dimensions.

### 5. Memory Exhaustion (RTC RAM Limits)
- **Problem:** `fabric_cells` are in `RTC_DATA_ATTR`.
- **Risk:** The ESP32-C6 has very limited RTC RAM (16KB total, shared with LP code). 16 cells * ~32 bytes each is ~512 bytes. This is safe now, but without a linker-check, we could silently overwrite the LP stack.
- **Fix:** Add a compile-time check for the Loom's size relative to RTC memory.

## Falsification Test (Phase 12.5)
**The "Ghost Heart" Test:**
1. Start the system.
2. Set `led_intent` to POS.
3. Call `bonsai deep-sleep`.
- **Result:** Currently, the LED will likely freeze in its last state or turn off, because the LP core is either not running or reading the wrong memory offsets. This falsifies the "Autonomous Heartbeat."
