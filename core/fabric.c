/** @file fabric.c
 * @brief Inter-node message fabric transport.
 */
#include "reflex_fabric.h"

#include <string.h>
#include <stdatomic.h>

#include "reflex_types.h"
#include "reflex_hal.h"
#include "reflex_task.h"

#include "reflex_log.h"

#define REFLEX_FABRIC_QUEUE_LEN 32
#define REFLEX_NODE_MAX 8

static reflex_queue_handle_t s_node_inboxes[REFLEX_NODE_MAX];
static bool s_reflex_fabric_ready = false;

reflex_err_t reflex_fabric_init(void)
{
    for (int i = 0; i < REFLEX_NODE_MAX; i++) {
        s_node_inboxes[i] = reflex_queue_create(REFLEX_FABRIC_QUEUE_LEN, sizeof(reflex_message_t));
        if (s_node_inboxes[i] == NULL) {
            return REFLEX_ERR_NO_MEM;
        }
    }
    
    s_reflex_fabric_ready = true;
    REFLEX_LOGI(REFLEX_BOOT_TAG, "ternary_fabric=ready");
    return REFLEX_OK;
}

reflex_err_t reflex_fabric_send(const reflex_message_t *msg)
{
    if (!s_reflex_fabric_ready) return REFLEX_ERR_INVALID_STATE;
    REFLEX_RETURN_ON_FALSE(msg != NULL, REFLEX_ERR_INVALID_ARG, "reflex.fabric", "message required");
    REFLEX_RETURN_ON_FALSE(msg->to < REFLEX_NODE_MAX, REFLEX_ERR_INVALID_ARG, "reflex.fabric", "invalid destination");

    // QoS Logic: In a full implementation, we'd check channel priority here.
    if (reflex_queue_send(s_node_inboxes[msg->to], msg, 0) != REFLEX_OK) {
        if (msg->channel == REFLEX_CHAN_TELEM) {
            return REFLEX_OK; // Silently drop telemetry if full
        }
        return REFLEX_ERR_TIMEOUT;
    }

    return REFLEX_OK;
}

reflex_err_t reflex_fabric_recv(uint8_t node_id, reflex_message_t *out_msg)
{
    if (!s_reflex_fabric_ready) return REFLEX_ERR_INVALID_STATE;
    REFLEX_RETURN_ON_FALSE(node_id < REFLEX_NODE_MAX, REFLEX_ERR_INVALID_ARG, "reflex.fabric", "invalid node");
    REFLEX_RETURN_ON_FALSE(out_msg != NULL, REFLEX_ERR_INVALID_ARG, "reflex.fabric", "output required");

    if (reflex_queue_recv(s_node_inboxes[node_id], out_msg, 0) == REFLEX_OK) {
        return REFLEX_OK;
    }

    return REFLEX_ERR_NOT_FOUND;
}
