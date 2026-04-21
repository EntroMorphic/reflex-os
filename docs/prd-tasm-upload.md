# PRD: Runtime TASM Upload over Serial

## Problem

Every TASM program change requires a full firmware reflash (~30s). Iteration is slow.

## Goal

`vm upload <hex>` shell command loads a .rfxv binary into RAM and executes it
without reflash. Lost on reboot (RAM-only, no NVS persistence).

## Current State

- The shell already has `vm loadhex` which accepts hex-encoded bytecode inline.
- `tools/tasm.py` compiles `.tasm` -> `.rfxv` binary but has no upload capability.
- The gap is a host-side tool that reads a `.rfxv` file and sends it as hex over serial.

## Proposed Architecture

### Device side (shell)

- Add `vm upload` as an alias for `vm loadhex` (or rename `loadhex` to `upload`).
- No protocol change: shell reads hex string from serial, calls `reflex_vm_load_binary`.

### Host side (tasm.py)

Add `--upload <port>` flag:

```
python3 tools/tasm.py examples/tasm/count.tasm --upload /dev/cu.usbmodem1101
```

Flow:
1. Compile `.tasm` -> `.rfxv` (existing logic).
2. Open serial port at 115200 baud.
3. Send: `vm upload <hex-encoded-bytes>\n`.
4. Read response, print success/failure.

## Files to Modify

| File | Change |
|------|--------|
| `tools/tasm.py` | Add `--upload <port>` flag, serial send logic (~40 LOC) |
| `shell/shell.c` | Register `vm upload` as alias for `vm loadhex` (~10 LOC) |

## Non-Goals

- Binary framing protocol with checksums (v2).
- Upload over mesh/BLE.
- NVS persistence of uploaded programs.
- Large program chunking (current limit: single serial line).

## Validation

```bash
python3 tools/tasm.py examples/tasm/count.tasm --upload /dev/cu.usbmodem1101
```

Expected: device prints program output within 1s, no reflash required.

## Effort

~50 LOC Python, ~10 LOC C. ~30 minutes.
