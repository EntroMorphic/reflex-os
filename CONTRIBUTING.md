# Contributing

## Principles

- Keep changes small and deliberate.
- Prefer minimal correct fixes over broad rewrites.
- Keep ternary logic concentrated in `vm/` and explicit bridge points.
- Update docs when behavior, interfaces, or workflows change.

## Development Setup

1. Install [ESP-IDF v5.5](https://docs.espressif.com/projects/esp-idf/en/v5.5/esp32c6/get-started/).
2. Install `clang-format` for formatting checks. `pip install clang-format` is
   preferred over the system package: it also provides `git-clang-format`,
   which `make format-diff` needs.
3. Export the ESP-IDF environment. The path depends on where you
   installed ESP-IDF; typical locations are `~/esp-idf/export.sh` or
   `~/Projects/esp-idf/export.sh`:

```bash
source /path/to/esp-idf/export.sh
```

4. Build the firmware:

```bash
idf.py set-target esp32c6   # first time only
idf.py build
```

5. Run host tests (no hardware needed):

```bash
make test                    # C host tests — no hardware needed
make tasm-test               # TASM compiler tests
```

With a board attached, the shell can be validated end to end. This is
non-destructive: it never provisions or clears an Aura key, never reboots, and
restores the session role, vitals overrides and purpose.

```bash
make hw-test PORT=/dev/cu.usbmodem1101
```

## Project Conventions

- Use ASCII by default unless a file already requires otherwise.
- Keep comments brief and only where the code is not immediately clear.
- Use `apply_patch`-style diffs for targeted code edits when collaborating through tooling.
- Do not commit generated build outputs, serial logs, or assembled binary artifacts.

## Validation

Run everything that does not need a board with one target:

```bash
make verify
```

That is host tests, TASM tests, doc links, the warning gate, the loom lock
check, workflow schema validation, and a **real ESP-IDF build** in the same
container image CI uses — no local ESP-IDF install required, only Docker.

Renaming a CI job counts as a change that needs checking: GitHub rejects a
workflow whose `needs:` names a job that no longer exists, and it rejects it at
parse time, so *no jobs run at all* and the failure reports only as "a workflow
file issue" without naming the dangling reference. `make ci-lint` names it.

### A syntax check is not a build

`gcc -fsyntax-only` generates no code, so it runs none of GCC's
flow-sensitive analyses. A change was once pushed on the strength of a clean
`-fsyntax-only`, a green host suite, green TASM tests and green doc links, and
it broke all four ESP-IDF builds on a `-Werror=format-truncation` diagnostic.

`make warn-check` compiles the ESP-independent two thirds of the firmware for
real, at `-O2`, with `-Wall -Wextra -Werror` — stricter than ESP-IDF's own
flags, and it has caught real defects. But it is **not** a substitute: that
truncation diagnostic is produced by the RISC-V toolchain and is missed by host
GCC 11 through 14, at every optimisation level. Measured, not assumed.

**`make idf-build` is the only local check that speaks for the firmware
build.** Run it before pushing anything that compiles into the image.

### The individual gates

| Command | Checks |
|---|---|
| `make test` | C host suite (no hardware) |
| `make tasm-test` | TASM compiler |
| `make doc-links` | Relative links across all tracked Markdown |
| `make warn-check` | ESP-independent firmware at `-Wall -Wextra -Werror` |
| `make lock-check` | No telemetry emission while the loom lock is held |
| `make format-diff` | Formatting of the lines your change touches |
| `make ci-lint` | Workflow file schema (catches dangling `needs:` after a job rename) |
| `make soc-check` | Generated SoC header is current with the SVD and mapping |
| `make soc-bridge` | Every generated SoC constant equals the ESP-IDF macro it replaced |
| `make rom-check` | ROM entry-point addresses match ESP-IDF's `esp32c6.rom.ld` |
| `make idf-build` | Real ESP-IDF build (Docker) |
| `make hw-test PORT=…` | Shell contract against a flashed board |

`make format-check` reports formatting tree-wide and currently fails: the
repository predates any enforced style. CI gates `format-diff` instead, so new
lines converge without a wholesale reformat that would bury `git blame`.

With a board attached, also run `make hw-test PORT=…` if you touched runtime
or shell paths, and keep implementation docs in sync with the actual runtime
contract.

## Commit Style

- Use concise imperative commit messages.
- Match existing repo style such as:
  - `Add ternary fabric and VM memory services`
  - `Remediate supervisor runtime and loader flow`
  - `Refresh docs and repo hygiene`
