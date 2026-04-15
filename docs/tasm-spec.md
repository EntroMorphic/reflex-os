# Reflex OS Ternary Assembly (TASM) Specification

## Syntax Overview

TASM files (`.tasm`) are line-oriented text files.

### Instructions

The assembler validates operands per opcode. Commas are optional.

- `TLDI DST, IMM`
- `TMOV DST, SRC`
- `TLD DST, ADDR_REG`
- `TST ADDR_REG, SRC`
- `TADD DST, SRC_A, SRC_B`
- `TSUB DST, SRC_A, SRC_B`
- `TCMP DST, SRC_A, SRC_B`
- `TSEL DST, SEL, ZERO, NEG, POS`
- `TJMP TARGET`
- `TBRNEG SRC, TARGET`
- `TBRZERO SRC, TARGET`
- `TBRPOS SRC, TARGET`
- `TSEND DST_REG, SRC, OP`
- `TRECV DST`
- `TFLUSH SRC`
- `TINV SRC`
- `TSYS DST, SRC_A, SRC_B, ID`
- `TNOP`
- `THALT`

### GOOSE-Native Geometric Opcodes

These opcodes bridge the VM into the GOOSE substrate for runtime route establishment, bias updates, and cell sampling.

- `TROUTE DST, SRC_A, SRC_B` — establish a GOOSE route in the VM's route manifest. `IMM=0`: arguments are name handles; `IMM=1`: arguments are 9-trit coordinates. Up to `REFLEX_VM_MAX_ROUTES` (4) routes per VM instance.
- `TBIAS DST, SRC_A` — update the orientation (bias trit) of an existing route. `DST` is the route index in the VM manifest; `SRC_A` holds the new bias trit.
- `TSENSE DST, SRC_A` — sample the state of a cell. `DST` receives the trit value; `SRC_A` identifies the cell. `IMM=0`: name handle lookup; `IMM=1`: coordinate lookup.

### Registers
`r0` through `r7`

### Literals (IMM)
- **Ternary:** Prefixed with `t`. Uses `+`, `0`, and `-`.
  - Example: `t+0-` (Value: 9 + 0 - 1 = 8)
- **Integer:** Standard decimal.
  - Example: `123`, `-45`
- **Labels:** Prefixed with `@` when used as an operand.
  - Example: `TJMP @loop_start`

### Labels
Defined by a trailing colon.
- Example: `start_process:`

### Comments
Start with `;` or `#`.

## Example Program
```tasm
; Simple loop that toggles LED on button press
init:
    TLDI r0, 1          ; Target LED Service
    TLDI r1, 9          ; Custom Event Type (Toggle)
    
loop:
    TRECV r2            ; Wait for message (from button)
    TBRZERO r2, @loop   ; If no message, loop
    TSEND r0, r2, 1     ; Send toggle to LED service
    TJMP @loop          ; Repeat
```

## Opcodes Mapping

| TASM | Binary ID | Description |
| :--- | :--- | :--- |
| `TNOP` | 0 | No Operation |
| `TLDI` | 1 | Load Immediate |
| `TMOV` | 2 | Copy Register |
| `TLD`  | 3 | Load from Memory |
| `TST`  | 4 | Store to Memory |
| `TADD` | 5 | Add |
| `TSUB` | 6 | Subtract |
| `TCMP` | 7 | Compare |
| `TSEL` | 8 | Select |
| `TJMP` | 9 | Jump |
| `TBRNEG` | 10 | Branch Negative |
| `TBRZERO`| 11 | Branch Zero |
| `TBRPOS` | 12 | Branch Positive |
| `TSEND`  | 13 | Send Message |
| `TRECV`  | 14 | Receive Message |
| `TFLUSH` | 15 | Flush Cache |
| `TINV`   | 16 | Invalidate Cache |
| `TSYS`   | 17 | Host Syscall |
| `THALT`  | 18 | Halt |
| `TROUTE` | 19 | Establish GOOSE route (Phase 10/11) |
| `TBIAS`  | 20 | Update route orientation |
| `TSENSE` | 21 | Sample GOOSE cell state |
