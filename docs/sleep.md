# Deep Sleep

Declared in `include/reflex_hal.h`, implemented in `platform/esp32c6/reflex_hal_esp32c6.c`.

## What Survives

- **NVS** -- all key-value data in "reflex" and "goose" namespaces.
- **RTC RAM** -- variables marked `REFLEX_RTC_DATA_ATTR` survive on ESP32 targets.
- **LP AON registers** -- used to pass sleep magic/duration across reboot.

## What Is Lost

- Heap, stack, all SRAM contents.
- Radio state (WiFi, 802.15.4, BLE).
- GOOSE fabric, event bus, service state, caches -- all re-initialized on wake.

## Shell Command

```
sleep [seconds]     # default 3, minimum 1, max 65535
```

Calls `reflex_hal_sleep_enter(seconds * 1_000_000)`.

## Implementation: Timed Reboot

The current ESP32-C6 implementation uses a **timed reboot** approach, not true PMU deep sleep:

1. Encodes duration into `LP_AON_STORE1_REG` with magic `0x534C5000` ("SLP\0").
2. Calls `reflex_hal_reboot()` (software reset via ROM `software_reset()`).
3. On next boot, bootloader detects the magic and resumes.

True PMU deep sleep requires ~400 lines of chip-revision-specific register writes. The timed-reboot approach is functionally equivalent for state persistence but draws more power during sleep.

## Wakeup Cause API

```c
int reflex_hal_sleep_wakeup_cause(void);
```

| Return | Meaning |
|--------|---------|
| 0 | Power-on (not a sleep wakeup) |
| 1 | Unknown source |
| 2 | GPIO wakeup |
| 4 | LP timer wakeup |

Reads `PMU_WAKEUP_STATUS0_REG` (`0x600B0140`).

## Known Limitation

Sleep current is higher than true deep sleep (~7 uA target vs actual reboot-idle current). Full PMU sleep is a planned optimization.
