# Experiment 002: Rhythm Field

## Purpose

Prove that GOOSE can compose an internal rhythm field with a visible agency field without requiring human input.

This experiment removes the user from the loop and tests whether a ternary phase can emerge from timed progression and route into persistent output meaning.

## Claim Under Test

An internal timing source can drive a ternary phase cell through:

- negative
- neutral
- positive

and route those phases into LED behavior where the neutral state acts as a real `hold`, not a null placeholder.

## Hardware Scope

- timing source via system time / scheduled cadence
- one LED output pin
- direct shell-controlled task

## Chosen Cells

1. **Rhythm cell**
   - advances on a fixed cadence
2. **Phase cell**
   - cycles through `falling`, `unchanged`, `rising`
3. **Agency cell**
   - drives LED output consequence

## Ternary Mapping

Phase interpretation:

- `falling` -> LED low
- `unchanged` -> hold prior LED state
- `rising` -> LED high

This is the central point of the experiment.

The neutral state is not ignored. It preserves consequence.

## Current Implementation

Shell command:

```text
bonsai exp2 <start|stop|status>
```

Implementation behavior:

- runs an autonomous task
- advances the phase every `400 ms`
- cycles:
  - `falling -> unchanged -> rising -> falling -> ...`
- drives the LED directly:
  - `falling` sets LED off
  - `unchanged` leaves LED as-is
  - `rising` sets LED on

Tracked state:

- current phase
- phase step count
- most recent tick timestamp
- current LED state

## On-Device Validation

Validated on hardware by repeated serial polling.

Observed sequence:

- `phase=rising ... led=on`
- `phase=falling ... led=off`
- `phase=unchanged ... led=off`
- `phase=rising ... led=on`
- `phase=falling ... led=off`
- `phase=unchanged ... led=off`

This proves:

1. the internal phase is advancing autonomously
2. all three ternary states are being inhabited on device
3. the neutral state has operative meaning as `hold`
4. agency can be composed from rhythm without human interaction

## Why This Matters

Experiment 001 proved an atomic cell-transition-route loop with human-driven input.

Experiment 002 proves autonomous composition:

- rhythm
- phase
- agency

This is the first no-human-in-the-loop GOOSE proof.

## Success Result

GOOSE survives a second proof under stricter conditions:

- no manual button press
- no VM involvement
- no fabric/service indirection
- real ternary phase progression on device

## Natural Next Step

The next step-change should likely be regional composition:

- combine an external cell field and an internal rhythm field
- or combine multiple internal fields with routing policy

That would move GOOSE from autonomous ternary cadence into multi-field interaction.
