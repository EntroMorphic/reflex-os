# PRD: Autonomous Reward / Pain Generation

## Problem

The Hebbian learning system (`goose_supervisor_learn_sync`) works but requires
an operator to manually set `sys.ai.reward = +1` or `sys.ai.pain = -1` via the
shell. Without manual input the hebbian_counter never advances and no
learned_orientation is ever committed. The OS cannot learn unattended.

## Goal

The supervisor auto-generates reward and pain signals based on observable
purpose fulfillment, closing the learning loop without external intervention.

## Current State

- `goose_supervisor_learn_sync` (goose_supervisor.c:284) reads `sys.ai.reward`
  and `sys.ai.pain` cells. On reward, co-active routes increment
  hebbian_counter; on pain, counters decay toward zero.
- Purpose-aware amplification doubles reward increments when `sys.purpose` is
  set (line 299).
- `goose_supervisor_pulse` already calls learn_sync on a divider
  (`REFLEX_SUPERVISOR_LEARN_DIV`). Adding an evaluate call on its own divider
  is the natural integration point.

## Architecture

New function: `goose_supervisor_evaluate(void)` called at 1Hz from
`goose_supervisor_pulse` on a new `REFLEX_SUPERVISOR_EVAL_DIV` divider, placed
before the `learn_div` block so that reward/pain are written before the
learning pass consumes them.

### Evaluation Logic (v1)

```
int reward_score = 0;
for each supervised field:
    for each route touching the active purpose domain:
        if source.state == sink.state:
            reward_score++

if reward_score >= REFLEX_AUTO_REWARD_THRESHOLD:
    sys.ai.reward = +1

for each supervised field:
    for each route where sink.type == GOOSE_CELL_HARDWARE_OUT:
        if sink.state != expected for >5 consecutive evaluate calls:
            sys.ai.pain = -1
            break
```

The 5-call (5-second) stuck threshold prevents transient disagreements from
triggering pain during normal propagation delays.

### Constants (reflex_tuning.h)

- `REFLEX_SUPERVISOR_EVAL_DIV` -- divider for 1Hz evaluate (same as learn_div)
- `REFLEX_AUTO_REWARD_THRESHOLD` -- minimum converged routes to emit reward (default 2)
- `REFLEX_AUTO_PAIN_STUCK_TICKS` -- consecutive disagreements before pain (default 5)

## Files Changed

| File | Change |
|------|--------|
| `goose_supervisor.c` | +`goose_supervisor_evaluate()` (~30 LOC), +1 divider block in pulse |
| `reflex_tuning.h` | +3 constants |

## Non-Goals

- Multi-objective optimization or weighted reward functions.
- External reward API or network-sourced reward.
- Changing the Hebbian commit threshold or plasticity rule.

## Validation

1. Flash board. `purpose set led`. Create route from intent cell to LED output.
2. Toggle LED via route so source and sink converge.
3. Observe `hebbian show` counters advancing without any manual `reward` or
   `pain` shell command.
4. Force LED output stuck at wrong sign for >5s. Observe pain-driven decay.

## Effort

~50 LOC across two files. Estimated 1 hour.
