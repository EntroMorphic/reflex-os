# Reflex OS Backlog

## Purpose

This backlog translates `OS-PRD.md` into buildable engineering work. It is ordered for implementation, not just grouped by feature area.

## Status Legend

- `todo` not started
- `in_progress` currently active
- `blocked` cannot proceed yet
- `done` completed

## Current Phase

Phase 1: Ternary Architecture And Host Foundation

## Execution Order

1. Define ternary architecture and data model
2. Boot metadata and startup pipeline
3. Logging and diagnostics base
4. Persistent config store
5. Serial shell core
6. Service registry and lifecycle
7. Ternary ALU library
8. Soft opcode ISA
9. Ternary VM core
10. Ternary shell and inspection tools
11. Ternary syscall bridge
12. Event bus
13. LED and button services
14. Timer service
15. Wi-Fi config and status
16. Safe mode and boot failure tracking
17. OTA-ready partition work

## Milestone Backlog

## Milestone 1: Host Foundation

### B001 Boot Banner And Version Metadata
- Status: `done`
- Goal: Replace the current single printf with a structured boot banner.
- Deliverables:
- print project name
- print build version
- print chip revision
- print flash size
- print reset reason
- print boot mode markers
- Dependencies: none
- Notes: this should become the first visible Reflex OS identity on boot.

### B002 System Time And Uptime Base
- Status: `todo`
- Goal: Establish a reusable uptime source for shell diagnostics.
- Deliverables:
- uptime helper API
- millisecond uptime reporting
- boot timestamp capture
- Dependencies: B001

### B003 Logging Facade
- Status: `done`
- Goal: Define a Reflex OS logging wrapper over ESP-IDF logging.
- Deliverables:
- log macros or wrapper API
- runtime log level control hook
- component tags for core modules
- Dependencies: B001
- Notes: this should be in place before shell and service work spreads logging everywhere.

### B004 NVS Storage Initialization
- Status: `done`
- Goal: Bring up persistent storage cleanly at boot.
- Deliverables:
- NVS init on startup
- erase-and-recover path for NVS init failures
- storage module boundary
- Dependencies: B001

### B005 Config Store Schema
- Status: `done`
- Goal: Create a typed config layer over NVS.
- Deliverables:
- config namespace design
- defaults initialization
- getters and setters for:
- device name
- log level
- Wi-Fi SSID
- Wi-Fi password
- safe mode flag
- Dependencies: B004

### B006 Serial Shell Core
- Status: `done`
- Goal: Implement a line-oriented serial shell over the USB serial console.
- Deliverables:
- input loop
- command parsing
- command registration table
- help output
- unknown command handling
- Dependencies: B003
- Notes: keep command dispatch simple and static for MVP.

### B007 Base Shell Commands
- Status: `done`
- Goal: Implement the first built-in commands.
- Deliverables:
- `help`
- `reboot`
- `version`
- `uptime`
- `heap`
- Dependencies: B002, B006

### B008 Config Shell Commands
- Status: `done`
- Goal: Make persisted config manageable from the shell.
- Deliverables:
- `config get <key>`
- `config set <key> <value>`
- value validation
- persistence confirmation
- Dependencies: B005, B006

### B009 Service Registry
- Status: `done`
- Goal: Define the runtime model for named internal services.
- Deliverables:
- service descriptor type
- init/start/stop/status hooks
- registration API
- boot-time service startup order
- Dependencies: B003

### B010 Core Service Manager
- Status: `done`
- Goal: Implement service lifecycle orchestration.
- Deliverables:
- service manager module
- service startup sequencing
- service status reporting
- failure propagation rules
- Dependencies: B009

### B011 Task And Service Diagnostics Command
- Status: `todo`
- Goal: Surface system runtime visibility in the shell.
- Deliverables:
- `tasks`
- `services`
- service health/status output
- Dependencies: B006, B010

## Milestone 2: Ternary Core

