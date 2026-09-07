# Adoption Gaps — 2026-09-07

What Reflex OS still needs before a hobbyist or a lab can use it to control ESP32
hardware and write programs that matter to them.

Companion to [`prd-developer-experience.md`](prd-developer-experience.md), not a
replacement for it. That PRD is about *contributor* experience — tests, CI,
formatting, onboarding — and is largely delivered: `make test` runs 396 host
assertions without hardware, eight CI jobs gate every push, and the format gate
became real on 2026-09-07. This document is about the other audience entirely:
someone who wants to *use* Reflex OS to build something, and whose first evening
with it decides whether there is a second one.

Assessed at commit `9609a08`, immediately after the
[`P0-07SEP26.md`](P0-07SEP26.md) remediation.

---

## 1. Method, and what is not verified

Read from source: the HAL surface, the shell command table, the VM opcode set and
syscall table, both compilers, the storage layer, and the fabric's hardware-binding
path. Every claim below names the file and line it came from.

**Not verified:** no firmware was built and no board was attached. Nothing here is
hardware-observed. Where a claim depends on runtime behaviour rather than on what
the code plainly does, it is marked as such. The sequencing in section 5 is a
code-grounded argument, not a validated plan.

---

## 2. The shape of the problem

The substrate is well ahead of the surface.

Reflex OS has a 12,738-entry shadow atlas of ESP32-C6 registers, an
Aura-authenticated 802.15.4 mesh, four-role capability-based access control, a
ternary VM with an MMU and a soft cache, metabolic scheduling, Hebbian plasticity,
and snapshot persistence for learned state. That is an unusual amount of real
machinery, and most of it works.

Almost none of it is reachable by someone who has not read the C.

The gaps below are therefore mostly not "this was never built". They are "this was
built and never exposed", which is a materially better position to be in — three of
the six are wiring, not construction. But the effect on a newcomer is identical,
and worse in one respect: the capability is visible in the docs, so its absence at
the prompt reads as a broken promise rather than an honest limit.

---

## 3. Gaps, ranked by whether a newcomer survives them

### A1 — Nothing a user creates survives a reboot
**BLOCKING.**

`vm loadhex` (`shell/shell.c:1345-1358`) decodes into `reflex_shell_vm` in RAM.
`vm run <name>` (`:1324`) runs only programs compiled into the firmware via
`vm_program_find`. Loom fragments weave into RTC-backed fabric, but the fragment
bytes are not kept, so nothing re-weaves them on a cold boot.

Storage is `reflex_kv_*` (`storage/storage.c`) — an NVS key-value store. There is
no filesystem and no program store. Snapshots persist *learned route state*
(`goose_snapshot_save`), which is a different thing: it preserves what the
substrate learned, not what the user wrote.

The loop a user actually experiences is: write a program, upload it, lose it on
the next power cycle. Every other gap is survivable; this one ends the evening.

### A2 — The command that runs a program can take the board away
**BLOCKING.**

`vm run` calls `reflex_vm_run(&reflex_shell_vm, 100000)` synchronously
(`shell/shell.c:1332`), inside the shell task. `vm stop` (`:1336`) sets the status
field the run loop checks — but the shell is *inside* `reflex_vm_run` and never
returns to read the next line, so the command cannot be issued. `reboot` is
unreachable for the same reason. Recovery is a physical reset.

Already documented at `USER_MANUAL.md:285`. Documenting a trap is not the same as
removing it, and this is the first command a new user will reach for.

The fix is already built and idle. `vm/task_runtime.c` runs a VM on its own task
with a bounded step slice, a stop path, and service lifecycle hooks; `main.c:164-168`
registers `system_vm` as a service. It never runs anything, because
`reflex_vm_task_service_start` returns early when `runtime->image == NULL`
(`task_runtime.c:263`). Routing `vm run` through that runtime instead of the shell's
own VM state is the single highest-leverage change in this document. (The soft
cache that runtime was given only started working on 2026-09-07 — see P0-M3.)

### A3 — The peripherals people buy the chip for are not reachable
**BLOCKING for "control hardware".**

`include/reflex_hal.h` is 19 functions: GPIO input/output/level/matrix-connect,
temperature, time, interrupt alloc/free, random, MAC, reboot, sleep, log, CPU
cycles, raw write.

No I2C, SPI, ADC, PWM, I2S, RMT, PCNT, or general timers. No sensor, no display,
no servo, no analog input. `driver/ledc.h` appears only inside `shell/shell.c:11`
for the onboard LED.

But the raw path exists. `goose_fabric_set_agency` (`goose_runtime.c:599`) binds a
cell to any GPIO index or MMIO address, guarded by the Sanctuary Guard, and the
shadow atlas already knows 12,738 registers by name. It is called only from C —
`goose_atlas.c:101`, `goose_runtime.c:182`, `goose_supervisor.c:922`. **No shell
command binds a cell to a register of the user's choosing.**

This is the gap where the architecture's own idea — peripherals as woven cells
rather than driver APIs — is both the most interesting answer available and
completely unavailable at the prompt.

### A4 — Neither language can express a real program
**SEVERE.**

**TASM** (`tools/tasm.py:8-31`) has 22 opcodes and 8 registers, and no `CALL`,
`RET`, `PUSH` or `POP`. No subroutines means no reusable code and no libraries —
every program is a single flat routine. No multiply or divide; `TADD` and `TSUB`
only. Four syscalls (`LOG`, `UPTIME`, `CONFIG_GET`, `DELAY`,
`reflex_vm_opcode.h:66-70`), **none of which touch hardware** — the only hardware
path from a program is `TSENSE`/`TROUTE`/`TBIAS` acting on cells that something
else, written in C, has already bound.

