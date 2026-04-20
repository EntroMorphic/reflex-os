# Boot Sequence

Entry point: `app_main()` in `main/main.c`.

## Stage 1 -- Persistent State

- `reflex_boot_print_banner()` -- prints version/device banner.
- `reflex_storage_init()` -- opens NVS partitions for "reflex" and "goose" namespaces.
- If storage init fails, drops straight to `reflex_shell_run()`.

## Stage 2 -- Lifecycle / Safe Mode

- `reflex_config_get_safe_mode(&safe_mode)` -- reads persisted flag.
- `reflex_config_get_boot_count(&boot_count)` -- reads crash-loop counter.
- If `boot_count >= REFLEX_BOOT_LOOP_THRESHOLD` (10), forces `safe_mode = true`.
- Safe mode resets both counters and skips the remaining stages (falls through to shell).
- Otherwise, increments boot count: `reflex_config_set_boot_count(boot_count + 1)`.

## Stage 3 -- Substrate

- `reflex_event_bus_init()` + `reflex_event_bus_start()` -- event pub/sub.
- `reflex_fabric_init()` + `goose_fabric_init()` -- ternary coordinate fabric.

## Stage 4 -- Atlas (GOOSE Loom)

- `goose_supervisor_init()` -- regulation state machine.
- `goose_gateway_init()` -- external-facing API gate.
- `goose_atlas_manifest_weave()` -- projects 26 MMIO nodes x 4 registers = **104 pre-woven cells** into the Loom.
- `goose_lp_heartbeat_init()` -- LP RISC-V coherent heartbeat.
- `goose_supervisor_task` spawned at 10 Hz (`reflex_task_create`, priority 20).

## Stage 5 -- Services

- `reflex_service_manager_init()` -- service lifecycle manager.
- `reflex_led_service_register()` -- addressable LED driver.
- `reflex_button_service_register()` -- GPIO button with interrupt.
- `reflex_temp_service_register()` -- on-die temperature sensor.
- `reflex_wifi_service_register()` -- WiFi STA (skipped when 802.15.4 radio enabled).
- Ternary VM: `reflex_cache_init()`, `reflex_vm_task_runtime_init()`, registered as `"system-vm"`.
- `reflex_service_start_all()` -- starts all registered services.

## Stage 6 -- Mesh

- **802.15.4 build**: `goose_atmosphere_init()` -- blob-free mesh over Thread radio.
- **WiFi build**: `manifest_demo_arc()` -- self-arc loopback demo (ghost cell, atmosphere field, distributed rhythm).

## Stage 7 -- Stability Window

- `reflex_stability_task` created (priority 5).
- Waits `REFLEX_STABILITY_MS` (10 seconds), then calls `reflex_config_set_boot_count(0)`.
- Task self-deletes after reset.
- `REFLEX_EVENT_BOOT_COMPLETE` published on the event bus.

## Stage 8 -- Self-checks and Shell

- `reflex_ternary_self_check()`, `reflex_vm_self_check()`, `reflex_vm_loader_self_check()`, `reflex_vm_task_self_check()`.
- `reflex_shell_run()` -- blocking REPL. Does not return.

## Safe Mode

| Trigger | Behavior |
|---|---|
| `safe_mode` flag set in NVS | Resets counters, skips stages 3-7, enters shell |
| `boot_count >= 10` (crash loop) | Same as above |

The shell is always reachable -- every fatal error in stages 1-6 falls through to `reflex_shell_run()`.
