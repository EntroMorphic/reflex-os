# Reflex OS — Documentation

Canonical design and status documents.

## Getting started

- [`GETTING_STARTED.md`](GETTING_STARTED.md) — Zero to running firmware: hardware, toolchain, build, flash, shell, SDK.
- [`USER_MANUAL.md`](USER_MANUAL.md) — The full operator's manual: every shell command, the purpose system, mesh, learning, holons, and the Python SDK.
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
- [`independence-dependency-map.md`](independence-dependency-map.md) — What ESP-IDF is actually needed for, measured from the linked image, and the order those needs can be retired.

## Plans not yet executed

Open roadmaps: work nothing has superseded, kept where it can be found rather
than rediscovered. Retired plans move to [`archive/`](archive/).

- [`cortexm_port.md`](cortexm_port.md) — Scoping for the first non-Espressif target (STM32L4A6). The portability proof that turns "multi-architecture behind a HAL" into a demonstrated claim.

## Archive

- [`archive/`](archive/) — Documents whose work is finished, kept for provenance. History, not status.

## Audits

- [`P0-07SEP26.md`](P0-07SEP26.md) — Current. Priority-0 audit: the VM loader's 16-bit syscall check, LoomScript wire validation, an unlocked route mutation, and a format gate that could not fail. Every finding is now closed.
- [`audit-2026-09-04.md`](audit-2026-09-04.md) — Console I/O under the loom lock, a policy guard reporting success, the crypto suite that tested determinism rather than correctness, and the verification gates that came out of it.
- [`audit-2026-08-12.md`](audit-2026-08-12.md) — Defects ranked by severity × reachability, prior-audit closure status, documentation drift.
- [`audit-2026-04-16.md`](audit-2026-04-16.md) — First full assessment (post-v2.6.0): novelty, execution quality, and fluff.

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
- [`design-role-based-access.md`](design-role-based-access.md) — RBA original design (PIN-based).
- [`lmm-role-based-access.md`](lmm-role-based-access.md) — LMM: capability-based reframe (voluntary roles, deferred PINs).

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
