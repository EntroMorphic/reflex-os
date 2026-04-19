/**
 * @file reflex_task_esp32c6.c
 * @brief Reflex Task — FreeRTOS backend.
 */

#include "reflex_task.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"

reflex_err_t reflex_task_create(void (*fn)(void *), const char *name,
                                uint32_t stack_bytes, void *arg, int priority,
                                reflex_task_handle_t *out_handle) {
    TaskHandle_t h = NULL;
    BaseType_t rc = xTaskCreate(fn, name, stack_bytes, arg, priority,
                                out_handle ? &h : NULL);
    if (rc != pdPASS) return REFLEX_ERR_NO_MEM;
    if (out_handle) *out_handle = (reflex_task_handle_t)h;
    return REFLEX_OK;
}

void reflex_task_delete(reflex_task_handle_t handle) {
    vTaskDelete((TaskHandle_t)handle);
}

void reflex_task_delay_ms(uint32_t ms) {
    vTaskDelay(pdMS_TO_TICKS(ms));
}

void reflex_task_yield(void) {
    taskYIELD();
}

reflex_task_handle_t reflex_task_get_by_name(const char *name) {
    return (reflex_task_handle_t)xTaskGetHandle(name);
}

void reflex_task_set_priority(reflex_task_handle_t handle, int priority) {
    if (handle) vTaskPrioritySet((TaskHandle_t)handle, (UBaseType_t)priority);
}

int reflex_task_get_priority(reflex_task_handle_t handle) {
    if (!handle) return 0;
    return (int)uxTaskPriorityGet((TaskHandle_t)handle);
}

reflex_queue_handle_t reflex_queue_create(uint32_t length, uint32_t item_size) {
    return (reflex_queue_handle_t)xQueueCreate(length, item_size);
}

reflex_err_t reflex_queue_send(reflex_queue_handle_t q, const void *item,
                               uint32_t timeout_ms) {
    if (!q) return REFLEX_ERR_INVALID_ARG;
    TickType_t ticks = (timeout_ms == UINT32_MAX) ? portMAX_DELAY
                                                  : pdMS_TO_TICKS(timeout_ms);
    if (xQueueSend((QueueHandle_t)q, item, ticks) != pdPASS)
        return REFLEX_ERR_TIMEOUT;
    return REFLEX_OK;
}

reflex_err_t reflex_queue_recv(reflex_queue_handle_t q, void *item,
                               uint32_t timeout_ms) {
    if (!q) return REFLEX_ERR_INVALID_ARG;
    TickType_t ticks = (timeout_ms == UINT32_MAX) ? portMAX_DELAY
                                                  : pdMS_TO_TICKS(timeout_ms);
    if (xQueueReceive((QueueHandle_t)q, item, ticks) != pdPASS)
        return REFLEX_ERR_NOT_FOUND;
    return REFLEX_OK;
}

reflex_mutex_t reflex_mutex_init(void) {
    reflex_mutex_t m;
    portMUX_TYPE init = portMUX_INITIALIZER_UNLOCKED;
    _Static_assert(sizeof(portMUX_TYPE) <= sizeof(reflex_mutex_t),
                   "reflex_mutex_t too small for portMUX_TYPE");
    __builtin_memcpy(&m, &init, sizeof(init));
    return m;
}

void reflex_critical_enter(reflex_mutex_t *m) {
    taskENTER_CRITICAL((portMUX_TYPE *)m);
}

void reflex_critical_exit(reflex_mutex_t *m) {
    taskEXIT_CRITICAL((portMUX_TYPE *)m);
}
