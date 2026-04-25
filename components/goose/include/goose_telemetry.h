/**
 * @file goose_telemetry.h
 * @brief Streaming telemetry for the GOOSE substrate (Phase 30: Loom Viewer).
 *
 * All telemetry is gated by goose_telemetry_enabled. When false (default),
 * the cost is a single volatile bool read per hook site (~2 cycles on RISC-V).
 * When true, each hook emits a #T:-prefixed line on serial for the host bridge.
 */
#ifndef GOOSE_TELEMETRY_H
#define GOOSE_TELEMETRY_H

#include <stdbool.h>
#include <stdint.h>

extern volatile bool goose_telemetry_enabled;

/**
 * Gate macro. Evaluates expr only when telemetry is enabled.
 * Uses __builtin_expect to hint the branch predictor toward the common
 * (disabled) path.
 */
#define TELEM_IF(expr) do { \
    if (__builtin_expect(goose_telemetry_enabled, 0)) { expr; } \
} while(0)

/** Cell state change: #T:C,<name>,<state>,<type> */
void goose_telem_cell(const char *name, int8_t state, int8_t type);

/** Route sink write: #T:R,<src>,<sink>,<state>,<coupling> */
void goose_telem_route(const char *src, const char *sink, int8_t state, int coupling);

/** Hebbian update: #T:H,<route_name>,<counter>,<learned_orientation> */
void goose_telem_hebbian(const char *route_name, int16_t counter, int8_t learned);

/** Autonomous route woven: #T:W,<route_name>,<src>,<sink> */
void goose_telem_weave(const char *route_name, const char *src, const char *sink);

/** Cell allocated: #T:A,<name>,<type> */
void goose_telem_alloc(const char *name, int8_t type);

/** Cell evicted: #T:E,<name> */
void goose_telem_evict(const char *name);

/** Supervisor equilibrium: #T:B,<balance_state> */
void goose_telem_balance(int8_t state);

/** Mesh arc event: #T:M,<op>,<state> */
void goose_telem_mesh(const char *op, int8_t state);

/** Purpose set/clear: #T:P,<name> */
void goose_telem_purpose(const char *name);

/** Autonomous evaluation: #T:V,<reward_score>,<pain> */
void goose_telem_eval(int reward_score, bool pain);

/** Metabolic state: #T:X,<metabolic>,<temp>,<battery>,<mesh>,<heap> */
void goose_telem_metabolic(int8_t metabolic, int8_t temp, int8_t batt, int8_t mesh, int8_t heap);

/** Exploration discovery: #T:D,<name> */
void goose_telem_explore(const char *name);

#endif /* GOOSE_TELEMETRY_H */
