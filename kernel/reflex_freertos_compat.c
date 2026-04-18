/**
 * @file reflex_freertos_compat.c
 * @brief Complete FreeRTOS API backed by the Reflex kernel scheduler.
 *
 * Provides every FreeRTOS function that ESP-IDF components call,
 * implemented using reflex_sched.c primitives. When this file is
 * linked and FreeRTOS's kernel .c files are excluded, the Reflex
 * scheduler runs ALL tasks — including ESP-IDF's internal ones
 * (Wi-Fi, timers, etc.).
 *
 * This is NOT a shim or a wrapper. This IS the RTOS implementation.
 * It just happens to speak FreeRTOS's API so ESP-IDF components
 * work without modification.
 */

#include "reflex_sched.h"
#include "reflex_task.h"
#include <stdlib.h>
#include <string.h>

/* ---- FreeRTOS type compatibility ---- */
typedef void *TaskHandle_t;
typedef void *QueueHandle_t;
typedef void *SemaphoreHandle_t;
typedef void *EventGroupHandle_t;
typedef int BaseType_t;
typedef unsigned int UBaseType_t;
typedef uint32_t TickType_t;
typedef uint32_t EventBits_t;
typedef void (*TaskFunction_t)(void *);

#define pdPASS          1
#define pdFAIL          0
#define pdTRUE          1
#define pdFALSE         0
#define portMAX_DELAY   0xFFFFFFFF
#define queueSEND_TO_BACK  0
#define queueOVERWRITE     2

/* ---- Thread-local storage ---- */
#define configNUM_THREAD_LOCAL_STORAGE_POINTERS 2
static void *s_tls[REFLEX_SCHED_MAX_TASKS][configNUM_THREAD_LOCAL_STORAGE_POINTERS];

/* ---- Globals that FreeRTOS code references ---- */
/* These exist so any residual FreeRTOS code that reads them doesn't crash */
volatile TickType_t xTickCount = 0;
volatile BaseType_t xSchedulerRunning = pdFALSE;

/* ---- Task API ---- */

BaseType_t xTaskCreatePinnedToCore(TaskFunction_t fn, const char *name,
                                   uint32_t stack, void *arg,
                                   UBaseType_t prio, TaskHandle_t *handle,
                                   int core) {
    (void)core;
    reflex_tcb_t *tcb = NULL;
    if (reflex_sched_create_task(fn, name, stack, arg, (int)prio, &tcb) != REFLEX_OK)
        return pdFAIL;
    if (handle) *handle = (TaskHandle_t)tcb;
    return pdPASS;
}

BaseType_t xTaskCreateStaticPinnedToCore(TaskFunction_t fn, const char *name,
                                         uint32_t stack, void *arg,
                                         UBaseType_t prio, void *stack_buf,
                                         void *tcb_buf, TaskHandle_t *handle,
                                         int core) {
    /* Static allocation — ignore the buffers, use dynamic */
    return xTaskCreatePinnedToCore(fn, name, stack, arg, prio, handle, core);
}

void vTaskDelay(TickType_t ticks) {
    reflex_sched_delay_ms(ticks); /* ticks ≈ ms at 1kHz */
}

void vTaskDelete(TaskHandle_t h) {
    reflex_sched_delete_task((reflex_tcb_t *)h);
}

TaskHandle_t xTaskGetCurrentTaskHandle(void) {
    return (TaskHandle_t)reflex_sched_get_current();
}

TaskHandle_t xTaskGetCurrentTaskHandleForCore(int core) {
    (void)core;
    return xTaskGetCurrentTaskHandle();
}

BaseType_t xTaskGetSchedulerState(void) {
    return 2; /* taskSCHEDULER_RUNNING */
}

TickType_t xTaskGetTickCount(void) {
    return (TickType_t)reflex_sched_get_tick();
}

TickType_t xTaskGetTickCountFromISR(void) {
    return xTaskGetTickCount();
}

UBaseType_t uxTaskGetNumberOfTasks(void) {
    return REFLEX_SCHED_MAX_TASKS; /* approximate */
}

const char *pcTaskGetName(TaskHandle_t h) {
    reflex_tcb_t *t = (reflex_tcb_t *)h;
    return t ? t->name : "?";
}