### T001 Ternary Architecture Note
- Status: `done`
- Goal: Define what Reflex OS means by ternary computation before implementation begins.
- Deliverables:
- balanced ternary decision
- VM model decision summary
- system boundary between binary host and ternary runtime
- Dependencies: none

### T002 Ternary Data Representation
- Status: `done`
- Goal: Define how trits and ternary words are stored in memory.
- Deliverables:
- trit type definition
- packed storage design
- ternary word size definition
- encode and decode rules
- Dependencies: T001

### T003 Ternary Arithmetic Library
- Status: `done`
- Goal: Implement the first reusable ternary math primitives.
- Deliverables:
- normalize helpers
- ternary compare
- ternary add
- ternary subtract
- ternary select semantics
- Dependencies: T002

### T004 Soft Opcode ISA Specification
- Status: `done`
- Goal: Define the MVP ternary instruction set.
- Deliverables:
- opcode enum
- instruction encoding format
- opcode semantics for load, store, arithmetic, compare, branch, syscall
- Dependencies: T001, T002

### T005 Ternary VM State Model
- Status: `done`
- Goal: Define the runtime machine state for executing ternary programs.
- Deliverables:
- instruction pointer model
- register or stack machine decision
- memory layout for VM instance
- fault state model
- Dependencies: T002, T004

### T006 Ternary Interpreter Core
- Status: `done`
- Goal: Execute the soft opcode instruction stream in software.
- Deliverables:
- fetch/decode/execute loop
- bounded execution step count
- error handling for invalid instructions
- Dependencies: T003, T004, T005

### T007 Ternary Program Loader Format
- Status: `done`
- Goal: Define the MVP format for loading ternary programs into the VM.
- Deliverables:
- in-memory bytecode or wordcode format
- validation checks
- loader API
- Dependencies: T004, T005

### T008 Ternary VM Shell Commands
- Status: `done`
- Goal: Make the VM observable and operable from the shell.
- Deliverables:
- `vm info`
- `vm load`
- `vm run`
- `vm step`
- `vm regs` or equivalent state dump
- Dependencies: B006, T006, T007

### T009 Ternary Syscall Bridge
- Status: `done`
- Goal: Allow ternary programs to request controlled OS services.
- Deliverables:
- syscall dispatch table
- logging syscall
- time or uptime syscall
- config read syscall
- Dependencies: B005, T004, T006

### T010 Ternary Task Runtime
- Status: `done`
- Goal: Run ternary VM instances as scheduled Reflex OS workloads.
- Deliverables:
- VM instance lifecycle
- task scheduling integration
- task state reporting
- Dependencies: B010, T006, T009

## Milestone 3: Hardware And Events

### H001 Event Bus Core
- Status: `done`
- Goal: Create an internal publish-subscribe event mechanism.
- Deliverables:
- event type definitions
- subscriber registration
- event publish API
- synchronous or queued dispatch design
- Dependencies: B010

### H002 Boot And Service Events
- Status: `done`
- Goal: Publish system lifecycle events on boot and service transitions.
- Deliverables:
- boot-complete event
- service-started event
- service-failed event
- Dependencies: H001, B010

### H003 LED HAL
- Status: `done`
- Goal: Create the first hardware abstraction module around the onboard LED.
- Deliverables:
- LED pin mapping for XIAO ESP32C6
- LED on/off/toggle API
- Dependencies: B003

### H004 LED Service
- Status: `done`
- Goal: Expose LED control as a managed service.
- Deliverables:
- LED service registration
- shell command or event-driven LED control
- Dependencies: H003, B010

### H005 Button HAL
- Status: `todo`
- Goal: Add button input support for reset-safe user interaction.
- Deliverables:
- button pin mapping
- debounced read path
- GPIO interrupt or polling strategy
- Dependencies: B003

### H006 Button Event Service
- Status: `todo`
- Goal: Convert button activity into Reflex OS events.
- Deliverables:
- button service
- button pressed and released events
- Dependencies: H001, H005, B010

