# Reflex OS Soft Opcode ISA

## Purpose

This document defines the MVP soft-opcode instruction set for the Reflex OS ternary VM.

Soft opcodes are VM instructions executed in software. They are not ESP32-C6 CPU instructions.

## Design Goals

- keep the first ISA small enough to implement and debug quickly
- make VM state easy to inspect from the shell
- use ternary compare results directly in control flow
- keep host effects behind a syscall boundary

## Machine Shape

The MVP VM is register-based.

Initial assumptions:

- data word: `word18`
- compare result: `trit`
- general register file: `r0` through `r7`
- instruction pointer: host-managed VM field

Register convention for compare and branch:

- compare results are stored as normalized `word18`
- trit slot `0` carries the compare result
- all higher trit slots are zeroed
- branch instructions inspect trit slot `0` of `SRC_A`

## Instruction Format

The exact binary encoding can stay provisional until `T005` and `T007`, but the semantic format for the MVP is:

`OP DST SRC_A SRC_B IMM`

Not every field is used by every instruction.

Working assumptions:

- `OP` selects the opcode
- `DST` selects the destination register
- `SRC_A` and `SRC_B` select source registers
- `IMM` provides a small immediate, offset, or syscall selector depending on opcode

MVP encoding constraints:

- this format supports at most two explicit source registers plus one destination register
- instructions that require more than two source registers are out of scope for the first encoding revision
- larger ternary literals must come from a later literal-pool or multiword-load design, not from `IMM`

## Opcode Families

## Data Movement

### `TNOP`
- No operation.

### `TLDI`
- Load a small signed immediate into `DST`.
- `IMM` is a host-side signed integer literal that is converted into a normalized `word18` value.
- This is intentionally a small-immediate instruction, not a full `word18` literal loader.

### `TMOV`
- Copy `SRC_A` into `DST`.

### `TLD`
- Load `DST` from ternary memory using address from `SRC_A` plus optional offset.

### `TST`
- Store `SRC_A` into ternary memory using address from `DST` plus optional offset.

## Arithmetic And Compare

### `TADD`
- `DST = SRC_A + SRC_B`
- Uses balanced ternary word addition.

### `TSUB`
- `DST = SRC_A - SRC_B`
- Uses balanced ternary word subtraction.

### `TCMP`
- Compare `SRC_A` and `SRC_B`
- Writes a normalized compare-result word into `DST`
- Trit slot `0` is one of `-1`, `0`, `+1`
- All remaining trit slots are zero

## Control Flow

### `TJMP`
- Unconditional jump by immediate or register-provided target.

### `TBRNEG`
- Branch when trit slot `0` in `SRC_A` is `-1`.

### `TBRZERO`
- Branch when trit slot `0` in `SRC_A` is `0`.

### `TBRPOS`
- Branch when trit slot `0` in `SRC_A` is `+1`.

### `THALT`
- Stop VM execution cleanly.

## Host Boundary

### `TSYS`
- Invoke a Reflex OS host syscall.
- `IMM` identifies the syscall number.
- `SRC_A` and `SRC_B` provide arguments or handles.
- `DST` receives the result word or status word.

## Initial Syscall Targets

The first syscall set should remain small:

- log output
- uptime/time query
- config read

GPIO and messaging can follow once the interpreter is stable.

## Error Model

The VM should trap and stop execution on:

- invalid opcode
- invalid register index
- invalid branch target
- invalid memory access
- invalid syscall selector
- arithmetic overflow where the VM chooses not to wrap

## MVP Execution Contract

The first interpreter only needs to support enough behavior to prove the architecture:

1. load a small program
2. move values through registers
3. perform ternary add and subtract
4. compute a ternary compare
5. branch on negative, zero, and positive
6. invoke one syscall
7. halt deterministically

## Recommended First Interpreter Slice

Implement in this order:

1. `TNOP`
2. `TLDI`
3. `TMOV`
4. `TADD`
5. `TSUB`
6. `TCMP`
7. `TBRNEG`
8. `TBRZERO`
9. `TBRPOS`
10. `TSYS`
11. `THALT`

Memory instructions can come immediately after the VM state model is settled.

## Deferred From The First Encoding Revision

### `TSEL`
- A true ternary select needs one selector and three candidate inputs.
- That does not fit the current `DST SRC_A SRC_B IMM` shape cleanly.
- `TSEL` is therefore deferred until the instruction format or VM calling convention is widened intentionally.
