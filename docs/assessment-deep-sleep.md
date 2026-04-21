# Assessment: True PMU Deep Sleep

## Current State

`reflex_hal_sleep_enter()` uses a "timed reboot" approach: writes the
requested duration to an LP AON register, triggers a software reset, and
on wake the boot path reads the register to detect a sleep-wake cycle.

This works but:
- Not true deep sleep (PMU is not placed in low-power state)
- Power consumption during "sleep" is still ~10mA (full HP core idle)
- True deep sleep on ESP32-C6 should draw ~7uA (HP core off, LP timer wake)

## Blocking Issue

The ESP32-C6 PMU has chip-revision-specific register layouts. The power
domain control registers at 0x600B0000 differ between rev 0.0, 0.1, and
0.2. ESP-IDF abstracts this via `esp_sleep_enable_timer_wakeup()` +
`esp_deep_sleep_start()`, which we've been avoiding to maintain
independence.

## Options

### A. Direct PMU Register Writes (high risk)
Read PMU registers from the TRM, implement power domain sequencing.
- Risk: chip revision mismatches cause bricking
- Effort: 2-3 days + extensive testing per revision
- Benefit: true independence from ESP-IDF sleep path

### B. Use ESP-IDF Sleep Functions (pragmatic)
Call `esp_sleep_enable_timer_wakeup()` + `esp_deep_sleep_start()` from
`reflex_hal_sleep_enter()`. Two function calls, guarded by platform ifdef.
- Risk: none (well-tested path)
- Effort: 15 minutes
- Tradeoff: adds two ESP-IDF calls to the otherwise independent HAL

### C. Keep Current Timed Reboot (status quo)
The current approach works. Power savings are limited but the feature
is functional.
- Risk: none
- Effort: 0
- Tradeoff: 10mA vs 7uA during "sleep"

## Recommendation

Option B. The two ESP-IDF sleep calls are well-tested across all chip
revisions. The power savings (10mA → 7uA) justify the minimal dependency.
Wrap in `#ifdef CONFIG_IDF_TARGET_ESP32C6` to keep the HAL portable.

## Effort

15 minutes for Option B. Platform test: `sleep 10` → measure current draw.
