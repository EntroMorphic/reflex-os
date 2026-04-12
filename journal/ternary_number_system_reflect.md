# Reflections: Ternary Number System Structure

## Core Insight

The ternary number system should be structured as a three-layer contract: balanced ternary semantics, an execution-friendly VM word model, and a separate storage encoding optimized for simplicity over maximal density.

## Resolved Tensions

- Node 3 vs Node 4 -> Resolution: do not force one representation to solve both storage and execution. Use a packed external format and a normalized execution form.
- Node 8 vs Node 9 -> Resolution: keep register inspection human-friendly while allowing packed storage to reserve invalid bit patterns for validation.

## Challenged Assumptions

- Assumption: the most mathematically pure packing is the best choice.
  Challenge: a ternary OS on a binary MCU succeeds through explicit boundaries, not purity at every layer.

- Assumption: word size should be chosen for aesthetic symmetry alone.
  Challenge: the word size must also reduce implementation friction on a 32-bit host.

## What I Now Understand

There are really two wrong moves here.

The first wrong move is fake ternary: treating ordinary integers as if that alone creates a ternary machine. That would collapse the whole concept into notation and lose the architectural point of Reflex OS.

The second wrong move is overcommitting to dense base-3 packing too early. That would create a lot of decode friction before the VM semantics are even stable.

The cleaner structure is:

1. A logical trit model based on balanced ternary.
2. A VM word model chosen for ergonomics and host fit.
3. A packed binary transport/storage format chosen for easy indexing, validation, and decode.

From that structure, the concrete recommendation becomes clearer.

- The logical unit is a `trit` with values `-1`, `0`, and `+1`.
- The human-scale aggregate unit should be `9 trits`.
- The VM execution word should be `18 trits`.
- Registers should expose normalized ternary words so shell tooling can inspect them directly.
- Packed storage should prefer decode simplicity. The best MVP choice is 2 bits per trit with one reserved invalid code.

This wastes some storage versus a tighter base-3 packing, but it buys simpler addressing, easier corruption checks, faster decode, and less fragile interpreter code. That trade is correct for MVP.

## Remaining Questions

- Should the normalized execution representation be an array of trits or a host integer plus conversion helpers at the ALU boundary?
- Should instruction words and data words both use 18 trits, or should instructions use a denser custom format later?
