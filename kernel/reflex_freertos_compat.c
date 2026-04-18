/**
 * @file reflex_freertos_compat.c
 * @brief FreeRTOS API → Reflex kernel scheduler (clean rewrite).
 *
 * Every function is __wrap_<name>. The --wrap linker flag redirects
 * ALL calls to the original FreeRTOS function to our implementation.
 * Internal references use reflex_sched_* directly.
 */

#include "reflex_sched.h"
#include "reflex_task.h"
#include <stdlib.h>
#include <string.h>

typedef void *TaskHandle_t;
typedef void *QueueHandle_t;
typedef void *EventGroupHandle_t;
typedef int BaseType_t;
typedef unsigned int UBaseType_t;
typedef uint32_t TickType_t;
typedef uint32_t EventBits_t;
typedef void (*TaskFunction_t)(void *);
typedef struct { TickType_t xTimeOnEntering; TickType_t xOverflowCount; } TimeOut_t;

#define pdPASS 1
#define pdFAIL 0
#define pdTRUE 1
#define pdFALSE 0
#define portMAX_DELAY 0xFFFFFFFF

volatile TickType_t xTickCount = 0;
volatile BaseType_t xSchedulerRunning = pdFALSE;

#define TLS_SLOTS 2
static void *s_tls[REFLEX_SCHED_MAX_TASKS][TLS_SLOTS];
extern reflex_tcb_t s_tasks[];

static int tls_idx(void *h) {
    reflex_tcb_t *t = h ? (reflex_tcb_t *)h : reflex_sched_get_current();
    if (!t) return -1;
    int i = (int)(t - s_tasks);
    return (i >= 0 && i < REFLEX_SCHED_MAX_TASKS) ? i : -1;
}

typedef struct { EventBits_t bits; } evg_t;

/* === TASKS === */

static bool s_sched_inited = false;
static void ensure_sched_init(void) {
    if (!s_sched_inited) { reflex_sched_init(); s_sched_inited = true; }
}

BaseType_t __wrap_xTaskCreatePinnedToCore(TaskFunction_t fn, const char *name,
    uint32_t stk, void *arg, UBaseType_t prio, TaskHandle_t *out, int core) {
    (void)core;
    ensure_sched_init();
    reflex_tcb_t *tcb = NULL;
    if (reflex_sched_create_task(fn, name, stk, arg, (int)prio, &tcb) != REFLEX_OK) return pdFAIL;
    if (out) *out = (TaskHandle_t)tcb;
    return pdPASS;
}
BaseType_t __wrap_xTaskCreateStaticPinnedToCore(TaskFunction_t fn, const char *name,
    uint32_t stk, void *arg, UBaseType_t prio, void *sb, void *tb, TaskHandle_t *out, int core) {
    (void)sb; (void)tb;
    return __wrap_xTaskCreatePinnedToCore(fn, name, stk, arg, prio, out, core);
}
void __wrap_vTaskDelay(TickType_t t)                  { reflex_sched_delay_ms(t); }
void __wrap_vTaskDelete(TaskHandle_t h)               { reflex_sched_delete_task((reflex_tcb_t *)h); }
void __wrap_vTaskSuspend(TaskHandle_t h)              { (void)h; }
void __wrap_vTaskSuspendAll(void)                     { reflex_sched_enter_critical(); }
BaseType_t __wrap_xTaskResumeAll(void)                { reflex_sched_exit_critical(); return pdFALSE; }
void __wrap_vTaskSwitchContext(void)                  { reflex_sched_yield(); }
void __wrap_vTaskMissedYield(void)                    { reflex_sched_yield(); }
TaskHandle_t __wrap_xTaskGetCurrentTaskHandle(void)   { return (TaskHandle_t)reflex_sched_get_current(); }
TaskHandle_t __wrap_xTaskGetCurrentTaskHandleForCore(int c) { (void)c; return (TaskHandle_t)reflex_sched_get_current(); }
BaseType_t __wrap_xTaskGetSchedulerState(void)        { return 2; }
TickType_t __wrap_xTaskGetTickCount(void)             { return (TickType_t)reflex_sched_get_tick(); }
TickType_t __wrap_xTaskGetTickCountFromISR(void)      { return (TickType_t)reflex_sched_get_tick(); }
UBaseType_t __wrap_uxTaskGetNumberOfTasks(void)       { return REFLEX_SCHED_MAX_TASKS; }
BaseType_t __wrap_xTaskGetCoreID(TaskHandle_t h)      { (void)h; return 0; }
const char *__wrap_pcTaskGetName(TaskHandle_t h)      { reflex_tcb_t *t = (reflex_tcb_t *)h; return t ? t->name : "?"; }
BaseType_t __wrap_xTaskIncrementTick(void)            { reflex_sched_tick(); xTickCount = reflex_sched_get_tick(); return pdFALSE; }

/* === TIMEOUT === */

