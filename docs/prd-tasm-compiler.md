# PRD: TASM Compiler — Ternary Assembly Toolchain

## Problem Statement

The ternary VM exists (interpreter, loader, memory, cache, syscalls) and passes self-checks. But there's no way to CREATE programs for it. The spec (`docs/tasm-spec.md`) defines the instruction set but no toolchain compiles it. The VM is a complete engine with no fuel.

## Goal

A developer writes a `.tasm` file, compiles it, rebuilds firmware, and runs it:

```bash
python tools/tasm/tasm.py examples/tasm/blink.tasm -o vm/programs/blink.c
idf.py build && idf.py flash
reflex> vm run blink
reflex> vm info
vm: running program=blink pc=0x004 ticks=127
```

## Non-Goals

- High-level language (no C-to-TASM compiler)
- Runtime program upload via serial (v2 — needs framing protocol)
- Debugger/stepper (v2)
- Optimizer (programs are small; optimization adds complexity without value)

---

## Architecture

### Pipeline

```
blink.tasm → [tasm.py parser] → [label resolver] → [binary emitter] → blink.c
                                                                         ↓
                                                              compiled into firmware
                                                                         ↓
                                                              reflex> vm run blink
```

### VM Image Format

From `vm/loader.c` (`reflex_vm_validate_image`), the binary format is:

```
Offset  Size  Field
0x00    4     Magic (must match REFLEX_VM_IMAGE_MAGIC)
0x04    2     Version
0x06    2     Instruction count
0x08    2     Entry point (instruction index)
0x0A    2     Register count (must be ≤ VM max)
0x0C    N×4   Instructions (packed 18-trit words as uint32_t)
```

Each instruction is a 32-bit packed word encoding:
- Opcode (bits 5:0, 6 bits)
- Destination register (bits 8:6, 3 bits)
- Source A register (bits 11:9, 3 bits)
- Source B register (bits 14:12, 3 bits)
- Signed immediate (bits 31:15, 17 bits)

### Instruction Set

From `vm/include/reflex_vm_opcode.h`:

| ID | Mnemonic | Description |
|----|----------|-------------|
| 0 | TNOP | No operation |
| 1 | TLDI | Load immediate into DST |
| 2 | TMOV | Copy register SRC_A into DST |
| 3 | TLD | Load from private memory |
| 4 | TST | Store to private memory |
| 5 | TADD | DST := SRC_A + SRC_B (balanced ternary) |
| 6 | TSUB | DST := SRC_A - SRC_B (balanced ternary) |
| 7 | TCMP | Compare SRC_A, SRC_B; sign into DST trit[0] |
| 8 | TSEL | Three-way select on SRC_A's sign |
| 9 | TJMP | Unconditional branch to IMM target |
| 10 | TBRNEG | Branch if SRC_A trit[0] < 0 |
| 11 | TBRZERO | Branch if SRC_A trit[0] == 0 |
| 12 | TBRPOS | Branch if SRC_A trit[0] > 0 |
| 13 | TSEND | Send fabric message |
| 14 | TRECV | Receive fabric message |
| 15 | TFLUSH | Flush soft cache line |
| 16 | TINV | Invalidate soft cache line |
| 17 | TSYS | Host syscall (selector in IMM) |
| 18 | THALT | Halt VM cleanly |
| 19 | TROUTE | Establish GOOSE route |
| 20 | TBIAS | Update route orientation |
| 21 | TSENSE | Sample cell state |

### Syscalls

| ID | Name | Args | Description |
|----|------|------|-------------|
| 0 | LOG | SRC_A | Print register value as signed integer |
| 1 | UPTIME | DST | Return uptime in milliseconds |
| 2 | CONFIG_GET | SRC_A, DST | Return config value (SRC_A = key) |
| 3 | DELAY | SRC_A | Yield task for SRC_A milliseconds |

---

## Implementation Plan

### Phase 1: Assembler (Python)

`tools/tasm/tasm.py` (~400 LOC):

```python
class Assembler:
    def __init__(self):
        self.instructions = []
        self.labels = {}
        self.current_addr = 0
    
    def parse_file(self, path):
        """Parse .tasm file into instruction list."""
        # Handle: labels, instructions, .data directives, comments
    
    def resolve_labels(self):
        """Replace label references with instruction indices."""
    
    def encode_instruction(self, instr) -> uint32_t:
        """Pack opcode + registers + immediate into 32-bit word."""
    
    def emit_image(self) -> bytes:
        """Produce VM image with header + instruction array."""
    
    def write_c_array(self, path, name):
        """Output as const uint8_t program_<name>[] = {...};"""
```

**TASM Syntax (actual — from `examples/tasm/blink.tasm`):**

```asm
; blink.tasm — toggle state every 500ms
.entry main

main:
    TLDI r0, 1           ; state = +1
    TLDI r1, 0           ; zero register
loop:
    TSYS r7, r0, r0, LOG ; log state
    TSUB r0, r1, r0      ; negate: 0 - state = -state
    TLDI r2, 500
    TSYS r7, r2, r0, DELAY ; wait 500ms
    TJMP @loop
```

