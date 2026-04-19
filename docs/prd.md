# Reflex OS PRD

## Goal

Build a usable MVP operating system for the Seeed Studio XIAO ESP32C6 that hosts a ternary compute environment on top of ESP-IDF.

The first version should feel like a real embedded OS while establishing a software-defined ternary machine through soft opcodes, ternary memory structures, and a ternary task runtime.

## Product Definition

Reflex OS is a binary-hosted ternary operating environment built on top of ESP-IDF. It is not a desktop-style operating system, and it is not a custom silicon implementation. Instead, it extends the ESP32-C6 with a software-defined ternary instruction layer.

For MVP, Reflex OS should provide two tightly integrated layers:

### Binary Host Layer

- deterministic boot
- a service-oriented runtime
- a serial shell
- persistent configuration
- hardware abstraction for core peripherals
- basic networking
- OTA-safe upgrade support
- logging and recovery paths

### Ternary Runtime Layer

- a ternary data model
- packed ternary words in binary memory
- a ternary ALU library
- a soft-opcode interpreter
- a ternary task model
- a system call boundary from ternary code into Reflex OS services

## Architectural Principle

The ESP32-C6 remains a binary machine at the silicon level. Reflex OS introduces ternary computation by defining a virtual ternary machine in software.

This means:

- hardware drivers stay binary and use ESP-IDF directly
- system services stay binary and host system capabilities
- ternary programs execute inside a Reflex VM
- ternary tasks call into OS services through soft-opcode syscalls

## Ternary Model

For MVP, Reflex OS should use balanced ternary.

- Trit values: `-1`, `0`, `+1`
- Ternary control states map naturally to negative, neutral, and positive conditions
- Arithmetic and branching semantics should be defined around balanced ternary first

Current implementation decisions:

- balanced ternary is the active semantic model
- `tryte9` is the human-scale unit
- `word18` is the primary execution word
- packed storage uses 2 bits per trit with one reserved invalid code

## Target Hardware

- Board: Seeed Studio XIAO ESP32C6
- MCU: ESP32-C6
- Flash: 4MB
- Primary console: USB Serial/JTAG
- Base SDK: ESP-IDF

## Non-Goals For MVP

- true process isolation
- users and permissions model
- package manager
- dynamic module loading
- graphical interface
- desktop-style filesystem UX
- complete peripheral coverage
- custom silicon changes
- native CPU opcode changes
- fully removing FreeRTOS (ESP-IDF components depend on it internally)

## MVP Requirements

### 1. Boot And Recovery

The system must:

- boot reliably into Reflex OS on power-up and reset
- report boot reason and firmware version
- detect repeated boot failures
- support a safe mode with minimal services enabled
- support factory reset of persisted configuration

Acceptance criteria:

- boot information is visible over the serial console
- a failed boot can be detected and counted across resets
- safe mode can be entered intentionally and after repeated failures

### 2. Console Shell

The system must provide a serial command shell for bring-up and maintenance.

Initial commands should include:

- `help`
- `reboot`
- `version`
- `uptime`
- `heap`
- `tasks`
- `config get`
- `config set`
- `log level`

Acceptance criteria:

- shell is accessible over the USB serial interface
- unknown commands fail cleanly
- command handlers can be extended without rewriting the shell core

### 3. Task And Service Runtime

The system must provide a runtime model for internal services behind a portable task abstraction (`reflex_task.h`). FreeRTOS is the production scheduling backend; a standalone Reflex kernel backend is available via `CONFIG_REFLEX_KERNEL_SCHEDULER`.

Requirements:

- named services
- lifecycle hooks for init, start, stop, and status
- service registration at boot
- task visibility for diagnostics

Acceptance criteria:

- at least two built-in services can be registered and started
- service status can be queried from the shell

### 4. Event And Message Bus

The system must provide a lightweight publish-subscribe event bus for internal communication.

Initial event types should support:

- boot events
- timer events
- button and GPIO events
- network state events
- service lifecycle events

Acceptance criteria:

- one service can publish an event and another can consume it
- event flow is observable in logs during development

### 5. Persistent Config And State

The system must persist configuration and small amounts of state across reboots.

Initial stored data should include:

- device name
- log level
- Wi-Fi credentials
- boot counters
- crash or recovery markers

Implementation target:

- ESP-IDF NVS

Acceptance criteria:

- configuration survives reboot
- config can be changed through the shell
- defaults are created automatically on first boot

### 6. Hardware Abstraction Layer

The system must expose stable internal interfaces for common hardware operations.

Initial support should include:

- GPIO input and output
- onboard LED control
- button input
- timers
- I2C scaffold
- SPI scaffold
- UART scaffold

Acceptance criteria:

- onboard LED can be controlled from a service or shell command
- button input can generate an event
- HAL interfaces hide direct ESP-IDF details from higher layers where practical

### 7. Timers And Watchdogs

The system must provide:

- one-shot timers
- periodic timers
- service health timeouts
- watchdog integration for system recovery

Acceptance criteria:

- a service can schedule periodic work
- a hung service can be detected and reported

### 8. Networking Baseline

The system must support first-use network functionality.

Initial scope:

- Wi-Fi station mode
- connect and disconnect lifecycle
- IP status reporting
- shell commands for network setup and status

Deferred:

- AP mode
- BLE application layer features
- Thread and Zigbee features

Acceptance criteria:

- user can store Wi-Fi credentials
- system can connect to a Wi-Fi network
- IP and connection state are visible from the shell

### 9. Logging And Diagnostics

The system must provide structured operational visibility.

Requirements:

- serial logs
- log levels
- service and task inspection
- reset reason reporting
- heap and uptime reporting

Acceptance criteria:

