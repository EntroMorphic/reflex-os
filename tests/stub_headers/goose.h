/* Stubs for host-side testing — replaces ESP-IDF and GOOSE headers */
#ifndef REFLEX_TEST_STUBS_H
#define REFLEX_TEST_STUBS_H

#include "reflex_types.h"
#include "reflex_ternary.h"
#include <stdbool.h>

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

/* goose.h stub — only what the VM headers structurally require.
 *
 * reflex_vm_state_t embeds a goose_route_t array for the TROUTE manifest, so
 * the type has to exist for vm/cache.c and vm/loader.c to compile on host.
 * These are layout stand-ins, not the substrate: nothing here is exercised by
 * the VM tests, which only need the struct to have a size. The real
 * definitions live in components/goose/include/goose.h. */
typedef enum {
    GOOSE_COUPLING_HARDWARE, GOOSE_COUPLING_SOFTWARE, GOOSE_COUPLING_ASYNC,
    GOOSE_COUPLING_RADIO, GOOSE_COUPLING_DMA, GOOSE_COUPLING_SILICON
} goose_coupling_t;

typedef struct {
    char name[16];
    reflex_tryte9_t source_coord;
    reflex_tryte9_t sink_coord;
    reflex_trit_t orientation;
    reflex_trit_t learned_orientation;
    reflex_tryte9_t control_coord;
    goose_coupling_t coupling;
    void *cached_source;
    void *cached_sink;
    void *cached_control;
    uint32_t cached_version;
    int16_t hebbian_counter;
} goose_route_t;

#endif
