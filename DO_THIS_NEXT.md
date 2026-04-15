# Do This Next

## Core Product

1. Implement a real OTA update service with rollback handling.
2. Expand from a single background VM task to a true multi-VM execution model.
3. Expose more hardware through the ternary fabric, especially I2C, SPI, and timers.
4. Improve VM observability with tracing, fault reporting, and fabric inspection tools.

## Runtime Hardening

1. Add automated tests for:
   - loader CRC validation
   - packed data loading
   - `TSEND` / `TRECV`
   - `TSEL`
   - MMU bounds enforcement
   - cache coherency
2. Re-audit VM ownership and lifecycle paths around load, unload, and reload.
3. Red-team malformed images, queue saturation, repeated task start/stop, and boot recovery edge cases.
4. Revalidate the literal onboard button -> fabric -> supervisor -> LED path after remediation.

## Tooling

1. Extend `tasm.py` with better diagnostics and multi-word literal/data support.
2. Add a repeatable host-side smoke test for build, flash, and serial validation.
3. Create a cleaner workflow for generating shell-friendly hex payloads.

## Repo / Project

1. Add CI for ESP-IDF builds and assembler validation.
2. Add repo process files only if desired, such as issue templates.
3. Tighten architecture docs around module boundaries and known limitations.

## Important Remaining Validation

1. The supervisor path is proven on-device through injected fabric traffic.
2. The most important remaining hardware proof is the full physical onboard button -> fabric -> supervisor -> LED loop after remediation.
