# Ternary Architecture Note

## Decision Summary

Reflex OS will implement a balanced ternary virtual machine hosted on the ESP32-C6.

This means:

- silicon remains binary
- ESP-IDF and FreeRTOS remain the host runtime
- ternary execution is implemented through a software-defined instruction set
- ternary programs run inside a Reflex VM

## Why Balanced Ternary

Reflex OS uses balanced ternary values:

- `-1`
- `0`
- `+1`

Reasons:

- cleaner signed arithmetic semantics
- natural negative, neutral, and positive control flow states
- good fit for a ternary operating model and state machine design

## Ternary Machine Model

The MVP machine model is:

- a binary host OS
- one or more ternary VM instances
- a soft-opcode interpreter
- a controlled syscall bridge into host services

The host OS is responsible for:

- boot
- persistence
- logging
- shell
- service scheduling
- hardware access
- networking

The ternary VM is responsible for:

- storing ternary values
- executing ternary instructions
- maintaining ternary task state
- requesting host services through syscalls

## Soft Opcodes

Soft opcodes are software-defined VM instructions, not real ESP32-C6 CPU instructions.

The MVP opcode families should include:

- memory: `TLD`, `TST`
- arithmetic: `TADD`, `TSUB`
- compare: `TCMP`
- control flow: `TJMP`, `TBRNEG`, `TBRZERO`, `TBRPOS`
- host boundary: `TSYS`

Current implementation note:

- `TSEL` is intentionally deferred from the first encoding revision because it does not fit the current compact instruction shape cleanly

## Initial VM Recommendation

For MVP, use a register-based VM rather than a stack VM.

Reasons:

- easier to inspect from the shell
- easier to debug instruction-by-instruction
- more explicit opcode semantics
- better fit for a future assembler

## Initial Representation Direction

The exact packed representation is still a build task, but the implementation should follow these constraints:

- a trit is a logical value, not a C integer scattered through the codebase
- ternary words must have a documented packed binary format
- encode and decode helpers must be centralized in `vm/`
- higher layers should operate on VM types, not ad hoc integer conventions

## Host Boundary Rules

The ternary VM must not reach directly into drivers or ESP-IDF APIs.

All external effects should go through:

- host syscalls
- event publication
- service interfaces

This keeps the ternary machine portable, inspectable, and recoverable.

## MVP Success Condition

The ternary architecture is real once Reflex OS can:

- load a small ternary program
- execute soft opcodes deterministically
- inspect VM state from the shell
- invoke a host syscall from ternary code

Current state:

- all four conditions above are implemented and validated on the XIAO ESP32C6
