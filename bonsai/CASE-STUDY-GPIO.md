# Case Study: GPIO as GOOSE Morphology

This document applies the first GOOSE morphology to one real ESP32-C6 subsystem:

- GPIO
- IO mux
- per-pin interrupt edge configuration
- interrupt status latches

The purpose is to ground Bonsai in actual hardware without beginning from drivers, services, or pre-existing OS abstractions.

## Why GPIO First

GPIO is a good first case study because it contains, in compact form, nearly everything GOOSE cares about:

- observable state
- writable state
- edge transitions
- routing through muxes
- attention via interrupt status
- external coupling to the world

It is a small but ontologically rich subsystem.

## Hardware Surfaces Involved

The relevant C6 register surfaces include:

- `GPIO_IN_REG`
- `GPIO_OUT_REG`
- `GPIO_ENABLE_REG`
- `GPIO_STATUS_REG`
- `GPIO_STATUS_W1TC_REG`
- `GPIO_PINn_REG` per-pin configuration, especially interrupt type and interrupt enable
- `IO_MUX_GPIOn_REG` per-pin mux and input-enable configuration
- `GPIO_FUNCx_IN_SEL_CFG_REG` and `GPIO_FUNCx_OUT_SEL_CFG_REG` for matrix routing

This is already enough to see the subsystem as a field rather than as a pin API.

## Morphological Mapping

### Field

The field is the GPIO control field within the broader MMIO state field.

It also touches neighboring fields:

- IO mux field
- interrupt-attention field
- external physical signal field

### Region

Relevant regions within the GPIO-related field:

1. Input sampling region
2. Output assertion region
3. Output enable region
4. Per-pin configuration region
5. Interrupt status region
6. IO mux region
7. Signal matrix region

These regions are distinct but coupled.

### Cell

A useful first GPIO cell is not "the entire pin" in the abstract.

A better cell decomposition is:

1. input-state cell
2. output-state cell
3. output-enable cell
4. interrupt-type cell
5. interrupt-latch cell
6. mux-selection cell

This is important.

A pin is not one cell. It is a local cluster of related cells.

## State Forms

### Input-State Cell

At the binary host level, input is sampled as low or high.

At the GOOSE level, the more useful ternary state is:

- negative: falling orientation
- neutral: no meaningful change
- positive: rising orientation

This means the ontologically interesting state is not raw level alone, but level in relation to prior sampled condition.

### Output-State Cell

Possible ternary interpretations:

- drive low
- hold
- drive high

This is more meaningful than on/off if the middle state is treated as explicit non-assertion or preservation.

### Output-Enable Cell

Possible ternary interpretation:

- inhibit
- latent
- expose

This distinguishes disabled output, inert retention, and active drive participation.

### Interrupt-Type Cell

The host encodes interrupt behavior numerically.

GOOSE should treat it as a routing policy cell:

- react to negative transition
- ignore / hold neutral
- react to positive transition

Later, richer multi-valued mappings may exist, but the first useful geometric interpretation is oriented edge attention.

### Interrupt-Latch Cell

Possible ternary interpretation:

- cleared
- quiescent
- asserted

This is one of the first clear examples of attention as state.

### Mux-Selection Cell

Possible ternary interpretation at the simplest level:

- detached
- held in GPIO identity
- attached to alternate function

For Bonsai, mux selection is not only configuration. It is route commitment.

## Transitions

The key GPIO transitions are:

1. external level change at the pin
2. sampled input change in `GPIO_IN_REG`
3. interrupt-type-configured recognition of edge orientation
4. interrupt latch assertion into `GPIO_STATUS_REG`
5. explicit clearing through `GPIO_STATUS_W1TC_REG`
6. output assertion changes through `GPIO_OUT_REG`
7. output participation changes through `GPIO_ENABLE_REG`
8. route changes through IO mux or signal matrix configuration

These transitions form a real hardware narrative:

- world changes
- field samples
- policy interprets
- attention latches
- effect may be routed onward

## Routes

GPIO reveals several different route classes.

### Route 1: External-to-Sampled

Physical pin condition is routed into the input state surface.

### Route 2: Sampled-to-Attention

Per-pin interrupt configuration routes certain transitions into interrupt-latch assertion.

### Route 3: MMIO-to-Effect

CPU writes route intent into output or configuration state.

### Route 4: Mux-to-Identity

IO mux configuration routes whether a pad participates as GPIO or some alternate function.

### Route 5: Matrix-to-Internal Function

The signal matrix routes internal function signals into or out of pin-related surfaces.

This matters because route is not one thing. It is a family of lawful influence paths.

## First Important Insight

GPIO on the C6 is not best understood as a pin with settings.

It is better understood as a local geometry of related cells whose states and transitions are coupled through route surfaces.

That is much closer to GOOSE.

## First Minimal GOOSE Cell Candidate

The strongest first candidate is the **edge-sensitive input attention cell**.

It spans:

- sampled input condition
- prior condition or effective history
- edge interpretation
- interrupt-latch assertion

Why this cell matters:

- it contains perception
- it contains transition
- it contains routing into attention
- it is externally falsifiable on hardware

## What This Case Study Changes

This case study suggests a refinement to the morphology:

- a region may contain not just cells, but **cell clusters**
- a hardware feature like a pin is often a cluster, not a single cell

That is useful pressure from reality.

## Next Step

The next artifact should define the first hardware experiment purely in these terms:

- identify one pin cluster
- define the cell of interest
- define admissible ternary state readings
- define admissible transitions
- define one route into visible consequence

That experiment should be simple enough to fail clearly if GOOSE is not actually buying us anything.
