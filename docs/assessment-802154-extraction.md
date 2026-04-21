# Assessment: 802.15.4 Driver Extraction from ESP-IDF

## Current State

A shim header exists at `radio/ieee802154/reflex_ieee802154_shim.h` that maps
ESP-IDF primitives to Reflex OS equivalents:

- `portMUX_TYPE` / `portENTER_CRITICAL` -- direct RISC-V CSR interrupt
  disable/enable
- `esp_timer_get_time()` -- `reflex_hal_time_us()`
- `esp_intr_alloc/free` -- direct interrupt setup
- Logging, attributes, PM, coex -- stubbed out

The shim satisfies the driver's compile-time dependencies without modifying
ESP-IDF source. The 802.15.4 driver compiles against this shim and the radio
functions correctly on hardware.

## Blocking Issue

ESP-IDF's `ieee802154` component transitively pulls in:

- `esp_phy` -- PHY calibration and RF initialization
- `esp_coex` -- WiFi/BT/802.15.4 coexistence arbitration
- `hal` -- register-level hardware abstraction (SoC-specific structs)
- `soc` -- chip-specific register definitions and field macros

The HAL header chain is deeply nested. The driver references HAL structs
(`ieee802154_ll_*` inline functions) that directly include SoC register
definitions. Extracting these means duplicating or reimplementing hundreds of
register-level definitions that change per chip revision.

## Options

### A. Fork the ieee802154 component and strip dependencies

Copy ESP-IDF's ieee802154 source into the Reflex tree and progressively replace
HAL/SOC includes with direct register access.

- Pros: Full ownership, no ESP-IDF build dependency for radio.
- Cons: Large surface area (~15 source files, ~30 HAL headers). Fragile across
  ESP-IDF updates and chip revisions. Ongoing maintenance burden.
- Effort: Weeks of focused work. Ongoing per chip revision.

### B. Write a minimal 802.15.4 MAC from register documentation

Implement TX/RX/ACK from the ESP32-C6 TRM register descriptions directly,
bypassing ESP-IDF's driver entirely.

- Pros: Zero ESP-IDF dependency. Complete control.
- Cons: Massive scope. The 802.15.4 MAC state machine (CCA, backoff, ACK
  timing, frame filtering) is complex. Debugging requires RF test equipment.
- Effort: Months. High risk of subtle timing bugs.

### C. Accept the shim boundary as the isolation point

Keep the current architecture: the shim header isolates ESP-IDF types at
compile time, the driver works, and the Reflex radio API (`reflex_radio.h`)
abstracts everything above.

- Pros: Zero additional work. Driver is tested and functional. The shim
  boundary is clean and documented. Application code never touches ESP-IDF
  types.
- Cons: The build still links against ESP-IDF's ieee802154 static library.
  True bare-metal independence for radio requires Option A or B.
- Effort: Zero (already done).

## Recommendation

**Option C.** The shim already provides the isolation that matters for
application code. The 802.15.4 driver works correctly on hardware. Full
extraction (A or B) carries high cost and high risk for low user-facing value.

If chip independence becomes a requirement (e.g., porting to a non-Espressif
802.15.4 radio), Option A becomes necessary, but scoped to the new target
rather than as a speculative extraction.

## Decision

Accept the shim boundary. Document it as a known ESP-IDF coupling point in
`implementation-status.md`. Revisit only if a concrete porting target demands
it.
