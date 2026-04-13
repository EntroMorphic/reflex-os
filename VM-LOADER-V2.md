# Reflex OS VM Loader Format v2 (Packed Bytecode)

## Purpose

This document defines the version 2 program image format for Reflex OS, moving from host-aligned structs to a dense packed bytecode format.

## Packing Specification

Each instruction is encoded as a 4-byte (32-bit) word.

| Bits | Field | Description |
| :--- | :--- | :--- |
| 0-5  | `opcode` | Soft Opcode ID (max 64 opcodes) |
| 6-8  | `dst`    | Destination Register (r0-r7) |
| 9-11 | `src_a`  | Source A Register (r0-r7) |
| 12-14| `src_b`  | Source B Register (r0-r7) |
| 15-31| `imm`    | 17-bit Signed Immediate |

## Image Structure (Binary)

The program image is stored as a contiguous byte stream:

1.  **Magic (4B):** `0x52465856` (RFXV)
2.  **Version (2B):** `0x0002`
3.  **Entry IP (2B):** Initial instruction index.
4.  **Instruction Count (2B):** Number of 4B instructions.
5.  **Data Count (2B):** Number of word18 data cells following the code.
6.  **Instruction Stream:** `4 * Instruction Count` bytes.
7.  **Data Stream:** `5 * Data Count` bytes (2-bit packed trits).

## Loading Process

1.  **Validate:** Header magic and version check.
2.  **Decode:** The loader iterates through the instruction stream, unpacking bit-fields into host `reflex_vm_instruction_t` structs.
3.  **MMU:** The loader maps the decoded code and the packed data stream into the VM's MMU.

## Benefits

- **Density:** Program size is reduced by ~50% compared to v1.
- **Portability:** Binary-level specification for future compilers and assemblers.