void __wrap_vTaskSetTimeOutState(TimeOut_t *t)          { if (t) t->xTimeOnEntering = reflex_sched_get_tick(); }
void __wrap_vTaskInternalSetTimeOutState(TimeOut_t *t)  { if (t) t->xTimeOnEntering = reflex_sched_get_tick(); }
BaseType_t __wrap_xTaskCheckForTimeOut(TimeOut_t *t, TickType_t *rem) {
    if (!t || !rem) return pdTRUE;
    TickType_t el = reflex_sched_get_tick() - t->xTimeOnEntering;
    if (el >= *rem) { *rem = 0; return pdTRUE; }
    *rem -= el; t->xTimeOnEntering = reflex_sched_get_tick(); return pdFALSE;
}

/* === EVENT LIST STUBS === */

void __wrap_vTaskPlaceOnEventList(void *l, TickType_t t)                     { (void)l; reflex_sched_delay_ms(t); }
void __wrap_vTaskPlaceOnUnorderedEventList(void *l, TickType_t v, TickType_t t) { (void)l; (void)v; reflex_sched_delay_ms(t); }
BaseType_t __wrap_xTaskRemoveFromEventList(void *l)                          { (void)l; return pdFALSE; }
void __wrap_vTaskRemoveFromUnorderedEventList(void *i, TickType_t v)         { (void)i; (void)v; }
UBaseType_t __wrap_uxTaskResetEventItemValue(void)                           { return 0; }

/* === PRIORITY INHERITANCE === */

BaseType_t __wrap_xTaskPriorityInherit(TaskHandle_t h)      { (void)h; return pdFALSE; }
BaseType_t __wrap_xTaskPriorityDisinherit(TaskHandle_t h)   { (void)h; return pdFALSE; }
void __wrap_vTaskPriorityDisinheritAfterTimeout(TaskHandle_t h, UBaseType_t p) { (void)h; (void)p; }
void *__wrap_pvTaskIncrementMutexHeldCount(void)            { return (void *)reflex_sched_get_current(); }

/* === TLS === */

void *__wrap_pvTaskGetThreadLocalStoragePointer(TaskHandle_t h, BaseType_t idx) {
    int ti = tls_idx(h); return (ti >= 0 && idx >= 0 && idx < TLS_SLOTS) ? s_tls[ti][idx] : NULL;
}
void __wrap_vTaskSetThreadLocalStoragePointerAndDelCallback(TaskHandle_t h, BaseType_t idx, void *val, void *cb) {
    (void)cb; int ti = tls_idx(h); if (ti >= 0 && idx >= 0 && idx < TLS_SLOTS) s_tls[ti][idx] = val;
}

/* === NOTIFICATIONS === */

void __wrap_vTaskGenericNotifyGiveFromISR(TaskHandle_t h, UBaseType_t i, BaseType_t *w) { (void)h; (void)i; if (w) *w = pdFALSE; }

/* === QUEUES === */

QueueHandle_t __wrap_xQueueGenericCreate(UBaseType_t len, UBaseType_t sz, uint8_t t) {
    (void)t; return (QueueHandle_t)reflex_queue_create(len, sz);
}
QueueHandle_t __wrap_xQueueGenericCreateStatic(UBaseType_t len, UBaseType_t sz, uint8_t *s, void *q, uint8_t t) {
    (void)s; (void)q; (void)t; return (QueueHandle_t)reflex_queue_create(len, sz);
}
BaseType_t __wrap_xQueueGenericGetStaticBuffers(QueueHandle_t q, uint8_t **b, void **s) { (void)q; (void)b; (void)s; return pdFALSE; }
BaseType_t __wrap_xQueueGenericReset(QueueHandle_t q, BaseType_t n) { (void)q; (void)n; return pdPASS; }

BaseType_t __wrap_xQueueGenericSend(QueueHandle_t q, const void *item, TickType_t to, BaseType_t pos) {
    (void)pos; uint32_t ms = (to == portMAX_DELAY) ? UINT32_MAX : to;
    return (reflex_queue_send((reflex_queue_handle_t)q, item, ms) == REFLEX_OK) ? pdPASS : pdFAIL;
}
BaseType_t __wrap_xQueueGenericSendFromISR(QueueHandle_t q, const void *item, BaseType_t *w, BaseType_t p) {
    (void)p; if (w) *w = pdFALSE;
    return (reflex_queue_send((reflex_queue_handle_t)q, item, 0) == REFLEX_OK) ? pdPASS : pdFAIL;
}
BaseType_t __wrap_xQueueReceive(QueueHandle_t q, void *item, TickType_t to) {
    uint32_t ms = (to == portMAX_DELAY) ? UINT32_MAX : to;
    return (reflex_queue_recv((reflex_queue_handle_t)q, item, ms) == REFLEX_OK) ? pdPASS : pdFAIL;
}
BaseType_t __wrap_xQueueReceiveFromISR(QueueHandle_t q, void *item, BaseType_t *w) {
    if (w) *w = pdFALSE;
    return (reflex_queue_recv((reflex_queue_handle_t)q, item, 0) == REFLEX_OK) ? pdPASS : pdFAIL;
}
BaseType_t __wrap_xQueueGiveFromISR(QueueHandle_t q, BaseType_t *w) {
    uint8_t d = 1; if (w) *w = pdFALSE;
    return (reflex_queue_send((reflex_queue_handle_t)q, &d, 0) == REFLEX_OK) ? pdPASS : pdFAIL;
}
UBaseType_t __wrap_uxQueueMessagesWaiting(QueueHandle_t q)  { (void)q; return 0; }
TaskHandle_t __wrap_xQueueGetMutexHolder(QueueHandle_t q)   { (void)q; return (TaskHandle_t)reflex_sched_get_current(); }

