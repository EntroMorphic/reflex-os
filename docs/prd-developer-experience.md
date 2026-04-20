# PRD: Developer Experience

## Problem Statement

Reflex OS has strong architecture and novel capabilities, but the developer experience gaps slow iteration, block contributions, and make debugging painful. A developer who isn't the original author faces: no tests they can run locally, no CI to catch regressions, no formatted code standards, no way to iterate without hardware, and minimal documentation on how the pieces connect.

## Goals

1. A developer can clone the repo and run meaningful tests in under 60 seconds without hardware
2. Every push to `main` is automatically validated (build + test)
3. Code quality is enforced by tooling, not reviews
4. A new contributor can add a platform backend or shell command without reading the entire codebase
5. A crash on hardware can be diagnosed in under 5 minutes with the right tools

## Non-Goals

- IDE-specific tooling (VS Code extensions, JetBrains plugins)
- GUI debugging tools
- Performance profiling framework
- Automated hardware-in-the-loop testing (future, requires lab setup)
- Release process automation (version bumps, tags — defer to v2)

## Notes

- **Effort estimates assume codebase familiarity.** A new contributor should double the time estimates.
- **Existing test infrastructure**: `tests/vm_test_main.c` + `Makefile.host` already runs ternary + loader self-checks on macOS. D1 EXTENDS this, not replaces it.
- **First format commit**: The initial `make format` run will produce a large diff. This is one "reformat all" commit, then hooks enforce from that point forward.

---

## Deliverable 0: Zero-Effort IDE Support + Config Reset

### What

Two Makefile targets that cost nothing but unlock significant DX:

1. **`compile_commands.json` generation** — enables LSP autocomplete and jump-to-definition in any editor (VS Code, Neovim, Emacs, CLion).
2. **`make config-reset`** — restores sdkconfig to known-good defaults after experimentation with radio backends, kernel scheduler, etc.

### Implementation

In `CMakeLists.txt`:
```cmake
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
```

In `Makefile`:
```makefile
config-reset:
	rm -f sdkconfig
	idf.py set-target esp32c6
	@echo "sdkconfig regenerated from defaults"
```

### Effort

3 lines. 5 minutes.

---

## Deliverable 1: Host-Side Test Runner

### What

A build target (`make test`) that compiles the substrate logic, ternary VM, and kernel scheduler against a mock HAL on macOS/Linux. Runs without ESP-IDF, without hardware. EXTENDS the existing `tests/vm_test_main.c` + `Makefile.host` infrastructure.

### Why

Today, every code change requires flash + serial monitor + manual validation (~45 seconds per iteration). Host-side tests reduce this to ~1 second. Most bugs are logic errors (off-by-one, wrong sign, missed edge case) that don't need real hardware to catch.

### Scope

| Module | Testable on host? | Mock required |
|--------|------------------|---------------|
| Ternary math (`vm/ternary.c`) | Yes | None (pure math) |
| VM interpreter (`vm/interpreter.c`) | Yes | Minimal syscall mock |
| VM loader (`vm/loader.c`) | Yes | None (pure parsing) |
| Kernel scheduler (`kernel/reflex_sched.c`) | Yes | Timer mock |
| Kernel task backend (`kernel/reflex_task_kernel.c`) | Yes | None (uses scheduler) |
| GOOSE runtime (`goose_runtime.c`) | Yes | HAL mock (time, GPIO stub) |
| GOOSE supervisor (`goose_supervisor.c`) | Yes | HAL mock + task mock |
| Crypto (`reflex_crypto_esp32c6.c`) | Yes | None (pure C) |
| Radio, WiFi, sleep | No | Hardware-only |

### Implementation

```
tests/
  host/
    Makefile              # Builds + runs all host tests
    mock_hal.c            # Stub implementations of reflex_hal.h
    mock_task.c           # Stub implementations of reflex_task.h
    test_ternary.c        # Ternary arithmetic + VM checks
    test_scheduler.c      # Task create, yield, priority, delay
    test_goose.c          # Cell alloc, route propagation, Hebbian
    test_crypto.c         # SHA-256 known-answer tests
    test_kv.c             # KV store read/write/erase (RAM-backed)
```