### Phase 2: Shell Command

In `shell/shell.c` (~50 LOC):

```c
} else if (strcmp(argv[0], "vm") == 0) {
    if (argc >= 2 && strcmp(argv[1], "run") == 0) {
        // Look up embedded program by name
        // Load into VM via reflex_vm_load_image
        // Start execution in a task
    }
}
```

**Program registry:** compile-time table of embedded programs:

```c
// vm/programs/programs.h (auto-generated or manual)
typedef struct { const char *name; const uint8_t *data; size_t len; } vm_program_t;
extern const vm_program_t vm_programs[];

// vm/programs/blink.c (output of tasm.py)
const uint8_t vm_program_blink[] = { 0x52, 0x46, ... };
```

### Phase 3: Example Programs

`examples/tasm/`:

| Program | Instructions | What it does |
|---------|-------------|-------------|
| `blink.tasm` | 7 | Toggle state (+1/-1) every 500ms via TSUB negation |
| `count.tasm` | 9 | Count 0-9 in ternary, log each value, halt |
| `respond.tasm` | 6 | Read uptime every 1s, log it |

---

## Validation Plan

### Compiler Tests

`tests/host/test_tasm.py` (~50 LOC):

```python
def test_encode_nop():
    asm = Assembler()
    asm.parse_line("NOP")
    assert asm.instructions[0].opcode == 0x00

def test_label_resolution():
    asm = Assembler()
    asm.parse_lines(["loop:", "NOP", "BR loop"])
    asm.resolve_labels()
    assert asm.instructions[1].target == 0

def test_image_validation():
    # Compile a program, load into VM loader, verify passes validation
    asm = Assembler()
    asm.parse_file("examples/tasm/count.tasm")
    image = asm.emit_image()
    # Feed to reflex_vm_validate_image (host build)
```

### On-Device Validation

1. Compile `blink.tasm` → `vm/programs/blink.c`
2. Rebuild firmware with embedded program
3. Flash to Alpha
4. `reflex> vm run blink` → LED blinks at 2Hz
5. `reflex> vm info` → shows running state, PC advancing
6. `reflex> vm stop` → LED stops

### End-to-End

```bash
python tools/tasm/tasm.py examples/tasm/blink.tasm -o vm/programs/blink.c
idf.py build && idf.py -p /dev/cu.usbmodem1101 flash
# Connect serial
reflex> vm run blink
# LED blinks
reflex> vm info
vm: running program=blink pc=0x003 ticks=42
```

---

## Pre-Implementation Study

Before writing the compiler, verify the VM image format by examining:

1. `vm/include/reflex_vm_opcode.h` — exact opcode values
2. `vm/loader.c` — `reflex_vm_validate_image` checks (magic, version, register bounds, branch targets, syscall selectors)
3. `vm/interpreter.c` — how instructions are decoded at runtime
4. `tests/vm_test_main.c` — `reflex_vm_loader_self_check` shows a known-good image

**Critical question:** what is `REFLEX_VM_IMAGE_MAGIC`? What version does the loader expect? What's the exact bit layout of a packed instruction? These must be answered from the source before the compiler can emit valid images.

---

## Risks

| Risk | Severity | Mitigation |
|------|----------|-----------|
| VM image format mismatch | HIGH | Study loader + self-check before writing emitter |
| Instruction encoding wrong | HIGH | Test: compile known program, validate with loader |
| Syscall READ_CELL/WRITE_CELL need cell name encoding | MEDIUM | Use cell index or string literal in data section |
| VM task stack overflow on complex programs | LOW | Default 4KB stack; configurable |
| No runtime program upload (v1) | LOW | Compile-time embedding is sufficient for v1 |

---

## Success Criteria

1. `tasm.py` compiles all 3 example programs without error
2. Compiled image passes `reflex_vm_validate_image` on host
3. `vm run blink` on hardware produces visible LED blink at expected frequency
4. `vm info` shows running state with advancing PC
5. `vm stop` halts execution cleanly
6. Compiler tests pass in CI (`make test` includes tasm tests)

---

## Dependencies

- R1 (KV compaction) — DONE
- R2 (integration test) — DONE
- VM interpreter + loader — EXISTS (passes self-checks)
- `docs/tasm-spec.md` — EXISTS (instruction set specification)

## Effort

| Component | LOC | Time |
|-----------|-----|------|
| `tasm.py` assembler | ~400 | 0.5 day |
| Shell `vm run/stop` commands | ~50 | 1 hour |
| Program registry + 3 example programs | ~80 | 1 hour |
| Compiler tests (Python) | ~50 | 1 hour |
| Pre-implementation study (loader/format) | — | 1 hour |
| **Total** | **~580** | **1 day** |