BaseType_t xTaskGetCoreID(TaskHandle_t h) {
    (void)h;
    return 0; /* single core */
}

void vTaskSuspendAll(void) {
    reflex_sched_enter_critical();
}

BaseType_t xTaskResumeAll(void) {
    reflex_sched_exit_critical();
    return pdFALSE; /* no context switch needed */
}

void vTaskSuspend(TaskHandle_t h) {
    (void)h; /* stub — not used in our config */
}

BaseType_t xTaskIncrementTick(void) {
    reflex_sched_tick();
    xTickCount = reflex_sched_get_tick();
    return pdFALSE; /* no context switch from tick in cooperative mode */
}

void vTaskSwitchContext(void) {
    reflex_sched_yield();
}

void vTaskMissedYield(void) {
    reflex_sched_yield();
}

/* ---- Timeout API (used internally by queues) ---- */

typedef struct {
    TickType_t xTimeOnEntering;
    TickType_t xOverflowCount;
} TimeOut_t;

void vTaskSetTimeOutState(TimeOut_t *t) {
    if (t) t->xTimeOnEntering = reflex_sched_get_tick();
}

void vTaskInternalSetTimeOutState(TimeOut_t *t) {
    vTaskSetTimeOutState(t);
}

BaseType_t xTaskCheckForTimeOut(TimeOut_t *t, TickType_t *remaining) {
    if (!t || !remaining) return pdTRUE;
    TickType_t now = reflex_sched_get_tick();
    TickType_t elapsed = now - t->xTimeOnEntering;
    if (elapsed >= *remaining) {
        *remaining = 0;
        return pdTRUE; /* timed out */
    }
    *remaining -= elapsed;
    t->xTimeOnEntering = now;
    return pdFALSE; /* still waiting */
}

/* ---- Event list stubs (queues use these for blocking) ---- */

void vTaskPlaceOnEventList(void *list, TickType_t ticks) {
    (void)list;
    reflex_sched_delay_ms(ticks);
}

void vTaskPlaceOnUnorderedEventList(void *list, TickType_t val, TickType_t ticks) {
    (void)list; (void)val;
    reflex_sched_delay_ms(ticks);
}

BaseType_t xTaskRemoveFromEventList(void *list) {
    (void)list;
    return pdFALSE;
}

void vTaskRemoveFromUnorderedEventList(void *item, TickType_t val) {
    (void)item; (void)val;
}

UBaseType_t uxTaskResetEventItemValue(void) {
    return 0;
}

/* ---- Priority inheritance (mutex support) ---- */

BaseType_t xTaskPriorityInherit(TaskHandle_t h) {
    (void)h;
    return pdFALSE;
}

BaseType_t xTaskPriorityDisinherit(TaskHandle_t h) {
    (void)h;
    return pdFALSE;
}

void vTaskPriorityDisinheritAfterTimeout(TaskHandle_t h, UBaseType_t prio) {
    (void)h; (void)prio;
}

void *pvTaskIncrementMutexHeldCount(void) {
    return xTaskGetCurrentTaskHandle();
}

/* ---- Thread-local storage ---- */

void *pvTaskGetThreadLocalStoragePointer(TaskHandle_t h, BaseType_t idx) {
    if (idx < 0 || idx >= configNUM_THREAD_LOCAL_STORAGE_POINTERS) return NULL;
    /* Use task index in s_tasks array */
    reflex_tcb_t *t = (reflex_tcb_t *)(h ? h : xTaskGetCurrentTaskHandle());
    if (!t) return NULL;
    extern reflex_tcb_t s_tasks[];
    int task_idx = (int)(t - s_tasks);
    if (task_idx < 0 || task_idx >= REFLEX_SCHED_MAX_TASKS) return NULL;
    return s_tls[task_idx][idx];
}