**Mock HAL** (~100 LOC):
```c
uint64_t reflex_hal_time_us(void) { return s_mock_time_us; }
void reflex_hal_delay_us(uint32_t us) { s_mock_time_us += us; }
reflex_err_t reflex_hal_gpio_set_level(uint32_t pin, int level) { return REFLEX_OK; }
// ... stubs for all reflex_hal.h functions
```

**Mock Task** (~50 LOC):
Wraps the kernel scheduler directly (it already uses setjmp/longjmp, which works on host).

### Success Criteria

- `make test` runs in <5 seconds on macOS
- Covers: ternary math (27 operations), VM execution (5 programs), scheduler (create/yield/priority/delay), GOOSE (cell alloc, route propagation, Hebbian commit), crypto (3 known-answer vectors)
- Zero hardware dependencies
- Exit code 0 = all pass, non-zero = failure details printed

### Mock Divergence Risk

As the real HAL gains functions, the mock must be updated in lockstep. Mitigation: CI builds BOTH host tests AND firmware. If the HAL interface adds a function, the mock build breaks (undefined symbol) — forcing the developer to add the stub.

### Effort

~300 LOC mocks + ~400 LOC tests = ~700 LOC total. 1 day.

---

## Deliverable 2: CI Pipeline

### What

GitHub Actions workflow that runs on every push and PR:
1. Build ESP32-C6 firmware (ESP-NOW mode)
2. Build ESP32-C6 firmware (802.15.4 mode)
3. Build ESP32 firmware
4. Run host-side tests

### Why

Today there's a badge in the README pointing to a non-existent workflow. No automated gate prevents pushing broken code. Two developers working in parallel have no way to know if their changes conflict until they flash hardware.

### Implementation

```yaml
# .github/workflows/build.yml
name: Build & Test
on: [push, pull_request]
jobs:
  host-tests:
    runs-on: ubuntu-latest
    steps:
      - uses: actions/checkout@v4
      - run: make -C tests/host test

  build-esp32c6:
    runs-on: ubuntu-latest
    container: espressif/idf:release-v5.5  # Verify tag at hub.docker.com/r/espressif/idf/tags
    steps:
      - uses: actions/checkout@v4
      - run: idf.py set-target esp32c6 && idf.py build

  build-esp32c6-802154:
    runs-on: ubuntu-latest
    container: espressif/idf:release-v5.5  # Verify tag at hub.docker.com/r/espressif/idf/tags
    steps:
      - uses: actions/checkout@v4
      - run: |
          sed -i 's/CONFIG_REFLEX_RADIO_ESPNOW=y/# CONFIG_REFLEX_RADIO_ESPNOW is not set/' sdkconfig.defaults
          echo "CONFIG_REFLEX_RADIO_802154=y" >> sdkconfig.defaults
          idf.py set-target esp32c6 && idf.py build

  build-esp32:
    runs-on: ubuntu-latest
    container: espressif/idf:release-v5.5  # Verify tag at hub.docker.com/r/espressif/idf/tags
    steps:
      - uses: actions/checkout@v4
      - run: |
          SDKCONFIG_DEFAULTS="sdkconfig.defaults.esp32" idf.py -B build_esp32 set-target esp32
          idf.py -B build_esp32 build
```

### Success Criteria

- Green badge in README means: all three targets build + host tests pass
- Build time < 10 minutes total (parallel jobs)
- PR checks block merge on failure

### Effort

~50 lines YAML + Makefile integration. Half day.

---

## Deliverable 3: Code Formatting & Pre-Commit Hooks

### What

- `.clang-format` config enforcing consistent C style
- `pre-commit` hook that runs format check + compile check before every commit
- `make format` target to auto-format all source

### Why

Code style inconsistencies waste review time. Formatting arguments are eliminated by automation. A compile check before commit catches typos immediately.

### Style Decisions

```yaml
# .clang-format
BasedOnStyle: LLVM
IndentWidth: 4
ColumnLimit: 100
AllowShortFunctionsOnASingleLine: Inline
AllowShortIfStatementsOnASingleLine: true
BreakBeforeBraces: Attach
```

### Pre-Commit Hook

```bash
#!/bin/bash
# .githooks/pre-commit
FILES=$(git diff --cached --name-only --diff-filter=ACM | grep -E '\.(c|h)$')
if [ -n "$FILES" ]; then
    clang-format --dry-run --Werror $FILES || {
        echo "Run 'make format' to fix formatting."
        exit 1
    }
fi
```

