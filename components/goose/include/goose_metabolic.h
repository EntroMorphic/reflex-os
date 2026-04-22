/**
 * @file goose_metabolic.h
 * @brief Metabolic Regulation — Phase 31.
 *
 * Two-layer self-governance: a circuit breaker (instant, aggregate) and
 * per-vital resource governance (fine-grained, per-sub-pass).
 *
 * Vital cells: perception.temp.reading (existing), perception.power.battery,
 * perception.mesh.health, perception.heap.pressure.
 *
 * Circuit breaker: sys.metabolic (+1 thriving, 0 conserving, -1 surviving).
 * Degradation is instant; recovery requires REFLEX_METABOLIC_RECOVERY_TICKS
 * consecutive stable readings (hysteresis).
 */
#ifndef GOOSE_METABOLIC_H
#define GOOSE_METABOLIC_H

#include "goose.h"
#include <stdbool.h>
#include <stdint.h>

/**
 * Initialize vital perception cells and the sys.metabolic circuit breaker.
 * Call during boot Stage 4 (after atlas weave, before services).
 */
reflex_err_t goose_metabolic_init(void);

/**
 * 1Hz metabolic sync pass. Reads vitals, updates ternary state for each,
 * computes circuit-breaker state with hysteresis, emits telemetry.
 * Called from goose_supervisor_pulse() as a time-divided sub-pass.
 */
reflex_err_t goose_metabolic_sync(void);

/** Current circuit-breaker state: +1 thriving, 0 conserving, -1 surviving. */
int8_t goose_metabolic_get_state(void);

/**
 * Override a vital cell's state for bench testing.
 * @param vital  One of "temp", "battery", "mesh", "heap"
 * @param state  Ternary state to inject (-1, 0, +1)
 * @return REFLEX_OK on success, REFLEX_ERR_INVALID_ARG if vital name unknown.
 */
reflex_err_t goose_metabolic_override(const char *vital, int8_t state);

/** Clear all overrides; resume reading real hardware. */
void goose_metabolic_clear_overrides(void);

/** Fill a buffer with a human-readable vitals summary for the shell. */
void goose_metabolic_format_vitals(char *buf, size_t buf_len);

#endif /* GOOSE_METABOLIC_H */