/* === MUTEX / SEMAPHORE === */

QueueHandle_t __wrap_xQueueCreateMutex(uint8_t t) {
    (void)t; QueueHandle_t q = (QueueHandle_t)reflex_queue_create(1, sizeof(uint8_t));
    if (q) { uint8_t v = 1; reflex_queue_send((reflex_queue_handle_t)q, &v, 0); }
    return q;
}
QueueHandle_t __wrap_xQueueCreateMutexStatic(uint8_t t, void *b) { (void)b; return __wrap_xQueueCreateMutex(t); }
QueueHandle_t __wrap_xQueueCreateCountingSemaphore(UBaseType_t max, UBaseType_t init) {
    QueueHandle_t q = (QueueHandle_t)reflex_queue_create(max, sizeof(uint8_t));
    if (q) { uint8_t v = 1; for (UBaseType_t i = 0; i < init; i++) reflex_queue_send((reflex_queue_handle_t)q, &v, 0); }
    return q;
}
BaseType_t __wrap_xQueueSemaphoreTake(QueueHandle_t q, TickType_t to) {
    uint8_t v; uint32_t ms = (to == portMAX_DELAY) ? UINT32_MAX : to;
    return (reflex_queue_recv((reflex_queue_handle_t)q, &v, ms) == REFLEX_OK) ? pdPASS : pdFAIL;
}
BaseType_t __wrap_xQueueGiveMutexRecursive(QueueHandle_t q) {
    uint8_t v = 1; return (reflex_queue_send((reflex_queue_handle_t)q, &v, 0) == REFLEX_OK) ? pdPASS : pdFAIL;
}
BaseType_t __wrap_xQueueTakeMutexRecursive(QueueHandle_t q, TickType_t to) {
    return __wrap_xQueueSemaphoreTake(q, to);
}

/* === EVENT GROUPS === */

EventGroupHandle_t __wrap_xEventGroupCreate(void) { return (EventGroupHandle_t)calloc(1, sizeof(evg_t)); }
EventBits_t __wrap_xEventGroupWaitBits(EventGroupHandle_t h, EventBits_t wait, BaseType_t clr, BaseType_t all, TickType_t to) {
    evg_t *eg = (evg_t *)h; if (!eg) return 0;
    TickType_t dl = reflex_sched_get_tick() + to;
    while (1) {
        EventBits_t c = eg->bits;
        BaseType_t ok = all ? ((c & wait) == wait) : ((c & wait) != 0);
        if (ok) { if (clr) eg->bits &= ~wait; return c; }
        if (to == 0 || reflex_sched_get_tick() >= dl) return c;
        reflex_sched_delay_ms(1);
    }
}
EventBits_t __wrap_xEventGroupSetBits(EventGroupHandle_t h, EventBits_t b) { evg_t *eg = (evg_t *)h; if (!eg) return 0; eg->bits |= b; return eg->bits; }
EventBits_t __wrap_xEventGroupClearBits(EventGroupHandle_t h, EventBits_t b) { evg_t *eg = (evg_t *)h; if (!eg) return 0; EventBits_t p = eg->bits; eg->bits &= ~b; return p; }

/* === PORT === */

void __wrap_vPortEnterCritical(void *m)                    { (void)m; reflex_sched_enter_critical(); }
void __wrap_vPortExitCritical(void *m)                     { (void)m; reflex_sched_exit_critical(); }
uint32_t __wrap_vPortSetInterruptMaskFromISR(void)         { reflex_sched_enter_critical(); return 0; }
void __wrap_vPortClearInterruptMaskFromISR(uint32_t m)     { (void)m; reflex_sched_exit_critical(); }
void *__wrap_pvPortMalloc(size_t s)                        { return malloc(s); }
void __wrap_vPortFree(void *p)                             { free(p); }
BaseType_t __wrap_xPortInIsrContext(void)                  { return pdFALSE; }
BaseType_t __wrap_xPortCheckValidTCBMem(void *p)           { (void)p; return pdTRUE; }
BaseType_t __wrap_xPortcheckValidStackMem(void *p)         { (void)p; return pdTRUE; }

/* === SCHEDULER START === */

void __wrap_vTaskStartScheduler(void) {
    ensure_sched_init();
    xSchedulerRunning = pdTRUE;
}
void __wrap_vTaskGetSnapshot(TaskHandle_t h, void *s) { (void)h; (void)s; }
