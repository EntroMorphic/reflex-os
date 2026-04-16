#ifndef REFLEX_FABRIC_H
#define REFLEX_FABRIC_H

#include <stdint.h>
#include <stdbool.h>

#include "reflex_types.h"
#include "reflex_ternary.h"

#ifdef __cplusplus
extern "C" {
#endif

#define REFLEX_NODE_SYSTEM  0
#define REFLEX_NODE_LED     1
#define REFLEX_NODE_BUTTON  2
#define REFLEX_NODE_GATEWAY 3
#define REFLEX_NODE_VM      7

/**
 * @brief Ternary Channel QoS
 */
typedef enum {
    REFLEX_CHAN_CRITICAL = -1,  // Real-Time / No Drops
    REFLEX_CHAN_SYSTEM = 0,     // Interactive / Standard
    REFLEX_CHAN_TELEM = 1       // Background / Best Effort
} reflex_channel_t;

/**
 * @brief Ternary Message
 * Optimized for word18 transfers between nodes.
 */
typedef struct {
    uint8_t to;
    uint8_t from;
    uint8_t op;
    reflex_channel_t channel;
    uint32_t correlation_id;
    reflex_word18_t payload;
} reflex_message_t;

reflex_err_t reflex_fabric_init(void);

/**
 * @brief Send a message to the fabric.
 * This is non-blocking and lock-free where possible.
 */
reflex_err_t reflex_fabric_send(const reflex_message_t *msg);

/**
 * @brief Receive a message from the fabric for a specific node.
 */
reflex_err_t reflex_fabric_recv(uint8_t node_id, reflex_message_t *out_msg);

#ifdef __cplusplus
}
#endif

#endif