- logs are readable during boot and runtime
- diagnostics are accessible from the shell

### 10. Ternary Data Model

The system must define a concrete software representation of ternary data.

Requirements:

- balanced ternary semantics
- trit representation in C
- packed ternary storage format
- ternary word size definition for the VM

Acceptance criteria:

- ternary values can be encoded and decoded deterministically
- packed storage format is documented and testable
- arithmetic helpers operate on the chosen word type

### 11. Ternary ALU And Soft Opcodes

The system must implement a core ternary instruction set in software.

Initial opcode groups should include:

- load and store
- arithmetic
- compare
- branch
- system call bridge

Example opcode families:

- `TLD`
- `TST`
- `TADD`
- `TSUB`
- `TCMP`
- `TJMP`
- `TBRNEG`
- `TBRZERO`
- `TBRPOS`
- `TSYS`

Current implementation note:

- `TSEL` is deferred from the first encoding revision

Acceptance criteria:

- opcode semantics are documented
- the interpreter can execute a short ternary program correctly
- arithmetic and control flow produce repeatable results

### 12. Ternary VM Runtime

The system must provide a software-defined ternary execution engine.

Requirements:

- instruction pointer
- ternary registers or stack model
- execution loop
- bounded execution slice support
- program loading format for MVP

Acceptance criteria:

- a small program can be loaded and executed
- VM state can be inspected from the shell
- execution can be stepped or bounded for diagnostics

### 13. Ternary Task Model

The system must support running ternary programs as managed runtime tasks.

Requirements:

- VM instance lifecycle
- scheduling model via reflex_task.h abstraction
- task naming and status
- failure handling for VM faults

Acceptance criteria:

- at least one ternary task can run under Reflex OS control
- ternary task state is visible from the shell

### 14. Ternary System Call Boundary

The system must expose controlled OS capabilities to ternary programs.

Initial syscall targets should include:

- logging
- timers
- message send and receive
- GPIO control
- config read

Acceptance criteria:

- ternary code can invoke a system call through a soft opcode
- syscall failures are reported safely
- syscall interface remains separate from internal binary driver details

### 15. OTA Update Path

The system must support safe firmware update workflows.

Initial scope:

- OTA partition strategy
- update trigger mechanism
- rollback-safe validation path

Acceptance criteria:

- partition layout supports future OTA
- update flow can be added without restructuring the whole system

### 16. Safe Failure Handling

The system must degrade safely when something goes wrong.

Requirements:

- boot loop detection
- last failure marker
- shell availability in reduced mode when possible
- ability to clear persisted bad state

Acceptance criteria:

- repeated startup failure can trigger safe mode
- failure state is visible after reboot

## MVP Milestones

### Milestone 1: Host Foundation

- boot banner
- logging facade
- shell framework
- version and diagnostics commands
- persistent config store
- service registry

### Milestone 2: Ternary Core

- ternary data model
- packed ternary word representation
- ternary ALU helpers
- soft opcode specification
- minimal interpreter

### Milestone 3: Ternary Runtime Integration

- ternary VM state model
- ternary task runtime
- shell commands for VM inspection and execution
- syscall bridge for logging and diagnostics

### Milestone 4: Hardware And Events

- event bus
- LED service
- button input service
- timer service

### Milestone 5: Connectivity

- Wi-Fi config commands
- Wi-Fi connect and status
- network event integration

### Milestone 6: Reliability

- safe mode
- boot failure tracking
- OTA-ready partition and update hooks

## Recommended First Implementation Order

1. define ternary number system and packed word representation
2. system boot banner and version metadata
3. logging facade and shell framework
4. config store backed by NVS
5. service registry and lifecycle model
6. ternary ALU library
7. soft opcode specification
8. minimal ternary interpreter
9. shell commands to inspect and run ternary programs
10. ternary syscall bridge
11. event bus and demo hardware services
12. Wi-Fi connect and status commands
13. safe mode and boot-failure tracking

## Open Questions

- What ternary word size should MVP use?
- Should the first VM be register-based or stack-based?
- What is the initial packed representation for trits in RAM and flash?
- Should ternary programs be hand-authored assembly first, or should we define a small assembler immediately?
- Should the first network control surface be shell-only, HTTP-based, or both?
- Do we want OTA in the first flashing milestone or only after the shell and config system are stable?
- Should services be statically linked only for MVP, or do we want an abstraction that anticipates loadable modules later?

## Definition Of Done For MVP

Reflex OS MVP is done when:

- it boots reliably on the XIAO ESP32C6
- it exposes a usable serial shell
- it can persist config across reboot
- it can run multiple named services
- it defines a balanced ternary data model
- it can execute ternary soft opcodes in a working VM
- ternary tasks can call selected OS services through a syscall boundary
- services can communicate over an event bus
- it can control at least one onboard hardware feature
- it can connect to Wi-Fi and report status
- it has a recovery path for boot failure
- it is structured for future OTA updates

## Milestone 7: Advanced Soft-Silicon Services

### Ternary Message Fabric
The system should implement a message-native substrate for high-speed coordination between the host and ternary runtime, inspired by the NeoGPU message fabric but adapted for ternary semantics.

Requirements:
- trit-defined QoS channels
- lock-free message passing
- soft opcode integration (TSEND/TRECV)

### Soft-Cache & Coherency
The system should establish a software-defined cache layer to reduce host RAM access latency during VM execution, managed by a coherency protocol to ensure data integrity across the host/VM boundary.

## Current Status

The ternary runtime foundation is implemented through `T010` and validated on hardware. The first host-foundation tasks `B001`, `B003`, `B004`, and `B005` are also implemented and validated on hardware. The next major phase is the broader host shell and service backlog starting with `B006` and `B007`.
