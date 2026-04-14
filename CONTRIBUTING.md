# Contributing

## Principles

- Keep changes small and deliberate.
- Prefer minimal correct fixes over broad rewrites.
- Keep ternary logic concentrated in `vm/` and explicit bridge points.
- Update docs when behavior, interfaces, or workflows change.

## Development Setup

1. Install ESP-IDF 5.5 or a compatible version.
2. Export the ESP-IDF environment:

```bash
source /Users/aaronjosserand-austin/Projects/esp-idf/export.sh
```

3. Build the firmware:

```bash
idf.py build
```

## Project Conventions

- Use ASCII by default unless a file already requires otherwise.
- Keep comments brief and only where the code is not immediately clear.
- Use `apply_patch`-style diffs for targeted code edits when collaborating through tooling.
- Do not commit generated build outputs, serial logs, or assembled binary artifacts.

## Validation

Before submitting changes:

1. Rebuild with `idf.py build`.
2. Reassemble any changed `.tasm` programs with `python3 tasm.py`.
3. If runtime or hardware paths changed, flash the board and verify the affected shell and device behavior.
4. Keep implementation docs in sync with the actual runtime contract.

## Commit Style

- Use concise imperative commit messages.
- Match existing repo style such as:
  - `Add ternary fabric and VM memory services`
  - `Remediate supervisor runtime and loader flow`
  - `Refresh docs and repo hygiene`
