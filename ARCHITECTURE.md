# Reflex OS Architecture

## Intent

Reflex OS is split into a binary host layer and a ternary runtime layer.

The binary host layer owns boot, hardware access, persistence, shell access, networking, and service lifecycle. The ternary runtime layer owns the ternary machine model, soft opcodes, VM execution, and the syscall boundary back into the host.

## Module Layout

## `main/`

ESP-IDF entrypoint only.

Responsibilities:

- call host startup
- currently runs bring-up self-checks and enters the serial shell
- should still shrink over time as `core/` takes ownership of boot behavior

## `core/`

Binary host runtime foundation.

Responsibilities:

- boot sequencing
- version metadata
- logging facade
- uptime helpers
- system state
- service manager
- event bus core

## `vm/`

Ternary compute runtime.

Responsibilities:

- ternary data representation
- ternary arithmetic helpers
- opcode definitions
- VM state model
- interpreter
- program loading
- syscall dispatch from ternary code into host services
- task runtime for scheduled VM execution

## `shell/`

Serial CLI and command dispatch.

Responsibilities:

- line input
- command parsing
- command registry
- system commands
- VM inspection and execution commands
- VM task commands

## `storage/`

Persistent state and configuration.

Responsibilities:

- NVS initialization
- typed config store
- boot counters
- failure markers
- recovery flags

## `services/`

Long-lived host services registered with the service manager.

Responsibilities:

- LED service
- button service
- timer service
- Wi-Fi manager service
- future OTA service
- future ternary task host service if needed

## `drivers/`

Hardware abstraction wrappers around ESP-IDF.

Responsibilities:

- GPIO
- LED
- button
- timer wrappers
- I2C scaffold
- SPI scaffold
- UART scaffold

## `net/`

Network-specific host logic.

Responsibilities:

- Wi-Fi connection lifecycle
- IP status
- future protocol adapters

## Dependency Rules

Allowed direction should remain simple:

- `main` may depend on `core`
- `core` may depend on `storage`, `shell`, `services`, `vm`, `drivers`, and `net`
- `shell` may depend on `core`, `storage`, `services`, `vm`, and `net`
- `services` may depend on `core`, `drivers`, `storage`, `net`, and `vm` only when intentionally hosting VM work
- `vm` may depend on `core` only through narrow host-facing interfaces and syscall hooks
- `drivers` should not depend on `vm`
- `net` should not depend on `vm`

## Design Constraint

Do not spread ternary logic across the whole codebase.

Ternary behavior should stay concentrated in `vm/` and in carefully defined host bridge points. The host remains binary and deterministic. The ternary machine is an execution model hosted by the OS, not a replacement for the ESP-IDF stack.

## First Refactor Target

`main/main.c` still contains boot-time self-check printing and shell handoff. The next host-phase cleanup should move that into `core/boot` or an equivalent startup module.
