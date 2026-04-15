# Reflex OS VM State Model

## Purpose

This document defines the MVP runtime machine state for the Reflex OS ternary VM.

The goal is to make interpreter execution deterministic, inspectable, and small enough to implement quickly on the ESP32-C6.

## Core Decision

The MVP VM is a register-based machine with host-indexed instruction memory.

That means:

- the host stores the instruction array in binary memory
- the instruction pointer is an instruction index, not a ternary memory word address yet
- registers store `word18` values
- compare results live in ordinary registers using the `TCMP` convention from `T004`

## VM State Fields

An MVP VM instance should contain:

- lifecycle state
- fault state
- instruction pointer
- step counter
- register file
- program pointer and program length
- optional data memory pointer and length
- last syscall result or last trap context later

## Register File

- register count: `8`
- register type: `word18`
- register names: `r0` through `r7`

Rules:

- all registers are initialized to zero on reset
- `TCMP` writes a normalized compare-result word into `DST`
- branch instructions inspect trit slot `0` of their source register

## Instruction Pointer

For MVP, the instruction pointer is a host-side unsigned instruction index.

Why:

- simpler than using ternary-addressed instruction memory immediately
- easy to debug and print from the shell
- avoids premature coupling between loader encoding and runtime control flow

Future work may move instruction memory toward ternary addressing once the interpreter and loader are stable.

## Program Image Model

The first interpreter should execute a host-loaded instruction array.

Program model:

- `program` points to an array of `reflex_vm_instruction_t`
- `program_length` is the number of valid instructions
- `ip` must remain in the range `[0, program_length)` unless the VM halts

This is intentionally simpler than a packed bytecode stream for the first execution slice.

## Data Memory Model

The first interpreter can carry an optional ternary data memory block, even if only arithmetic and branching are implemented first.

Recommended MVP shape:

- `memory` points to an array of `word18`
- `memory_length` counts `word18` cells

This keeps `TLD` and `TST` compatible with the state model even if they land after the first interpreter slice.

## Lifecycle States

Recommended VM statuses:

- `READY` initialized and ready to execute
- `RUNNING` actively executing instructions
- `HALTED` stopped by `THALT`
- `FAULTED` stopped by an execution error

## Fault Model

The VM should record a coarse fault reason when execution stops abnormally.

Initial fault classes:

- none
- invalid opcode
- invalid register index
- invalid immediate usage
- program counter out of range
- arithmetic overflow
- invalid syscall
- invalid memory access

## Execution Accounting

The VM should track a monotonic step counter.

Why:

- bounded execution slices need it
- shell diagnostics need it
- later scheduling will use it for fairness and debugging

## Reset Semantics

Resetting a VM instance should:

- zero registers
- set `ip` to `0`
- clear the fault code
- set lifecycle state to `READY`
- zero the executed-step counter

It should not necessarily erase loaded program or data memory unless explicitly requested.

## MVP Success Criteria

The `T005` state model is sufficient when:

- a VM instance can be zero-initialized predictably
- the interpreter can fetch by instruction index
- registers can hold arithmetic and compare results
- faults can be recorded without crashing the host runtime
- shell tooling can print the VM state in a stable format
