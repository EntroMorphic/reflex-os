# PRD: Publish reflex-os to PyPI

## Goal

`pip install reflex-os` installs the Python SDK from anywhere, no local clone required.

## Current State

- `sdk/python/` builds as `reflex_os-0.1.0` wheel via setuptools.
- Distribution is local-only (editable install or wheel from disk).

## Prerequisites

- PyPI account registered under `tripp@anjaustin.com`.
- API token scoped to the `reflex-os` project (store in `~/.pypirc` or env var).
- `twine` and `build` installed in a working venv.

## Steps

1. Ensure `pyproject.toml` metadata is complete (author, description, license, URLs).
2. Build: `python -m build` from `sdk/python/`.
3. Upload: `twine upload dist/reflex_os-0.1.0*`.
4. Verify: in a fresh venv, `pip install reflex-os` succeeds.
5. Update `README.md` and `docs/USER_MANUAL.md` to include `pip install reflex-os`.

## Test Plan

```bash
python -m venv /tmp/test-reflex && source /tmp/test-reflex/bin/activate
pip install reflex-os
python -c "import reflex"
reflex-cli --help
deactivate && rm -rf /tmp/test-reflex
```

## Non-Goals

- No CI/CD auto-publish on tag (future work).
- No conda-forge recipe.
- No namespace package changes.

## Effort

~15 minutes once PyPI credentials exist.
