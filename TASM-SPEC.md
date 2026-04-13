# Reflex OS Ternary Assembly (TASM) Specification

## Syntax Overview

TASM files (`.tasm`) are line-oriented text files.

### Instructions
`OPCODE [DST], [SRC_A], [SRC_B], [IMM]`

Not all fields are required. Commas are optional.

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
    TBRZERO @loop       ; If no message, loop
    TSEND r0, r2, r1    ; Send toggle to LED service
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
