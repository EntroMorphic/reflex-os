# Reflex OS — Documentation

Canonical design and status documents.

## Getting started

- [`GETTING_STARTED.md`](GETTING_STARTED.md) — Zero to running firmware: hardware, toolchain, build, flash, shell, SDK.
- [`ONBOARDING.md`](ONBOARDING.md) — Contributor onboarding (fork → first PR).
- [`DEBUGGING.md`](DEBUGGING.md) — Panic decoding, crash types, recovery.
- [`PLATFORM_BACKEND.md`](PLATFORM_BACKEND.md) — Porting to new chips.

## Architecture and substrate

- [`architecture.md`](architecture.md) — Module layout and the GOOSE substrate (cells, routes, fields, supervisor).
- [`ternary-architecture.md`](ternary-architecture.md) — Background on the ternary data model (trits, trytes, word18).
- [`soft-opcodes.md`](soft-opcodes.md) — High-level soft-opcode surface.

## Strategy and status

- [`implementation-status.md`](implementation-status.md) — What is built, what's validated on hardware, what limits are known.
- [`strategy.md`](strategy.md) — The Chronicler's Path: gaps, advantages, next phases.
- [`potentials.md`](potentials.md) — Realized milestones and the biological-frontier roadmap.
- [`prd.md`](prd.md) — Original product requirements.

## Ternary VM internals

See [`vm/`](vm/):

- [`vm/cache.md`](vm/cache.md) — Three-state MESI-lite soft cache.
- [`vm/loader.md`](vm/loader.md) — Original packed-image loader.
- [`vm/loader-v2.md`](vm/loader-v2.md) — V2 loader with CRC32 verification and packed data segments.
- [`vm/state.md`](vm/state.md) — VM register file, flags, and IP semantics.
- [`vm/syscalls.md`](vm/syscalls.md) — Syscall dispatch from ternary code into host services.

## Design documents

- [`design-self-expanding-perception.md`](design-self-expanding-perception.md) — Phase 33 original architecture (pain-driven).
- [`design-mmio-sync.md`](design-mmio-sync.md) — MMIO sync design for distributed atlas.
- [`design-metabolic-regulation.md`](design-metabolic-regulation.md) — Phase 31 initial design.
- [`lmm-curiosity-attractor.md`](lmm-curiosity-attractor.md) — LMM: reframe from pain to curiosity attractor.
- [`lmm-self-expanding-perception.md`](lmm-self-expanding-perception.md) — LMM: original exploration design analysis.
- [`lmm-metabolic-regulation.md`](lmm-metabolic-regulation.md) — LMM: two-layer metabolic architecture.

## Languages and tooling

- [`tasm-spec.md`](tasm-spec.md) — Ternary Assembler syntax and GOOSE-native opcodes.
- Assembler: [`../tools/tasm.py`](../tools/tasm.py)
- LoomScript compiler: [`../tools/loomc.py`](../tools/loomc.py)
- Atlas SVD scraper: [`../tools/goose_scraper.py`](../tools/goose_scraper.py)

## Root-level files

These live at the repo root per GitHub conventions:

- [`../README.md`](../README.md) — project overview and build/flash quickstart
- [`../SECURITY.md`](../SECURITY.md) — Sanctuary Guard, Authority Sentry, Aura protocol
- [`../CONTRIBUTING.md`](../CONTRIBUTING.md) — how to contribute
- [`../CHANGELOG.md`](../CHANGELOG.md) — release notes
- [`../LICENSE`](../LICENSE)
