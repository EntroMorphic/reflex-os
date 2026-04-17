/**
 * @file reflex_freertos_shim.c
 * @brief FreeRTOS compatibility shim — routes FreeRTOS calls to our scheduler.
 *
 * ESP-IDF's internal components (Wi-Fi, console, etc.) call FreeRTOS
 * functions directly. This shim provides __wrap_ implementations that
 * route those calls to the Reflex kernel scheduler.
 *
 * The shim covers:
 * - xTaskCreatePinnedToCore / xTaskCreate → reflex_sched_create_task
 * - vTaskDelay → reflex_sched_delay_ms
 * - vTaskDelete → reflex_sched_delete_task
 * - xQueueCreate/Send/Receive → reflex queue impl
 * - vTaskStartScheduler → reflex_sched_start (wrapped in app_startup)
 */

#include "reflex_sched.h"
#include "reflex_task.h"
#include <stdlib.h>
#include <string.h>

/* FreeRTOS type compatibility (these match FreeRTOS's actual types) */
typedef void *TaskHandle_t;
typedef void *QueueHandle_t;
typedef int BaseType_t;
typedef unsigned int UBaseType_t;
typedef uint32_t TickType_t;
typedef void (*TaskFunction_t)(void *);
#define pdPASS 1
#define pdFAIL 0
#define pdTRUE 1
#define pdFALSE 0
#define portMAX_DELAY 0xFFFFFFFF
#define portTICK_PERIOD_MS 1

/* ---- Task shims ---- */

BaseType_t __wrap_xTaskCreatePinnedToCore(TaskFunction_t fn, const char *name,
                                          uint32_t stack, void *arg,
                                          UBaseType_t priority,
                                          TaskHandle_t *handle,
                                          int core) {
    (void)core;  /* single core — ignore affinity */
    reflex_tcb_t *tcb = NULL;
    reflex_err_t rc = reflex_sched_create_task(fn, name, stack, arg,
                                                (int)priority, &tcb);
    if (rc != REFLEX_OK) return pdFAIL;
    if (handle) *handle = (TaskHandle_t)tcb;
    return pdPASS;
}

void __wrap_vTaskDelay(TickType_t ticks) {
    reflex_sched_delay_ms(ticks * portTICK_PERIOD_MS);
}

void __wrap_vTaskDelete(TaskHandle_t handle) {
    reflex_sched_delete_task((reflex_tcb_t *)handle);
}

/* ---- Queue shims ---- */

QueueHandle_t __wrap_xQueueCreate(UBaseType_t length, UBaseType_t item_size) {
    return (QueueHandle_t)reflex_queue_create(length, item_size);
}

QueueHandle_t __wrap_xQueueGenericCreate(UBaseType_t length, UBaseType_t item_size, uint8_t type) {
    (void)type;
    return (QueueHandle_t)reflex_queue_create(length, item_size);
}

BaseType_t __wrap_xQueueGenericSend(QueueHandle_t q, const void *item,
                                    TickType_t timeout, BaseType_t copy_position) {
    (void)copy_position;
    uint32_t ms = (timeout == portMAX_DELAY) ? UINT32_MAX : timeout * portTICK_PERIOD_MS;
    return (reflex_queue_send((reflex_queue_handle_t)q, item, ms) == REFLEX_OK) ? pdPASS : pdFAIL;
}

BaseType_t __wrap_xQueueReceive(QueueHandle_t q, void *item, TickType_t timeout) {
    uint32_t ms = (timeout == portMAX_DELAY) ? UINT32_MAX : timeout * portTICK_PERIOD_MS;
    return (reflex_queue_recv((reflex_queue_handle_t)q, item, ms) == REFLEX_OK) ? pdPASS : pdFAIL;
}

/* ---- Scheduler shims ---- */

void __wrap_vTaskStartScheduler(void) {
    /* The Reflex scheduler is already started by reflex_app_startup.
     * This should never be called, but if it is, just return. */
}

BaseType_t __wrap_xTaskGetSchedulerState(void) {
    return 2;  /* taskSCHEDULER_RUNNING */
}

TaskHandle_t __wrap_xTaskGetCurrentTaskHandle(void) {
    /* Return something non-NULL so ESP-IDF's assert checks pass */
    extern reflex_tcb_t *reflex_sched_get_current(void);
    return (TaskHandle_t)reflex_sched_get_current();
}

/* ---- Critical section shims ---- */

void __wrap_vPortEnterCritical(void *mux) {
    (void)mux;
    reflex_sched_enter_critical();
}

void __wrap_vPortExitCritical(void *mux) {
    (void)mux;
    reflex_sched_exit_critical();
}

/* ---- Yield shim ---- */

void __wrap_vPortYield(void) {
    reflex_sched_yield();
}
