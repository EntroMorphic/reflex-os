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

## Implementation: True PMU Deep Sleep

The ESP32-C6 implementation uses `esp_sleep_enable_timer_wakeup()` +
`esp_deep_sleep_start()` for true PMU deep sleep:

1. LP timer configured for the requested duration.
2. HP core fully powered down (~7uA sleep current).
3. On wakeup, the full boot sequence re-runs (NVS + RTC RAM survive).

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

## Power

Sleep current: ~7uA (HP core off, LP timer running). This is true PMU deep
sleep, not a timed reboot.