Activated via: `git config core.hooksPath .githooks`

### Success Criteria

- `make format` formats all .c/.h files in-place
- Pre-commit hook rejects unformatted code with clear error message
- CI also runs format check (catches contributors who skip hooks — hooks are opt-in via `git config core.hooksPath .githooks`, CI is the real gate)

### Effort

~30 lines config + hook. 2 hours.

---

## Deliverable 4: API Documentation Generation

### What

Doxygen-generated HTML from existing header comments. Published to GitHub Pages or included in releases.

### Why

The public headers (`include/reflex_*.h`) have section comments and clear function names, but no searchable reference. A new developer shouldn't need to `grep` to find "how do I create a task?"

### Scope

| Header | Status | Action |
|--------|--------|--------|
| `reflex_hal.h` | Section headers, no @param | Add @param/@return to each function |
| `reflex_task.h` | Section headers | Add @param/@return |
| `reflex_kv.h` | Minimal | Add @param/@return |
| `reflex_radio.h` | Minimal | Add @param/@return |
| `reflex_crypto.h` | Minimal | Add @param/@return |
| `reflex_types.h` | Well-documented | No change needed |
| `goose.h` | Extensive | Minor cleanup |

### Implementation

```
docs/
  Doxyfile             # Doxygen configuration
  api/                 # Generated output (gitignored)
```

Makefile target: `make docs` → generates `docs/api/index.html`

### Success Criteria

- `make docs` generates browsable HTML API reference
- Every public function has: brief description, parameter names, return value
- CI publishes to GitHub Pages on release tags

### Effort

~100 lines of @param additions across 5 headers + Doxyfile. Half day.

---

## Deliverable 5: Platform Backend Guide

### What

A document (`docs/PLATFORM_BACKEND.md`) explaining how to add support for a new chip/board.

### Why

The project now supports two architectures (RISC-V, Xtensa). The abstraction layers are clean. But a contributor wanting to add ESP32-S3 or RP2040 support has no guide — they'd have to reverse-engineer the esp32c6 backend.

### Content

1. What a platform backend IS (implementation of reflex_hal.h + reflex_task.h + reflex_kv.h + reflex_radio.h + reflex_crypto.h)
2. File structure: `platform/<target>/`
3. CMakeLists.txt template with target guard
4. Which functions are mandatory vs optional (temp sensor is optional, GPIO is not)
5. How to add the platform to the top-level CMakeLists
6. How to create `sdkconfig.defaults.<target>`
7. Testing checklist (what to validate on first boot)
8. Example: walkthrough of the ESP32 backend (simplest case)

### Success Criteria

- A developer can add a new platform backend by following the guide
- The guide includes a complete checklist of files to create
- References working examples (esp32c6 for full-featured, esp32 for minimal)

### Effort

~200 lines. 2 hours.

---

## Deliverable 6: Debug Guide

### What

A document (`docs/DEBUGGING.md`) covering:
- How to read a Guru Meditation Error (panic decode)
- How to use `addr2line` with the ELF to find the crashing line
- How to enable verbose logging per module
- How to use the Python SDK for automated diagnostics
- Common crash signatures and their causes

### Why

When a board panics, the serial output shows a register dump and backtrace. Without knowing how to decode it, developers are stuck. This knowledge is currently tribal (learned by doing, not documented).

### Content

1. Reading a panic dump (what each line means)
2. Decoding the backtrace:
   ```bash
   riscv32-esp-elf-addr2line -e build/reflex_os.elf 0x4200XXXX
   ```
3. Common crashes:
   - Stack overflow (task stack too small)
   - NULL dereference (uninitialized pointer)
   - WDT timeout (task blocked too long)
   - Interrupt handler crash (wrong IRAM placement)
4. Using `idf.py monitor` with panic decode
5. USB-JTAG recovery (when board is unresponsive)
6. Enabling per-module log levels
7. Using SDK for health checks:
   ```python
   node = ReflexNode(port)
   print(node.heartbeat())  # Is LP core alive?
   print(node.services())   # Did all services start?
   ```

### Success Criteria

