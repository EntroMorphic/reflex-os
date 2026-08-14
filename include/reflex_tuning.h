#ifndef REFLEX_TUNING_H
#define REFLEX_TUNING_H

/**
 * @file reflex_tuning.h
 * @brief Every tunable constant the substrate reasons with, in one place.
 *
 * These are policy, not implementation detail: pulse divisors, swarm consensus
 * thresholds, replay window, metabolic limits. They live together so a change
 * to one can be weighed against the others it interacts with — the swarm
 * threshold and the per-packet weight cap only make sense read as a pair, and
 * the metabolic isolation count is derived from the discovery divisor rather
 * than chosen independently.
 *
 * Each is overridable with `-D` so a build can retune without editing source.
 */

/* Supervisor pulse rates (divisors at 10Hz base)
 *
 * Consumed as `if (++div >= N) { run(); div = 0; }`, which fires on exactly the
 * N-th pulse. A divisor of 10 at 10Hz is therefore a true 1Hz.
 *
 * These were previously written `div++ >= N`, which fires on the (N+1)-th
 * pulse — every sub-pass ran ~9% slow, so the "1Hz" passes were really
 * ~0.909Hz. Harmless in itself, but it meant any constant derived from these
 * divisors had to carry a correction term to stay honest. The rates below can
 * now be read at face value.
 */
#ifndef REFLEX_SUPERVISOR_WEAVE_DIV
#define REFLEX_SUPERVISOR_WEAVE_DIV     10   /* Autonomic fabrication: 1Hz */
#endif
#ifndef REFLEX_SUPERVISOR_LEARN_DIV
#define REFLEX_SUPERVISOR_LEARN_DIV     10   /* Hebbian learning: 1Hz */
#endif
#ifndef REFLEX_SUPERVISOR_SYNC_DIV
#define REFLEX_SUPERVISOR_SYNC_DIV      10   /* Swarm sync: 1Hz */
#endif
#ifndef REFLEX_SUPERVISOR_STALE_DIV
#define REFLEX_SUPERVISOR_STALE_DIV     50   /* Staleness check: 0.2Hz */
#endif
#ifndef REFLEX_SUPERVISOR_SNAP_DIV
#define REFLEX_SUPERVISOR_SNAP_DIV      300  /* Snapshot save: ~30s */
#endif

/* Task priority landscape */
#ifndef REFLEX_PULSE_BASE_PRIORITY
#define REFLEX_PULSE_BASE_PRIORITY      10
#endif
#ifndef REFLEX_PULSE_PURPOSE_BOOST
#define REFLEX_PULSE_PURPOSE_BOOST      3
#endif
#ifndef REFLEX_PULSE_HEBBIAN_BOOST
#define REFLEX_PULSE_HEBBIAN_BOOST      2
#endif
#ifndef REFLEX_PULSE_PAIN_PENALTY
#define REFLEX_PULSE_PAIN_PENALTY       2
#endif
#ifndef REFLEX_PULSE_MAX_PRIORITY
#define REFLEX_PULSE_MAX_PRIORITY       15
#endif

/* Hebbian learning */
#ifndef REFLEX_HEBBIAN_COMMIT_THRESHOLD
#define REFLEX_HEBBIAN_COMMIT_THRESHOLD 8
#endif
#ifndef REFLEX_HEBBIAN_COUNTER_MAX
#define REFLEX_HEBBIAN_COUNTER_MAX      (REFLEX_HEBBIAN_COMMIT_THRESHOLD * 2)
#endif

/* Fabric limits */
#ifndef REFLEX_FABRIC_MAX_CELLS
#define REFLEX_FABRIC_MAX_CELLS         256
#endif
#ifndef REFLEX_LATTICE_BUCKETS
#define REFLEX_LATTICE_BUCKETS          503
#endif
/* Buckets for each of the registry's two indexes (by name, by coordinate).
 * Must exceed REFLEX_FABRIC_MAX_CELLS so a probe run always meets an empty
 * bucket; prime, and roughly 2x capacity, to keep runs short. Costs
 * 2 * 503 * 2 bytes of BSS. */
#ifndef REFLEX_REGISTRY_BUCKETS
#define REFLEX_REGISTRY_BUCKETS         503
#endif
#define GOOSE_FABRIC_MAX_CELLS REFLEX_FABRIC_MAX_CELLS
#define GOOSE_LATTICE_BUCKETS  REFLEX_LATTICE_BUCKETS

/* Mesh/atmosphere */
#ifndef REFLEX_SWARM_THRESHOLD
#define REFLEX_SWARM_THRESHOLD          10
#endif
#ifndef REFLEX_SWARM_ACCUM_MAX
#define REFLEX_SWARM_ACCUM_MAX          100
#endif
#ifndef REFLEX_SWARM_WEIGHT_MAX
#define REFLEX_SWARM_WEIGHT_MAX         4
#endif
#ifndef REFLEX_REPLAY_CACHE_SLOTS
#define REFLEX_REPLAY_CACHE_SLOTS       64
#endif
#ifndef REFLEX_REPLAY_WINDOW_US
#define REFLEX_REPLAY_WINDOW_US         (5 * 1000 * 1000ULL)
#endif

/* Boot */
#ifndef REFLEX_BOOT_LOOP_THRESHOLD
#define REFLEX_BOOT_LOOP_THRESHOLD      10
#endif
#ifndef REFLEX_STABILITY_MS
#define REFLEX_STABILITY_MS             10000
#endif

/* Rebalance */
#ifndef REFLEX_MAX_REBALANCE_ITERATIONS
#define REFLEX_MAX_REBALANCE_ITERATIONS 10
#endif

