# Synthesis: Ternary Number System Structure

## Recommendation

Reflex OS should structure its ternary number system in three explicit layers.

### 1. Semantic Layer

Use balanced ternary as the only logical number system for the VM.

- `trit = {-1, 0, +1}`
- comparisons and branch semantics should be defined in terms of negative, neutral, and positive outcomes

### 2. Execution Layer

Use `18 trits` as the primary VM word and `9 trits` as the subunit.

Recommended units:

- `trit` = smallest logical value
- `tryte9` = 9 trits
- `word18` = 18 trits

Why:

- 9 trits is small enough to reason about and large enough to be useful
- 18 trits gives the VM a real word size without pushing too hard against a 32-bit host
- 18 trits pair naturally as two 9-trit halves for future instruction and register design

Execution representation should be normalized and inspection-friendly.

Recommended MVP shape:

- registers store normalized `word18` values
- shell and debug tools display registers in ternary form
- ALU helpers operate on VM word types, not on raw C integers scattered across the codebase

### 3. Storage Layer

Use a simple packed binary encoding with `2 bits per trit`.

Recommended code map:

- `00` -> `-1`
- `01` -> `0`
- `10` -> `+1`
- `11` -> invalid/reserved

Why:

- constant-time indexing by trit position
- simple encode and decode helpers
- easy validation of malformed program data
- less fragile than denser base-3 byte packing for MVP

This is intentionally not the densest possible ternary storage format. That is acceptable because MVP needs clarity, speed of implementation, and debuggability more than maximum packing efficiency.

## Concrete Spec Direction

### Core Types

- `trit_t` logical trit type
- `tryte9_t` 9-trit aggregate type
- `word18_t` 18-trit aggregate type
- `packed_trits_t` byte-backed packed storage view

### Representation Rules

- semantic APIs expose trits and ternary words
- packing APIs are isolated in VM encoding modules
- host code does not invent alternate ternary conventions
- reserved invalid packed values must trigger decode errors

### ALU Rules

- arithmetic helpers normalize all outputs back into valid balanced ternary form
- compare returns ternary outcomes directly
- branch opcodes consume ternary compare outcomes without converting them into fake boolean-only logic too early

## Action Plan

1. Define `trit_t`, `tryte9_t`, and `word18_t` in `vm/`.
2. Write encode/decode helpers for 2-bit packed trits.
3. Add tests for round-trip conversion and invalid packed values.
4. Implement comparison, add, subtract, and select on `word18_t`.
5. Base the first soft opcode set on `word18_t` and ternary compare results.

## Success Criteria

- [ ] Balanced ternary is the only VM semantic model
- [ ] `word18` is defined as the primary execution word
- [ ] packed storage uses one documented encoding scheme
- [ ] encode/decode logic is centralized and testable
- [ ] invalid packed values are detectable
- [ ] VM arithmetic does not rely on ad hoc host integer shortcuts

## Decision Summary

The MVP number system for Reflex OS should be:

- balanced ternary semantics
- 9-trit subunits
- 18-trit execution words
- 2-bit-per-trit packed storage with one reserved invalid state

This gives the project a real ternary identity without making the VM brittle or over-optimized too early.
