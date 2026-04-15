# Ternary VM — Internals

The ternary VM lives in [`../../vm/`](../../vm/) and provides the execution substrate for TASM programs and LoomScript fragments.

## Files in this directory

- [`cache.md`](cache.md) — Three-state (I/E/M) MESI-lite soft cache and the host-side Coherency Proxy.
- [`loader.md`](loader.md) — Original packed-image loader.
- [`loader-v2.md`](loader-v2.md) — V2 loader: 32-bit dense instruction packing, CRC32 verification, packed data segments.
- [`state.md`](state.md) — Register file, flags, instruction pointer, fault state.
- [`syscalls.md`](syscalls.md) — Syscall dispatch from ternary code into host services.

## Source layout

| Source | Header | Purpose |
|---|---|---|
| `vm/ternary.c` | `reflex_ternary.h` | Trit / tryte9 / word18 primitives |
| `vm/mmu.c` | `reflex_vm_mem.h` | Region-protected memory |
| `vm/cache.c` | `reflex_cache.h` | Soft cache (see `cache.md`) |
| `vm/interpreter.c` | `reflex_vm.h` | Instruction dispatch |
| `vm/loader.c` | `reflex_vm_loader.h` | Packed image loading (see `loader.md`, `loader-v2.md`) |
| `vm/syscall.c` | — | Host syscall bridge (see `syscalls.md`) |
| `vm/task_runtime.c` | `reflex_vm_task.h` | Background VM task hosting |

## Assembler

Ternary assembly source compiles via [`../../tools/tasm.py`](../../tools/tasm.py). Syntax reference: [`../tasm-spec.md`](../tasm-spec.md).

Example program: [`../../examples/supervisor.tasm`](../../examples/supervisor.tasm) — the canonical background supervisor that routes fabric messages to the LED.