/* Mesh discovery heartbeat */
#ifndef REFLEX_SUPERVISOR_DISCOVER_DIV
#define REFLEX_SUPERVISOR_DISCOVER_DIV  100  /* 10s at 10Hz supervisor */
#endif

/* Service watchdog */
#ifndef REFLEX_SUPERVISOR_WATCHDOG_DIV
#define REFLEX_SUPERVISOR_WATCHDOG_DIV  10   /* 1Hz at 10Hz supervisor */
#endif

/* Autonomous reward/pain evaluation */
#ifndef REFLEX_SUPERVISOR_EVAL_DIV
#define REFLEX_SUPERVISOR_EVAL_DIV      10   /* 1Hz at 10Hz supervisor */
#endif
#ifndef REFLEX_AUTO_REWARD_THRESHOLD
#define REFLEX_AUTO_REWARD_THRESHOLD    2
#endif
#ifndef REFLEX_AUTO_PAIN_STUCK_TICKS
#define REFLEX_AUTO_PAIN_STUCK_TICKS    5
#endif

/* Metabolic regulation (Phase 31) */
#ifndef REFLEX_SUPERVISOR_METABOLIC_DIV
#define REFLEX_SUPERVISOR_METABOLIC_DIV  10   /* 1Hz at 10Hz supervisor */
#endif
#ifndef REFLEX_METABOLIC_HEAP_CRITICAL
#define REFLEX_METABOLIC_HEAP_CRITICAL   8192
#endif
#ifndef REFLEX_METABOLIC_HEAP_TIGHT
#define REFLEX_METABOLIC_HEAP_TIGHT      16384
#endif
#ifndef REFLEX_METABOLIC_MESH_ISOLATED_TICKS
/* Consecutive idle vital-scans before the mesh reads -1 (isolated).
 *
 * This must be expressed relative to the discovery beacon, not as a constant.
 * The vital is scanned every 1s but a quiet-but-healthy mesh only emits a
 * beacon every 10s, so a fixed threshold of 3 (3s) declared isolation
 * for roughly 7s out of every 10s on a working two-board pair — which then
 * halved the discovery interval via the inverted governance in
 * goose_supervisor_pulse, hunting for peers that were already answering.
 * Measured on hardware: two paired C6s reported mesh=-1 and mesh=0
 * simultaneously, purely depending on where each sampled in the beacon cycle.
 *
 * Derived so it tracks the dividers if either is retuned: tolerate two whole
 * beacon periods of silence before calling the mesh isolated. The +1
 * corrections this used to carry are gone now that the dividers fire on
 * exactly their N-th pulse.
 *
 * Floored at 2. The division is integer, so a future retune where the vital
 * scan is slower than half the beacon period (say METABOLIC_DIV 250 against
 * DISCOVER_DIV 100) would evaluate to 0 — and `idle_ticks >= 0` is always
 * true, which would report the mesh permanently isolated while resetting the
 * arc window on every tick. A floor of 2 is the smallest value for which the
 * window and its predecessor are distinct, which the connected check relies
 * on. Deriving a constant from other constants is only safe if the derivation
 * cannot degenerate. */
#define REFLEX_METABOLIC_MESH_ISOLATED_TICKS_RAW \
    ((REFLEX_SUPERVISOR_DISCOVER_DIV * 2) / REFLEX_SUPERVISOR_METABOLIC_DIV)
#define REFLEX_METABOLIC_MESH_ISOLATED_TICKS \
    (REFLEX_METABOLIC_MESH_ISOLATED_TICKS_RAW < 2 ? 2 : REFLEX_METABOLIC_MESH_ISOLATED_TICKS_RAW)
#endif
/* Arcs required within the isolation window (plus the previous completed
 * window) before the mesh reads +1 connected.
 *
 * This is compared against a windowed count, not a single vital scan. It was 5
 * per 1s scan, which needed 5 arcs/sec while a quiet pair produces ~0.1 — so
 * +1 was unreachable and the vital was ternary in name only.
 *
 * The window spans 2 discovery beacons (20s) and the check also counts the
 * previous window, so a healthy peer contributes ~4 arcs over the observed 40s.
 * A threshold of 2 therefore means "heard from someone at least twice in
 * roughly the last 40s", which stays true across one missed beacon but goes
 * false when a peer actually stops talking. */
#ifndef REFLEX_METABOLIC_MESH_SPARSE_THRESHOLD
#define REFLEX_METABOLIC_MESH_SPARSE_THRESHOLD 2
#endif
#ifndef REFLEX_METABOLIC_RECOVERY_TICKS
#define REFLEX_METABOLIC_RECOVERY_TICKS  30  /* 30s at 1Hz metabolic sync */
#endif

/* Self-Expanding Perception (Phase 33) */
#ifndef REFLEX_SUPERVISOR_EXPLORE_DIV
#define REFLEX_SUPERVISOR_EXPLORE_DIV    10  /* 1Hz at 10Hz supervisor */
#endif
#ifndef REFLEX_EXPLORE_PROBE_COUNT
#define REFLEX_EXPLORE_PROBE_COUNT       4   /* Baseline probes per pulse (doubled under pain) */
#endif
#ifndef REFLEX_EXPLORE_PAIN_THRESHOLD
#define REFLEX_EXPLORE_PAIN_THRESHOLD    5   /* Pain ticks before rate amplification */
#endif
#ifndef REFLEX_EXPLORE_MAX_ACTIVE
#define REFLEX_EXPLORE_MAX_ACTIVE        30  /* Hard cap on exploration cells */
#endif

#endif /* REFLEX_TUNING_H */