**LoomScript** has one verb. `weave <src> -> <snk>`, and nothing else — no
conditionals, no parameters, no transitions. The `.loom` header carries a
`trans_count` field and no transition format was ever implemented; until
2026-09-07 the loader parsed it and silently dropped it, and now honestly refuses
it (P0-H2).

So the two ways to write a program are an assembler that cannot call a function
and a wiring language with one statement.

### A5 — Programs cap out near 500 bytes
**SEVERE.**

`vm loadhex` and `loom load` both arrive on a shell input line bounded at 1023
characters. `tools/tasm.py` enforces the consequence directly: a program whose hex
plus `"vm loadhex \n"` exceeds 1024 is refused, "max ~500" bytes. At 4 bytes per
packed instruction plus a 16-byte header, that is roughly 120 instructions.

There is no chunking, no XMODEM, no OTA. The line length was raised from 255 to
1023 in an earlier pass, which moved the ceiling without removing it.

### A6 — No debugging, and no way to share what you made
**IMPORTANT.**

A program that faults yields a status enum (`reflex_vm_status_name`) and a step
count. There is no breakpoint, no single-step, no register dump mid-run, and no
fault address. Telemetry (`#T:`) describes the substrate — cells, routes,
evictions — not the user's program.

And there is nowhere for a finished program to go: no package format, no
registry, no examples gallery beyond three `.tasm` files and two `.ls` files.
PyPI publication is still a PRD ([`prd-pypi-publish.md`](prd-pypi-publish.md)).

---

## 4. What is already good

Recorded so the sequencing argument below is fair, and so this reads as a
punch-list rather than a verdict.

- **The shell is genuinely pleasant.** 23 commands, line editing, four-role access
  control, and a machine-readable `#R:` outcome trit on every command — that last
  one is better than most hobby firmware offers, and is what makes the Python SDK
  honest rather than string-matching.
- **The Python SDK is real.** `ReflexNode` with typed outcomes, `AccessDenied`,
  and injection-resistant token handling.
- **The mesh works and is authenticated.** Aura HMAC, replay protection, ingress
  and (as of 2026-09-07) egress rate limits, self-arc suppression.
- **Testing and CI are solid.** 396 host assertions with no hardware, plus TASM
  and LoomScript compiler suites, six CI jobs, and a hardware validation script
  (`make hw-test`) that is non-destructive by construction.
- **The docs are unusually candid.** `SECURITY.md` states its own limits;
  `implementation-status.md` keeps a Known Gaps list that leads the code.

The distance from here to usable is shorter than the gap list makes it look.

---

## 5. Suggested order

A1 through A3 are what decide whether someone stays. Two of the three are wiring
existing capability to the surface rather than building anything new.

| # | Change | Nature | Unblocks |
|---|---|---|---|
| 1 | Route `vm run` through `vm/task_runtime.c` instead of the shell task | **wiring** — the runtime exists, is registered, and is idle | A2 |
| 2 | Program store: a partition plus `vm save <name>` / `vm ls` / `vm autorun <name>` | new, small | A1 |
| 3 | `bind` shell verb over `goose_fabric_set_agency`, with atlas name lookup | **wiring** — guard and atlas already exist | A3 |
| 4 | Chunked upload (`vm loadhex --begin/--chunk/--end`) or OTA | new, small | A5 |
| 5 | `CALL`/`RET` plus a call stack; hardware syscalls (`GPIO_GET`/`GPIO_SET`) | VM change, needs opcode space | A4 |
| 6 | `vm step`, `vm regs`, `vm break`; a `.loom`/`.rfxv` package format | new | A6 |

Steps 1 and 3 are the ones to do first: they are small, they are reversible, and
each converts machinery that already exists into something a user can reach.

Step 5 needs care — the opcode field is 6 bits with 22 of 64 values used, so there
is room, but `CALL`/`RET` implies a stack the VM state does not currently have
(`reflex_vm_state.h:87` is 8 registers and no stack pointer), and the MMU would
need a region for it.

---

## 6. What "good" would look like

Concrete enough to test, because "better DX" is not a target anyone can hit.

1. A new user flashes a board, opens the shell, and blinks an LED **from a program
   they wrote**, in under 15 minutes, following only `GETTING_STARTED.md`.
2. That program survives a power cycle and runs on boot.
3. No shell command can render the board unresponsive. `vm stop` always works.
4. A user reads an I2C sensor without writing or compiling any C.
5. A program that faults reports *where*, not just *that*.
6. A user can hand another user one file, and it runs.

None of the six is true today. The first three are reachable with the four changes
at the top of section 5.

---

## 7. Relationship to other documents

- [`prd-developer-experience.md`](prd-developer-experience.md) — contributor
  experience: tests, CI, formatting. Largely delivered; this document deliberately
  does not restate it.
- [`P0-07SEP26.md`](P0-07SEP26.md) — correctness audit. Several fixes there (the
  VM task cache in P0-M3, the loom parser in P0-H2, both compilers in P0-H4/M5)
  are prerequisites for the changes proposed here.
- [`implementation-status.md`](implementation-status.md) — the running Known Gaps
  list. A1–A6 are registered there.
- [`prd-full-independence.md`](prd-full-independence.md),
  [`prd-tasm-compiler.md`](prd-tasm-compiler.md) — prior roadmap documents whose
  scope overlaps A4 and A5.
