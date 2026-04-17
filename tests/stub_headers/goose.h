/* Stubs for host-side testing — replaces ESP-IDF and GOOSE headers */
#ifndef REFLEX_TEST_STUBS_H
#define REFLEX_TEST_STUBS_H

#include "reflex_types.h"
#include "reflex_ternary.h"

/* reflex_fabric.h stub */
#define REFLEX_NODE_VM 7
typedef enum { REFLEX_CHAN_CRITICAL = -1, REFLEX_CHAN_SYSTEM = 0, REFLEX_CHAN_TELEM = 1 } reflex_channel_t;
typedef struct {
    uint8_t to, from, op;
    reflex_channel_t channel;
    uint32_t correlation_id;
    reflex_word18_t payload;
} reflex_message_t;
static inline reflex_err_t reflex_fabric_send(const reflex_message_t *m) { (void)m; return REFLEX_OK; }
static inline reflex_err_t reflex_fabric_recv(uint8_t n, reflex_message_t *m) { (void)n; (void)m; return REFLEX_ERR_NOT_FOUND; }
static inline reflex_err_t reflex_fabric_init(void) { return REFLEX_OK; }

#endif
