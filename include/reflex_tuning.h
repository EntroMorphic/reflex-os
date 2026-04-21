#ifndef REFLEX_TUNING_H
#define REFLEX_TUNING_H

/* Supervisor pulse rates (divisors at 10Hz base) */
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
#define REFLEX_HEBBIAN_COUNTER_MAX      (REFLEX_HEBBIAN_COMMIT_THRESHOLD * 2)

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

#endif
