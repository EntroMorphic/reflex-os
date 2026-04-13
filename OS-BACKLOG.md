# Reflex OS Backlog

## Purpose

This backlog translates `OS-PRD.md` into buildable engineering work. It is ordered for implementation, not just grouped by feature area.

## Status Legend

- `todo` not started
- `in_progress` currently active
- `blocked` cannot proceed yet
- `done` completed

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

### B003 Logging Facade
- Status: `done`
- Goal: Define a Reflex OS logging wrapper over ESP-IDF logging.
- Deliverables:
- log macros or wrapper API
- runtime log level control hook
- component tags for core modules
- Dependencies: B001

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
- getters and setters
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

### B007 Base Shell Commands
- Status: `done`
- Goal: Implement the first built-in commands (reboot, version, uptime, heap).
- Deliverables:
- `help`
- `reboot`
- `version`
- `uptime`
- `heap`
- Dependencies: B006

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

## Milestone 2: Hardware And Events

### H001 Event Bus Core
- Status: `done`
- Goal: Create an internal publish-subscribe event mechanism.
- Deliverables:
- event type definitions
- subscriber registration
- event publish API
- async queue-based dispatch
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
- event-driven LED control
- Dependencies: H003, B010

### H005 Button HAL
- Status: `done`
- Goal: Add button input support for reset-safe user interaction (GPIO 9).
- Deliverables:
- button pin mapping
- debounced read path
- polling task
- Dependencies: B003

### H006 Button Event Service
- Status: `done`
- Goal: Convert button activity into Reflex OS events.
- Deliverables:
- button service
- button pressed and released events
- Dependencies: H001, H005, B010

## Milestone 3: Connectivity

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

## Milestone 4: Reliability

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

### R005 Stability Signal & Pointer Hardening (Red-Team Remediations)
- Status: `done`
- Goal: Fix critical architectural risks identified during redteaming.
- Deliverables:
- async event bus with dedicated task
- 10s stability window for boot count reset
- VM state and memory access boundary checks
- TLD/TST realized with checked memory helpers (VMM)
- Dependencies: H001, T006, B005

## Milestone 5: OTA Readiness

### O001 OTA Partition Layout
- Status: `done`
- Goal: Move from the default single factory app layout to an OTA-ready partition table.
- Deliverables:
- custom partition table (ota_0, ota_1, otadata)
- flash re-init confirmed
- Dependencies: Milestone 1 stability

### O002 OTA Service Scaffold
- Status: `done`
- Goal: Create the module boundary for future update support.
- Deliverables:
- OTA service placeholder
- update state model
- validation hook points
- Dependencies: O001, B010

## Milestone 6: Ternary Core

### T001-T010 Ternary VM Foundation
- Status: `done`
- Goal: Establish the software-defined ternary machine.
- Deliverables:
- balanced ternary semantics (-1, 0, +1)
- 32-bit packed bytecode (v2)
- interpreter with ALU, Branching, and Syscalls
- VM task runner on FreeRTOS
- MMU for shared/private memory mapping

## Milestone 7: Advanced Soft-Silicon Services

### S001 Ternary Message Fabric
- Status: `done`
- Goal: Implement high-speed message dispatch between tasks.
- Deliverables:
- lock-free trit-indexed message queues
- TSEND and TRECV soft opcodes
- QoS channel model
- Dependencies: R005, B010

### S002 Ternary Shared Memory Regions
- Status: `done`
- Goal: Support shared data structures between multiple VM instances.
- Deliverables:
- MMU translation layer
- shared memory segment mapping
- boundary-checked shared addressing
- Dependencies: R005

### S003 Soft-Cache & Coherency Manager
- Status: `done`
- Goal: Accelerate VM execution with a software-defined cache and coherency protocol.
- Deliverables:
- software-defined L1 cache for ternary words
- MESI-lite coherency protocol (I, E, M)
- Coherency Proxy for host-side writes
- cache-flush and invalidate opcodes
- Dependencies: S001, S002
