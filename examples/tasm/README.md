# TASM Examples

TASM (Ternary Assembly) is the assembly language for the Reflex OS ternary VM.

## Quick Start: Compile and Upload

Compile and send directly to a connected board (no reflash needed):

```bash
python3 tools/tasm.py examples/tasm/count.tasm --upload /dev/cu.usbmodem1101
```

## Compiling

Compile to binary image (.rfxv):

```bash
python3 tools/tasm.py examples/tasm/blink.tasm /tmp/blink.rfxv
```

Compile to C array for firmware embedding:

```bash
python3 tools/tasm.py examples/tasm/blink.tasm vm/programs/blink.c
```

## Loading onto a Device

**Via shell** -- hex-encode the .rfxv file and load it:

```
reflex> vm loadhex <hex>
```

You must hex-encode the raw .rfxv binary before pasting it into the shell.

**Via firmware embedding** -- compile to .c, register it, rebuild, and flash:

1. Compile: `python3 tools/tasm.py my_prog.tasm vm/programs/my_prog.c`
2. Edit `vm/programs/programs.c` -- add externs and registry entry:
   ```c
   extern const uint8_t vm_program_my_prog[];
   extern const size_t vm_program_my_prog_len;
   // In ensure_init():
   s_programs[N] = (vm_program_t){"my_prog", vm_program_my_prog, vm_program_my_prog_len};
   ```
3. Add `"programs/my_prog.c"` to `vm/CMakeLists.txt` SRCS list
4. Rebuild and flash:
   ```bash
   idf.py build && idf.py -p /dev/cu.usbmodem1101 flash
   reflex> vm list
   reflex> vm run my_prog
   ```

## Example Programs

| File | Description |
|------|-------------|
| `blink.tasm` | Toggle state (+1/-1) every 500ms using TSUB negation. Logs each state. |
| `count.tasm` | Count from 0 to 9 in balanced ternary, log each value, then halt. |
| `respond.tasm` | Read system uptime every 1 second and log it in a loop. |

## Register Conventions

The VM has 8 general-purpose registers: `r0` through `r7`.
There are no reserved registers -- all are available for program use.
`r7` is conventionally used as a throwaway destination for syscall results.

## Syscalls

Syscalls are invoked with the `TSYS` instruction. The syscall selector
is specified as a name in the immediate field:

| Name | Description |
|------|-------------|
| `LOG` | Print a register value as a signed integer |
| `UPTIME` | Return system uptime in milliseconds |
| `CONFIG_GET` | Read a config value (key index in source register) |
| `DELAY` | Yield execution for N milliseconds |

Example:

```asm
TSYS r7, r2, r0, DELAY   ; sleep r2 milliseconds
TSYS r1, r0, r0, UPTIME  ; store uptime in r1
```
