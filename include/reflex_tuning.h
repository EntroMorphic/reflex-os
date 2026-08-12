#ifndef REFLEX_TUNING_H
#define REFLEX_TUNING_H

/* Supervisor pulse rates (divisors at 10Hz base)
 *
 * These are consumed as `if (div++ >= N) { run(); div = 0; }`, which fires on
 * the (N+1)-th pulse, not the N-th. A divisor of 10 at 10Hz therefore runs at
 * ~0.909Hz with an 1.1s period, not the 1Hz the comments below name. The rates
 * are quoted as their design intent; the ~9% shortfall is uniform across every
 * sub-pass and has no functional consequence, so the timing is left as-is
 * rather than changing the cadence of every subsystem at once. Anything that
 * needs a true 1Hz should not assume these are exact.
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
/* Consecutive idle vital-scans before the mesh reads -1 (isolated). The vital
 * scan runs at REFLEX_SUPERVISOR_METABOLIC_DIV (~1.1s), not the 10s an earlier
 * comment assumed, so this is ~3.3s of silence — not 30s. */
#define REFLEX_METABOLIC_MESH_ISOLATED_TICKS  3
#endif
#ifndef REFLEX_METABOLIC_MESH_SPARSE_THRESHOLD
#define REFLEX_METABOLIC_MESH_SPARSE_THRESHOLD 5
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