### H007 Timer Service
- Status: `todo`
- Goal: Provide reusable one-shot and periodic timers.
- Deliverables:
- timer wrapper API
- timer-backed service work scheduling
- Dependencies: H001, B010

## Milestone 4: Connectivity

### N001 Wi-Fi Config Integration
- Status: `done`
- Goal: Connect Wi-Fi config entries to the networking stack.
- Deliverables:
- config-to-runtime Wi-Fi credential loading
- validation rules for missing credentials
- Dependencies: B005

### N002 Wi-Fi Manager Service
- Status: `done`
- Goal: Add managed station-mode Wi-Fi support.
- Deliverables:
- init and teardown path
- station connect flow
- reconnect behavior
- Dependencies: B010, N001

### N003 Network Event Mapping
- Status: `done`
- Goal: Publish Wi-Fi and IP events into the Reflex OS event bus.
- Deliverables:
- connected event
- disconnected event
- IP acquired event
- Dependencies: H001, N002

### N004 Network Shell Commands
- Status: `done`
- Goal: Make network setup visible and controllable from the shell.
- Deliverables:
- `wifi status`
- `wifi connect`
- `wifi disconnect`
- Dependencies: B006, N002

## Milestone 5: Reliability

### R001 Boot Counter And Failure Marker
- Status: `done`
- Goal: Persist enough boot-state information to detect unstable startup.
- Deliverables:
- boot count
- last boot success flag
- last panic or failure marker
- Dependencies: B005

### R002 Safe Mode Entry Logic
- Status: `done`
- Goal: Support minimal boot mode for recovery.
- Deliverables:
- intentional safe mode trigger
- automatic safe mode after repeated failures
- minimal service boot profile
- Dependencies: R001, B010

### R003 Factory Reset Command
- Status: `todo`
- Goal: Clear persisted state and recover to defaults.
- Deliverables:
- shell command to reset config
- reboot-after-reset flow
- Dependencies: B008, B005

### R004 Watchdog Integration
- Status: `todo`
- Goal: Detect and report hung services.
- Deliverables:
- watchdog strategy
- service heartbeat rules
- timeout reporting
- Dependencies: H007, B010

## Milestone 6: OTA Readiness

### O001 OTA Partition Layout
- Status: `todo`
- Goal: Move from the default single factory app layout to an OTA-ready partition table.
- Deliverables:
- custom partition table
- ota_0 and ota_1 partitions
- otadata partition
- Dependencies: Milestone 1 stability

### O002 OTA Service Scaffold
- Status: `todo`
- Goal: Create the module boundary for future update support.
- Deliverables:
- OTA service placeholder
- update state model
- validation hook points
- Dependencies: O001, B010

## Cross-Cutting Design Tasks

### D001 Define Reflex OS Module Layout
- Status: `todo`
- Goal: Establish source tree boundaries before the codebase spreads.
- Deliverables:
- decide directories for `core`, `vm`, `services`, `drivers`, `shell`, `storage`, and `net`
- Dependencies: none

### D002 Define "Ternary" Architecture Meaning
- Status: `todo`
- Goal: Turn the ternary product idea into implementation constraints for the VM and host runtime.
- Deliverables:
- short architecture note
- explicit mapping to runtime concepts
- Dependencies: none

### D003 Command Style Guide
- Status: `todo`
- Goal: Keep shell UX consistent as command count grows.
- Deliverables:
- naming conventions
- output formatting rules
- error formatting rules
- Dependencies: B006

## First Active Queue

These are the next recommended tasks to implement in order:

1. B006 Serial shell core
2. B007 Base shell commands
3. B008 Config shell commands
4. B009 Service registry
5. B010 Core service manager
6. H001 Event bus core
7. H003 LED HAL
8. H004 LED service

## Deferred Until Needed

- BLE feature work
- Thread and Zigbee runtime work
- AP mode
- remote management UI
- dynamic application loading
- rich storage model beyond config and state
- higher-level ternary compiler beyond the MVP VM toolchain
