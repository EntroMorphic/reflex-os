#include "reflex_fabric.h"

#include <string.h>
#include <stdatomic.h>

#include "esp_check.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

#include "reflex_log.h"

#define REFLEX_FABRIC_QUEUE_LEN 32
#define REFLEX_NODE_MAX 8

static QueueHandle_t s_node_inboxes[REFLEX_NODE_MAX];
static bool s_reflex_fabric_ready = false;

esp_err_t reflex_fabric_init(void)
{
    for (int i = 0; i < REFLEX_NODE_MAX; i++) {
        s_node_inboxes[i] = xQueueCreate(REFLEX_FABRIC_QUEUE_LEN, sizeof(reflex_message_t));
        if (s_node_inboxes[i] == NULL) {
            return ESP_ERR_NO_MEM;
        }
    }
    
    s_reflex_fabric_ready = true;
    REFLEX_LOGI(REFLEX_BOOT_TAG, "ternary_fabric=ready");
    return ESP_OK;
}

esp_err_t reflex_fabric_send(const reflex_message_t *msg)
{
    if (!s_reflex_fabric_ready) return ESP_ERR_INVALID_STATE;
    ESP_RETURN_ON_FALSE(msg != NULL, ESP_ERR_INVALID_ARG, "reflex.fabric", "message required");
    ESP_RETURN_ON_FALSE(msg->to < REFLEX_NODE_MAX, ESP_ERR_INVALID_ARG, "reflex.fabric", "invalid destination");

    // QoS Logic: In a full implementation, we'd check channel priority here.
    // For MVP, we use the standard FreeRTOS queue.
    if (xQueueSend(s_node_inboxes[msg->to], msg, 0) != pdPASS) {
        if (msg->channel == REFLEX_CHAN_TELEM) {
            return ESP_OK; // Silently drop telemetry if full
        }
        return ESP_ERR_TIMEOUT;
    }

    return ESP_OK;
}

esp_err_t reflex_fabric_recv(uint8_t node_id, reflex_message_t *out_msg)
{
    if (!s_reflex_fabric_ready) return ESP_ERR_INVALID_STATE;
    ESP_RETURN_ON_FALSE(node_id < REFLEX_NODE_MAX, ESP_ERR_INVALID_ARG, "reflex.fabric", "invalid node");
    ESP_RETURN_ON_FALSE(out_msg != NULL, ESP_ERR_INVALID_ARG, "reflex.fabric", "output required");

    if (xQueueReceive(s_node_inboxes[node_id], out_msg, 0) == pdPASS) {
        return ESP_OK;
    }

    return ESP_ERR_NOT_FOUND;
}