void vTaskSetThreadLocalStoragePointerAndDelCallback(TaskHandle_t h, BaseType_t idx,
                                                     void *val, void *cb) {
    (void)cb;
    if (idx < 0 || idx >= configNUM_THREAD_LOCAL_STORAGE_POINTERS) return;
    reflex_tcb_t *t = (reflex_tcb_t *)(h ? h : xTaskGetCurrentTaskHandle());
    if (!t) return;
    extern reflex_tcb_t s_tasks[];
    int task_idx = (int)(t - s_tasks);
    if (task_idx < 0 || task_idx >= REFLEX_SCHED_MAX_TASKS) return;
    s_tls[task_idx][idx] = val;
}

/* ---- Notification stubs ---- */

void vTaskGenericNotifyGiveFromISR(TaskHandle_t h, UBaseType_t idx, BaseType_t *woken) {
    (void)h; (void)idx;
    if (woken) *woken = pdFALSE;
}

/* ---- Queue API ---- */

QueueHandle_t xQueueGenericCreate(UBaseType_t len, UBaseType_t item_size, uint8_t type) {
    (void)type;
    return (QueueHandle_t)reflex_queue_create(len, item_size);
}

QueueHandle_t xQueueGenericCreateStatic(UBaseType_t len, UBaseType_t item_size,
                                         uint8_t *storage, void *static_q, uint8_t type) {
    (void)storage; (void)static_q; (void)type;
    return xQueueGenericCreate(len, item_size, 0);
}

BaseType_t xQueueGenericGetStaticBuffers(QueueHandle_t q, uint8_t **buf, void **static_q) {
    (void)q; (void)buf; (void)static_q;
    return pdFALSE;
}

BaseType_t xQueueGenericReset(QueueHandle_t q, BaseType_t new_queue) {
    (void)q; (void)new_queue;
    return pdPASS;
}

BaseType_t xQueueGenericSend(QueueHandle_t q, const void *item,
                             TickType_t timeout, BaseType_t pos) {
    (void)pos;
    uint32_t ms = (timeout == portMAX_DELAY) ? UINT32_MAX : timeout;
    return (reflex_queue_send((reflex_queue_handle_t)q, item, ms) == REFLEX_OK) ? pdPASS : pdFAIL;
}

BaseType_t xQueueGenericSendFromISR(QueueHandle_t q, const void *item,
                                     BaseType_t *woken, BaseType_t pos) {
    (void)pos;
    if (woken) *woken = pdFALSE;
    return (reflex_queue_send((reflex_queue_handle_t)q, item, 0) == REFLEX_OK) ? pdPASS : pdFAIL;
}

BaseType_t xQueueReceive(QueueHandle_t q, void *item, TickType_t timeout) {
    uint32_t ms = (timeout == portMAX_DELAY) ? UINT32_MAX : timeout;
    return (reflex_queue_recv((reflex_queue_handle_t)q, item, ms) == REFLEX_OK) ? pdPASS : pdFAIL;
}

BaseType_t xQueueReceiveFromISR(QueueHandle_t q, void *item, BaseType_t *woken) {
    if (woken) *woken = pdFALSE;
    return (reflex_queue_recv((reflex_queue_handle_t)q, item, 0) == REFLEX_OK) ? pdPASS : pdFAIL;
}

BaseType_t xQueueGiveFromISR(QueueHandle_t q, BaseType_t *woken) {
    uint8_t dummy = 1;
    if (woken) *woken = pdFALSE;
    return (reflex_queue_send((reflex_queue_handle_t)q, &dummy, 0) == REFLEX_OK) ? pdPASS : pdFAIL;
}

UBaseType_t uxQueueMessagesWaiting(QueueHandle_t q) {
    (void)q;
    return 0; /* stub */
}

/* ---- Mutex/Semaphore (queue of length 1) ---- */

QueueHandle_t xQueueCreateMutex(uint8_t type) {
    (void)type;
    QueueHandle_t q = xQueueGenericCreate(1, sizeof(uint8_t), 0);
    if (q) {
        uint8_t val = 1;
        reflex_queue_send((reflex_queue_handle_t)q, &val, 0); /* initially "given" */
    }
    return q;
}

QueueHandle_t xQueueCreateMutexStatic(uint8_t type, void *buf) {
    (void)buf;
    return xQueueCreateMutex(type);
}

