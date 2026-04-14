# Experiment 001: Edge Attention Cell

## Purpose

Test whether a real ESP32-C6 hardware behavior can be described and driven more truthfully through GOOSE's cell-state-transition-route model than through inherited binary event logic.

This is not yet a full GOOSE runtime.

It is a falsifiable hardware-near experiment.

## Claim Under Test

A GPIO input edge can be treated as a ternary state transition in a cell cluster, and routed into a visible consequence without beginning from driver/service mental models.

## Hardware Scope

- one input pin
- one output pin or LED
- GPIO region
- IO mux region if needed
- interrupt status / edge attention surfaces if needed

## Chosen Cell

The first cell of interest is the **edge attention cell**.

This is the smallest meaningful unit formed by:

- sampled input condition
- effective previous condition
- oriented transition interpretation
- latched attention consequence

## Ternary State Model

Interpret the cell's present meaning as:

- negative: falling
- neutral: unchanged
- positive: rising

This is intentionally geometric and relational.

It does not treat the raw pin level as the main truth.

## Visible Consequence

Route the ternary transition into output meaning:

- negative -> drive low
- neutral -> hold
- positive -> drive high

This is the simplest visible route from ternary transition into external effect.

## Minimal Procedure

1. Configure one GPIO input path.
2. Sample the input repeatedly at a controlled cadence.
3. Derive the ternary transition from previous and current sample.
4. Route that transition directly into the output-state cell.
5. Observe whether the output behavior is intelligible and stable.

## Current Implementation

The first live implementation is integrated into the running Reflex OS image as a shell-controlled direct GPIO experiment.

Shell command:

```text
bonsai exp1 <start|stop|status>
```

Behavior:

- samples `REFLEX_BUTTON_PIN` directly with `gpio_get_level()`
- derives the ternary transition by comparing the current and previous sample
- maps transitions as:
  - `falling -> led low`
  - `unchanged -> hold`
  - `rising -> led high`
- updates the LED directly with `reflex_led_set()`
- does not use the fabric, services, or VM path for the experiment logic

Sampling cadence:

- `25 ms`

Tracked runtime counters:

- rising edge count
- falling edge count
- stable sample count
- current input level
- most recent interpreted edge

## Why This Is a Good Falsifier

This experiment fails cleanly if any of the following become true:

1. The neutral state is meaningless.
2. The routed ternary model is more contrived than ordinary binary edge handling.
3. The hardware cannot be described cleanly in cell/transition/route terms.
4. The model immediately collapses back into conventional imperative event code.

## Success Criteria

- The input behavior can be expressed clearly as a ternary transition cell.
- The output consequence can be expressed clearly as a route.
- The neutral state carries real meaning.
- The explanation of the experiment feels simpler than a normal GPIO-event narrative.

## Current Validation Status

Validated on device:

- experiment shell command is present
- task starts and stops cleanly
- direct GPIO sampling remains stable in the neutral case
- stable sample counts increase as expected while the button is untouched

Still required:

- human-driven physical button press and release to verify the `rising` and `falling` routes on hardware

## What Comes Next If It Works

If Experiment 001 works, then the next step is:

- add rhythm as a second field
- route edge attention through timing modulation
- test whether two interacting fields compose naturally under GOOSE

That would move Bonsai from a single cell cluster into the first small geometry of interacting state fields.
