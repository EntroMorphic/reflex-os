# Synthesis: GOOSE Phase 1

## Position

GOOSE is now best treated as a hardware-near geometric ontology project rather than an immediate ISA design exercise.

The C6 should be approached as a field of stateful regions whose accesses are numerically expressed but operationally routed. Bonsai's job is to define the minimal ontology that makes this view precise and testable.

## Immediate Deliverable

Produce a first Bonsai morphology document that defines these terms:

1. `field`
2. `region`
3. `cell`
4. `state`
5. `transition`
6. `route`

Each term should include:

- a concise definition
- how it maps onto C6 hardware reality
- what ternary meaning it admits
- what it is not

## Execution Plan

1. Write `bonsai/MORPHOLOGY.md` defining the six primitive terms.
2. Classify one real C6 subsystem using that morphology.
   Recommended subsystem: GPIO + IO mux + interrupt edge state.
3. Define one falsifiable hardware experiment entirely in morphology terms.
4. Only after that, draft a candidate executable notation for routed state transitions.

## Why This Path

- It keeps Bonsai close to the hardware.
- It avoids prematurely importing CPU/VM abstractions.
- It gives GOOSE a chance to become testable before it becomes elaborate.
- It preserves Reflex OS as a proving environment without letting existing implementation categories dominate the new paradigm.

## Success Criteria

- [ ] The six primitive terms can be defined without circular language.
- [ ] At least one C6 subsystem can be described clearly using the morphology.
- [ ] A hardware experiment can be expressed without relying on services, tasks, or driver-level mental models.
- [ ] The resulting model feels simpler and truer than an equivalent binary-software description.

## Current Best One-Sentence Summary

GOOSE should emerge by formalizing the C6 as a ternary-readable field of routed state transitions, not by designing a new instruction set before the ontology exists.
