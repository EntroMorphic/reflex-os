/**
 * @file goose_telemetry.c
 * @brief Streaming telemetry emitters for the GOOSE substrate.
 *
 * Each function formats a single #T:-prefixed line and writes it
 * directly to serial via reflex_hal_write_raw, bypassing stdio
 * buffering. This ensures telemetry from any task context (supervisor,
 * pulse, shell) reaches the host immediately.
 */

#include "goose_telemetry.h"
#include "reflex_hal.h"
#include <stdio.h>

volatile bool goose_telemetry_enabled = false;

static void telem_emit(const char *buf, int len) {
    if (len > 0) reflex_hal_write_raw(buf, len);
}

void goose_telem_cell(const char *name, int8_t state, int8_t type) {
    char buf[80];
    int n = snprintf(buf, sizeof(buf), "#T:C,%s,%d,%d\n", name ? name : "?", (int)state, (int)type);
    telem_emit(buf, n);
}

void goose_telem_route(const char *src, const char *sink, int8_t state, int coupling) {
    char buf[96];
    int n = snprintf(buf, sizeof(buf), "#T:R,%s,%s,%d,%d\n", src ? src : "?", sink ? sink : "?", (int)state, coupling);
    telem_emit(buf, n);
}

void goose_telem_hebbian(const char *route_name, int16_t counter, int8_t learned) {
    char buf[64];
    int n = snprintf(buf, sizeof(buf), "#T:H,%s,%d,%d\n", route_name ? route_name : "?", (int)counter, (int)learned);
    telem_emit(buf, n);
}

void goose_telem_weave(const char *route_name, const char *src, const char *sink) {
    char buf[128];
    int n = snprintf(buf, sizeof(buf), "#T:W,%s,%s,%s\n", route_name ? route_name : "?", src ? src : "?", sink ? sink : "?");
    telem_emit(buf, n);
}

void goose_telem_alloc(const char *name, int8_t type) {
    char buf[64];
    int n = snprintf(buf, sizeof(buf), "#T:A,%s,%d\n", name ? name : "?", (int)type);
    telem_emit(buf, n);
}

void goose_telem_evict(const char *name) {
    char buf[48];
    int n = snprintf(buf, sizeof(buf), "#T:E,%s\n", name ? name : "?");
    telem_emit(buf, n);
}

void goose_telem_balance(int8_t state) {
    char buf[16];
    int n = snprintf(buf, sizeof(buf), "#T:B,%d\n", (int)state);
    telem_emit(buf, n);
}

void goose_telem_mesh(const char *op, int8_t state) {
    char buf[32];
    int n = snprintf(buf, sizeof(buf), "#T:M,%s,%d\n", op ? op : "?", (int)state);
    telem_emit(buf, n);
}

void goose_telem_purpose(const char *name) {
    char buf[48];
    int n = snprintf(buf, sizeof(buf), "#T:P,%s\n", (name && name[0]) ? name : "(clear)");
    telem_emit(buf, n);
}

void goose_telem_eval(int reward_score, bool pain) {
    char buf[24];
    int n = snprintf(buf, sizeof(buf), "#T:V,%d,%d\n", reward_score, pain ? 1 : 0);
    telem_emit(buf, n);
}