- A developer can decode a crash backtrace in under 2 minutes
- The guide covers the 5 most common crash types with exact symptoms and fixes
- Includes copy-paste commands for every tool

### Effort

~150 lines. 2 hours.

---

## Deliverable 7: Example Projects

### What

A directory `examples/` with 3-5 self-contained projects demonstrating common use cases. Each is a single file + README explaining what it does.

### Why

The User Manual describes concepts. Examples show working code. A developer learns fastest by modifying something that already works.

### Examples

| Name | What it demonstrates |
|------|---------------------|
| `examples/blink/` | Minimal: flash + control LED from Python SDK |
| `examples/mesh-temp/` | Two boards sharing temperature readings via atmosphere |
| `examples/purpose-demo/` | Set purpose, observe holon activation, trigger learning |
| `examples/multi-node/` | Discover all nodes, coordinate purposes, read aggregate state |
| `examples/custom-cell/` | Register a custom cell, wire a route, observe propagation |

Each example:
```
examples/blink/
  README.md       # What it does, how to run
  blink.py        # The script (Python SDK)
```

### Success Criteria

- Each example runs with `python examples/blink/blink.py` (no build step)
- Each takes < 30 seconds to demonstrate its point
- README includes expected output

### Effort

~50 lines per example × 5 = ~250 LOC. Half day.

---

## Deliverable 8: Developer Onboarding Checklist

### What

A section in CONTRIBUTING.md or standalone `docs/ONBOARDING.md` that walks a new developer from "I want to contribute" to "I submitted my first PR" in 10 steps.

### Content

1. Fork + clone
2. Install ESP-IDF (link to Espressif docs)
3. `make test` (verify host tests pass)
4. `idf.py build` (verify firmware builds)
5. Flash a board (if you have hardware)
6. Run the Python SDK against it
7. Read ARCHITECTURE.md (understand layers)
8. Pick an issue / area of interest
9. Make a change, run tests, format
10. Submit PR (CI validates automatically)

### Why

Reduces the barrier from "interested" to "productive" from days to hours.

### Effort

~80 lines. 1 hour.

---

## Priority & Dependencies

```
    ┌─────────────────┐
    │ D0: IDE + Config│ ← Zero effort, do first
    └────────┬────────┘
             │
    ┌────────┴────────┐
    │ D1: Host Tests  │ ← Foundation (D2 depends on this)
    └────────┬────────┘
             │
    ┌────────┼──────────────┐
    ▼        ▼              ▼
┌───────┐ ┌───────┐  ┌───────────┐
│D2: CI │ │D3: Fmt│  │D7: Example│ ← D7 has NO dependency on D4
└───┬───┘ └───────┘  └───────────┘
    │
    ▼
┌───────────┐  ┌───────────┐
│D4: APIDocs│  │D6: Debug  │ ← Independent of each other
└───────────┘  └───────────┘
                    │
         ┌──────────┼──────────┐
         ▼                     ▼
   ┌───────────┐         ┌─────────┐
   │D5:Platform│         │D8:Onbrd │ ← References all above
   └───────────┘         └─────────┘
```

### Recommended Order

0. **D0: IDE + Config** (3 lines, immediate value)
1. **D1: Host Tests** (enables D2, gates everything)
2. **D3: Code Formatting** (quick win, enforces quality immediately)
3. **D2: CI Pipeline** (automates D1 + D3, prevents regressions)
4. **D7: Examples** (immediate value for users AND developers — no dependency on D4)
5. **D4: API Docs** (reference for contributors)
6. **D6: Debug Guide** (unblocks self-service debugging)
7. **D5: Platform Guide** (unblocks community contributions)
8. **D8: Onboarding** (final polish, references everything above)

### Total Effort

| Deliverable | LOC | Time |
|-------------|-----|------|
| D0: IDE + Config | ~3 | 5 min |
| D1: Host Tests | ~700 | 1 day |
| D2: CI Pipeline | ~50 | 0.5 day |
| D3: Formatting | ~30 | 2 hours |
| D4: API Docs | ~100 | 0.5 day |
| D5: Platform Guide | ~200 | 2 hours |
| D6: Debug Guide | ~150 | 2 hours |
| D7: Examples | ~250 | 0.5 day |
| D8: Onboarding | ~80 | 1 hour |
| **Total** | **~1563** | **~4 days** |
