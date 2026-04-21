# PRD: Service Watchdog (Auto-Restart with Backoff)

## Problem

Services registered via `reflex_service_register` can crash and remain dead until
the next reboot. There is no health monitoring and no restart logic in the current
`service_manager.c`.

## Goal

Crashed services auto-restart with exponential backoff: 1s, 2s, 4s, ... capped at 30s.

## Current Architecture

- `core/service_manager.c` stores up to 16 `reflex_service_desc_t*` pointers.
- Lifecycle: `init` -> `start` -> (runs forever) -> `stop` on shutdown.
- No liveness check, no crash detection, no restart counter.

## Proposed Architecture

1. Add per-service runtime state struct (not in the const descriptor):
   ```c
   typedef struct {
       bool alive;
       uint8_t restart_count;
       uint32_t next_restart_ms;  // tick at which restart is allowed
   } reflex_service_state_t;
   ```
2. Supervisor pulse (called from main loop or dedicated task, ~500ms period):
   - For each service, check `alive` flag (set false by task-death hook).
   - If dead and `now >= next_restart_ms`, call `svc->start(svc->context)`.
   - Compute next backoff: `min(1000 << restart_count, 30000)`.
   - Increment `restart_count`. Reset to 0 on successful 60s uptime.
3. Task-death hook: FreeRTOS `vTaskDelete` notification sets `alive = false`.

## Files to Modify

| File | Change |
|------|--------|
| `core/service_manager.c` | Add `s_service_state[]`, watchdog tick function |
| `core/include/reflex_service.h` | Add `reflex_service_state_t`, declare `reflex_service_watchdog_tick()` |

## Non-Goals

- Graceful degradation / dependency ordering (v2).
- Per-service health-check callbacks (v2).
- Notification to user shell on restart.

## Validation

1. Start system normally; all services report alive.
2. `kill` a service task (or inject fault). Verify restart within 2s.
3. Kill repeatedly: observe 1s, 2s, 4s backoff in logs, capped at 30s.
4. Let service stay alive 60s after restart: confirm counter resets.

## Effort

~60 LOC added, ~1 hour implementation.
