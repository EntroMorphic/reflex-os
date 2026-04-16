#include "reflex_hal.h"
#include "reflex_task.h"
#include "driver/temperature_sensor.h"
/**
 * @file temp_service.c
 * @brief Temperature sensor service — first real perception on the fabric.
 *
 * Uses the ESP32-C6 internal temperature sensor (no external wiring) to
 * read die temperature and project it into the GOOSE Tapestry as a
 * perception cell addressable by name.
 *
 * The cell `perception.temp.reading` is allocated at service init and
 * updated every 5 seconds by a background FreeRTOS task. The ternary
 * state is mapped as: cold (<40C) = -1, normal (40-55C) = 0, warm (>55C) = +1.
 * Thresholds are calibrated for die temperature (the C6 die runs at
 * ~50-55C under normal USB operation). The raw float reading is tracked
 * separately for the shell's `temp` command.
 */

#include "goose.h"
#include "reflex_log.h"
#include "reflex_service.h"



#include <stdio.h>

#define TEMP_TAG "reflex.temp"
#define TEMP_POLL_MS 5000
/* Die-temperature thresholds. The C6 die runs at ~50-55C under normal
 * USB operation, so the ambient-sensor defaults (25/35) would pin the
 * state permanently at +1. These are calibrated for die reality. */
#define TEMP_COLD_THRESHOLD  40.0f
#define TEMP_WARM_THRESHOLD  55.0f

static temperature_sensor_handle_t s_temp_handle = NULL;
static goose_cell_t *s_temp_cell = NULL;
static volatile float s_last_reading = 0.0f;
static reflex_task_handle_t s_task_handle = NULL;

float reflex_temp_get_celsius(void) { return s_last_reading; }

static void temp_poll_task(void *arg) {
    (void)arg;
    while (1) {
        float celsius;
        if (temperature_sensor_get_celsius(s_temp_handle, &celsius) == REFLEX_OK) {
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
    temperature_sensor_config_t cfg = TEMPERATURE_SENSOR_CONFIG_DEFAULT(10, 50);
    reflex_err_t rc = temperature_sensor_install(&cfg, &s_temp_handle);
    if (rc != REFLEX_OK) {
        REFLEX_LOGE(TEMP_TAG, "temp sensor install failed rc=0x%x", rc);
        return rc;
    }
    rc = temperature_sensor_enable(s_temp_handle);
    if (rc != REFLEX_OK) {
        REFLEX_LOGE(TEMP_TAG, "temp sensor enable failed rc=0x%x", rc);
        return rc;
    }

    /* The internal temp sensor is a driver API, not an MMIO register —
     * it has no hardware address to bind via set_agency. Allocate the
     * cell as system-pinned (is_system_weaving=true → GOOSE_CELL_PINNED,
     * never evicted) and leave hardware_addr=0. The temp_poll_task
     * writes cell state directly. */
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
