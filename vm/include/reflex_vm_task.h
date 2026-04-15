#ifndef REFLEX_VM_TASK_H
#define REFLEX_VM_TASK_H

/**
 * @file reflex_vm_task.h
 * @brief FreeRTOS task runtime that hosts a reflex_vm_state_t so
 * ternary programs can run as background services.
 *
 * A reflex_vm_task_runtime_t owns one VM instance plus a FreeRTOS
 * task that steps it in bounded slices (`steps_per_slice`) with a
 * configurable inter-slice `delay_ms`. The slice model exists so a
 * long-running VM program can't starve the supervisor pulse, the
 * atmospheric mesh RX callback, or the serial shell task.
 *
 * The service_* entry points implement the reflex_service_desc_t
 * contract so a VM task can be registered with the host service
 * manager like any other service (see core/service_manager.c).
 */

#include <stdbool.h>
#include <stdint.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_err.h"

#include "reflex_service.h"
#include "reflex_vm_loader.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Per-task scheduling and runtime parameters.
 *
 * @var name            FreeRTOS task name (and log tag).
 * @var steps_per_slice Maximum VM instructions per scheduling slice.
 * @var delay_ms        vTaskDelay between slices — yields the CPU.
 * @var stack_size      FreeRTOS stack in bytes.
 * @var priority        FreeRTOS task priority.
 */
typedef struct {
    const char *name;
    uint32_t steps_per_slice;
    uint32_t delay_ms;
    uint32_t stack_size;
    UBaseType_t priority;
} reflex_vm_task_config_t;

/**
 * @brief Container for an owned VM instance plus its hosting task.
 *
 * A single runtime struct is enough for one background ternary
 * program. Multi-VM hosting would instantiate several of these.
 */
typedef struct {
    reflex_vm_state_t vm;
    TaskHandle_t handle;
    const reflex_vm_image_t *image;
    reflex_vm_task_config_t config;
    bool running;
} reflex_vm_task_runtime_t;

/**
 * @brief Zero-initialize the runtime and install default syscalls.
 * Must be called before the VM instance is stepped.
 */
void reflex_vm_task_runtime_init(reflex_vm_task_runtime_t *runtime);

/**
 * @brief Start the hosting task with a pre-parsed VM image.
 * The image memory must outlive the task.
 */
esp_err_t reflex_vm_task_start(reflex_vm_task_runtime_t *runtime,
                               const reflex_vm_image_t *image,
                               const reflex_vm_task_config_t *config);

/**
 * @brief Start the hosting task from a raw packed binary buffer.
 * The loader parses the buffer; image lifetime is tied to the runtime.
 */
esp_err_t reflex_vm_task_start_binary(reflex_vm_task_runtime_t *runtime,
                                      const uint8_t *buffer,
                                      size_t len,
                                      const reflex_vm_task_config_t *config);

/** @brief Signal the hosting task to stop cleanly and join it. */
esp_err_t reflex_vm_task_stop(reflex_vm_task_runtime_t *runtime);

/** @brief Returns true between start and stop. */
bool reflex_vm_task_is_running(const reflex_vm_task_runtime_t *runtime);

/** @brief reflex_service_desc_t-compatible init hook. */
esp_err_t reflex_vm_task_service_init(void *ctx);
/** @brief reflex_service_desc_t-compatible start hook. */
esp_err_t reflex_vm_task_service_start(void *ctx);
/** @brief reflex_service_desc_t-compatible stop hook. */
esp_err_t reflex_vm_task_service_stop(void *ctx);
/** @brief reflex_service_desc_t-compatible status query. */
reflex_service_status_t reflex_vm_task_service_status(void *ctx);

/** @brief Boot-time self-check. */
esp_err_t reflex_vm_task_self_check(void);

/**
 * @brief Register this runtime with the service manager under @p name.
 * Used by main.c at boot to expose the default system VM.
 */
esp_err_t reflex_vm_task_register_service(reflex_vm_task_runtime_t *runtime, const char *name);

#ifdef __cplusplus
}
#endif

#endif
