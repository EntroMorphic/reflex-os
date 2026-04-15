# Reflex OS VM Loader Format

## Purpose

This document defines the MVP program image format and loader contract for Reflex OS.

The first loader is intentionally host-oriented. It does not yet parse a packed ternary byte stream. Instead, it validates and loads a structured in-memory image that points at decoded instruction records.

## Image Model

An MVP VM image contains:

- magic value
- format version
- entry instruction index
- instruction array pointer and length
- optional data memory pointer and length

This keeps the first loader simple while still creating a real validation boundary between arbitrary host data and executable VM state.

## Image Structure

- `magic` identifies Reflex VM images
- `version` allows future incompatible loader revisions
- `entry_ip` is the initial instruction pointer
- `instructions` points to a `reflex_vm_instruction_t` array
- `instruction_count` gives the number of instructions
- `memory` points to optional `word18` data cells
- `memory_count` gives the number of data cells

## Validation Rules

The MVP loader must reject images when:

- magic does not match
- version is unsupported
- instruction array is missing
- instruction count is zero
- entry point is out of range
- any instruction uses an invalid opcode
- any instruction uses invalid register fields for its opcode
- any jump or branch target is out of range
- any syscall selector is unsupported
- memory pointer is null while memory count is nonzero

The loader does not need to prove full program correctness yet. It only needs to ensure the image is structurally safe to hand to the interpreter.

## Load Contract

Loading an image into a VM instance should:

- validate the image first
- reset the VM register state and fault state
- attach the image's instruction and memory pointers into the VM state
- set `ip` to the validated entry point
- leave the VM in `READY` state

## MVP Scope

The first loader supports:

- decoded instruction arrays
- host-managed memory pointers
- entry-point selection

Deferred:

- packed bytecode decoding
- relocation
- literal pools
- checksums or signatures
- persistent image storage format