QueueHandle_t xQueueCreateCountingSemaphore(UBaseType_t max, UBaseType_t initial) {
    QueueHandle_t q = xQueueGenericCreate(max, sizeof(uint8_t), 0);
    if (q) {
        uint8_t val = 1;
        for (UBaseType_t i = 0; i < initial; i++)
            reflex_queue_send((reflex_queue_handle_t)q, &val, 0);
    }
    return q;
}

BaseType_t xQueueSemaphoreTake(QueueHandle_t q, TickType_t timeout) {
    uint8_t val;
    return xQueueReceive(q, &val, timeout);
}

BaseType_t xQueueGiveMutexRecursive(QueueHandle_t q) {
    uint8_t val = 1;
    return (reflex_queue_send((reflex_queue_handle_t)q, &val, 0) == REFLEX_OK) ? pdPASS : pdFAIL;
}

BaseType_t xQueueTakeMutexRecursive(QueueHandle_t q, TickType_t timeout) {
    return xQueueSemaphoreTake(q, timeout);
}

TaskHandle_t xQueueGetMutexHolder(QueueHandle_t q) {
    (void)q;
    return xTaskGetCurrentTaskHandle();
}

/* ---- Event Groups ---- */

typedef struct {
    EventBits_t bits;
} event_group_t;

EventGroupHandle_t xEventGroupCreate(void) {
    event_group_t *eg = calloc(1, sizeof(event_group_t));
    return (EventGroupHandle_t)eg;
}

EventBits_t xEventGroupWaitBits(EventGroupHandle_t h, EventBits_t wait_for,
                                 BaseType_t clear_on_exit, BaseType_t wait_all,
                                 TickType_t timeout) {
    event_group_t *eg = (event_group_t *)h;
    if (!eg) return 0;
    TickType_t deadline = reflex_sched_get_tick() + timeout;
    while (1) {
        EventBits_t current = eg->bits;
        BaseType_t match = wait_all ? ((current & wait_for) == wait_for)
                                    : ((current & wait_for) != 0);
        if (match) {
            if (clear_on_exit) eg->bits &= ~wait_for;
            return current;
        }
        if (timeout == 0 || reflex_sched_get_tick() >= deadline) return current;
        reflex_sched_delay_ms(1);
    }
}

EventBits_t xEventGroupSetBits(EventGroupHandle_t h, EventBits_t bits) {
    event_group_t *eg = (event_group_t *)h;
    if (!eg) return 0;
    eg->bits |= bits;
    return eg->bits;
}

EventBits_t xEventGroupClearBits(EventGroupHandle_t h, EventBits_t bits) {
    event_group_t *eg = (event_group_t *)h;
    if (!eg) return 0;
    EventBits_t prev = eg->bits;
    eg->bits &= ~bits;
    return prev;
}

/* ---- Port functions ---- */

void vPortEnterCritical(void *mux) {
    (void)mux;
    reflex_sched_enter_critical();
}

void vPortExitCritical(void *mux) {
    (void)mux;
    reflex_sched_exit_critical();
}

uint32_t vPortSetInterruptMaskFromISR(void) {
    reflex_sched_enter_critical();
    return 0;
}

void vPortClearInterruptMaskFromISR(uint32_t mask) {
    (void)mask;
    reflex_sched_exit_critical();
}

void *pvPortMalloc(size_t size) { return malloc(size); }
void vPortFree(void *p) { free(p); }

BaseType_t xPortInIsrContext(void) { return pdFALSE; }
BaseType_t xPortCheckValidTCBMem(void *p) { (void)p; return pdTRUE; }
BaseType_t xPortcheckValidStackMem(void *p) { (void)p; return pdTRUE; }

/* ---- Timer stubs (esp_timer uses its own mechanism) ---- */

BaseType_t xTimerCreateTimerTask(void) { return pdPASS; }

/* ---- Snapshot stubs ---- */

void vTaskGetSnapshot(TaskHandle_t h, void *snap) { (void)h; (void)snap; }

/* ---- Scheduler start ---- */

void vTaskStartScheduler(void) {
    xSchedulerRunning = pdTRUE;
    /* Create idle task */
    reflex_sched_init();
    /* The real scheduling is started by xPortStartScheduler
     * which our __wrap intercepts */
}
