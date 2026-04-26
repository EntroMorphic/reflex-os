# Hebbian Learning

Reference for the reward-gated Hebbian plasticity in `goose_supervisor.c`.

## Signal Cells

| Cell             | Value | Effect                        |
|------------------|-------|-------------------------------|
| `sys.ai.reward`  | +1    | Triggers co-activation learning |
| `sys.ai.pain`    | -1    | Triggers counter decay        |
| `sys.purpose`    | != 0  | Doubles reward increments     |

## Learning Cycle

1. `goose_supervisor_learn_sync()` runs at 1 Hz (every 10 supervisor pulses).
2. If `sys.ai.reward == +1`, each supervised route is evaluated:
   - Source and sink both non-zero and same sign: counter increments by +1.
   - Source and sink disagree: counter decrements by -1.
   - Increment is multiplied by `purpose_multiplier` (see below).
3. When counter reaches `+HEBBIAN_COMMIT_THRESHOLD`: `learned_orientation = +1`, counter resets to 0.
4. When counter reaches `-HEBBIAN_COMMIT_THRESHOLD`: `learned_orientation = -1`, counter resets to 0.
5. After processing, `sys.ai.reward` is cleared to 0.

## Constants

| Name                       | Value | Notes                                |
|----------------------------|-------|--------------------------------------|
| `HEBBIAN_COMMIT_THRESHOLD` | 8     | Counter value that triggers commit   |
| `HEBBIAN_COUNTER_MAX`      | 16    | Hard clamp (2x threshold)            |
| `purpose_multiplier`       | 2     | Applied when `sys.purpose != 0`      |

## What a Commit Means

When the counter crosses the threshold, `learned_orientation` is written to the
route. This acts as a permanent route sign: the route will use the learned
orientation in place of (or in addition to) its static orientation during
transition processing. Learned orientation also earns a `PULSE_HEBBIAN_BOOST`
(+2) to the field's scheduling priority.

## Pain and Decay

When `sys.ai.pain == -1`, all hebbian counters decay by 1 toward zero per tick.
Uncommitted learning (counter only) is gradually erased. Committed learning
(`learned_orientation`) is preserved -- pain cannot undo a commit.

## Snapshots

`goose_snapshot_save()` and `goose_snapshot_load()` persist learned state to NVS
under the `goose` namespace. Per-field blobs store each route's
`learned_orientation` and `hebbian_counter`, keyed by FNV-1a hash of the field
name. Routes are matched by their 16-byte fixed-width name on restore.

Related: `goose_snapshot_clear()` erases all saved plasticity data.

## Autonomous Reward

The supervisor automatically evaluates purpose fulfillment at 1Hz via
`goose_supervisor_evaluate()`. No manual reward signal is needed.

**Reward:** When purpose-relevant routes converge (source and sink states
match), `reward_score` increments. If `reward_score >= REFLEX_AUTO_REWARD_THRESHOLD`
(default 2), `sys.ai.reward` is set to +1.

**Pain:** When a `HARDWARE_OUT` cell disagrees with its expected state for
`REFLEX_AUTO_PAIN_STUCK_TICKS` (default 5) consecutive evaluations,
`sys.ai.pain` is set to -1.

The evaluate pass runs before the learning pass in the supervisor pulse,
so reward written in tick N is consumed by `learn_sync` in the same tick.

## How to Observe

- `snapshot save` then inspect NVS keys matching `s_*` in the `goose` namespace.
- Set a purpose (`purpose set <name>`) and watch counters advance automatically
  as routes converge — no manual reward needed.
- Counter values are visible in snapshot blobs (offset: 16-byte name + 1-byte
  orientation, then 2-byte little-endian counter per route).
- Enable telemetry (`telemetry on`) to watch `#T:H` (Hebbian updates) and
  `#T:V` (reward/pain evaluation) events in real time.

## Exploration and the Three-Layer Model

Hebbian learning is the middle layer of a three-layer system that governs how the OS discovers and retains new hardware senses (Phase 33):

1. **Curiosity** (exploration) — finds what's alive. The supervisor probes HARDWARE_IN registers from the shadow atlas, reading them 1 second apart. Registers whose values change are *hot* and get paged into the Loom. This layer provides raw material.
2. **Learning** (Hebbian) — finds what's relevant. Routes from exploration cells that correlate with purpose activity accumulate Hebbian counter increments and eventually commit `learned_orientation`. Timer registers (hot but uncorrelated) oscillate around zero and never commit.
3. **Forgetting** (eviction) — discards what's not. Exploration cells are VIRTUAL — unreinforced cells stay eviction-eligible and are reclaimed by round-robin when Loom pressure rises.

Each layer does one job. Curiosity doesn't judge relevance. Learning doesn't seek novelty. Eviction doesn't evaluate. The separation keeps each mechanism simple and composable.
