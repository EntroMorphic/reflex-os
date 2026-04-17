/**
 * @file temp_service.c
 * @brief Temperature sensor service — first real perception on the fabric.
 */

#include "reflex_hal.h"
#include "reflex_task.h"
#include "goose.h"
#include "reflex_log.h"
#include "reflex_service.h"

#define TEMP_TAG "reflex.temp"
#define TEMP_POLL_MS 5000
#define TEMP_COLD_THRESHOLD  40.0f
#define TEMP_WARM_THRESHOLD  55.0f

static reflex_temp_handle_t s_temp_handle = NULL;
static goose_cell_t *s_temp_cell = NULL;
static volatile float s_last_reading = 0.0f;
static reflex_task_handle_t s_task_handle = NULL;

float reflex_temp_get_celsius(void) { return s_last_reading; }

static void temp_poll_task(void *arg) {
    (void)arg;
    while (1) {
        float celsius;
        if (reflex_hal_temp_read(s_temp_handle, &celsius) == REFLEX_OK) {
            s_last_reading = celsius;
            if (s_temp_cell) {
                if (celsius < TEMP_COLD_THRESHOLD)       s_temp_cell->state = -1;
                else if (celsius > TEMP_WARM_THRESHOLD)   s_temp_cell->state =  1;
                else                                      s_temp_cell->state =  0;
            }
        }
        reflex_task_delay_ms(TEMP_POLL_MS);
    }
}

static reflex_err_t temp_service_init(void *ctx) {
    (void)ctx;
    reflex_err_t rc = reflex_hal_temp_init(&s_temp_handle);
    if (rc != REFLEX_OK) {
        REFLEX_LOGE(TEMP_TAG, "temp sensor init failed rc=0x%x", (unsigned)rc);
        return rc;
    }
    reflex_tryte9_t coord = goose_make_coord(-1, 4, 0);
    s_temp_cell = goose_fabric_alloc_cell("perception.temp.reading", coord, true);
    return REFLEX_OK;
}

static reflex_err_t temp_service_start(void *ctx) {
    (void)ctx;
    if (!s_temp_handle) return REFLEX_ERR_INVALID_STATE;
    reflex_task_create(temp_poll_task, "temp-poll", 2048, NULL, 5, &s_task_handle);
    return REFLEX_OK;
}

static reflex_err_t temp_service_stop(void *ctx) {
    (void)ctx;
    if (s_task_handle) { reflex_task_delete(s_task_handle); s_task_handle = NULL; }
    return REFLEX_OK;
}

static reflex_service_status_t temp_service_status(void *ctx) {
    (void)ctx;
    return s_task_handle ? REFLEX_SERVICE_STATUS_STARTED : REFLEX_SERVICE_STATUS_STOPPED;
}

reflex_err_t reflex_temp_service_register(void) {
    static reflex_service_desc_t desc = {
        .name = "temp",
        .init = temp_service_init,
        .start = temp_service_start,
        .stop = temp_service_stop,
        .status = temp_service_status,
    };
    return reflex_service_register(&desc);
}
