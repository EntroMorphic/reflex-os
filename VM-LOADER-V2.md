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
3.  **CRC32 (4B):** CRC32 over the instruction and data payload.
4.  **Entry IP (2B):** Initial instruction index.
5.  **Instruction Count (2B):** Number of 4B instructions.
6.  **Data Count (2B):** Number of word18 data cells following the code.
7.  **Instruction Stream:** `4 * Instruction Count` bytes.
8.  **Data Stream:** `5 * Data Count` bytes (2-bit packed trits).

## Loading Process

1.  **Validate:** Header magic, version, CRC32, entry point, and decoded fields.
2.  **Decode:** The loader iterates through the instruction stream, unpacking bit-fields into host `reflex_vm_instruction_t` structs.
3.  **Unpack Data:** The loader unpacks 2-bit-per-trit `word18` payload cells into host memory.
4.  **MMU:** The loader maps the unpacked private data segment into the VM MMU at base address `0`.

## Benefits

- **Density:** Program size is reduced by ~50% compared to v1.
- **Portability:** Binary-level specification for future compilers and assemblers.
