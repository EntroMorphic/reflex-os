# Contributing

## Principles

- Keep changes small and deliberate.
- Prefer minimal correct fixes over broad rewrites.
- Keep ternary logic concentrated in `vm/` and explicit bridge points.
- Update docs when behavior, interfaces, or workflows change.

## Development Setup

1. Install [ESP-IDF v5.5](https://docs.espressif.com/projects/esp-idf/en/v5.5/esp32c6/get-started/).
2. Install `clang-format` for code formatting checks (`brew install clang-format` on macOS, `apt install clang-format` on Linux).
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
make test                    # C host tests (26 tests)
make tasm-test               # TASM compiler tests (9 tests)
```

## Project Conventions

- Use ASCII by default unless a file already requires otherwise.
- Keep comments brief and only where the code is not immediately clear.
- Use `apply_patch`-style diffs for targeted code edits when collaborating through tooling.
- Do not commit generated build outputs, serial logs, or assembled binary artifacts.

## Validation

Before submitting changes:

1. Rebuild with `idf.py build`.
2. Reassemble any changed `.tasm` programs with `python3 tools/tasm.py`.
3. If runtime or hardware paths changed, flash the board and verify the affected shell and device behavior.
4. Keep implementation docs in sync with the actual runtime contract.

## Commit Style

- Use concise imperative commit messages.
- Match existing repo style such as:
  - `Add ternary fabric and VM memory services`
  - `Remediate supervisor runtime and loader flow`
  - `Refresh docs and repo hygiene`
