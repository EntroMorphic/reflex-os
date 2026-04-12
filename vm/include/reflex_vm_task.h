#ifndef REFLEX_VM_TASK_H
#define REFLEX_VM_TASK_H

#include <stdbool.h>
#include <stdint.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_err.h"

#include "reflex_vm_loader.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    const char *name;
    uint32_t steps_per_slice;
    uint32_t delay_ms;
    uint32_t stack_size;
    UBaseType_t priority;
} reflex_vm_task_config_t;

typedef struct {
    reflex_vm_state_t vm;
    TaskHandle_t handle;
    const reflex_vm_image_t *image;
    reflex_vm_task_config_t config;
    bool running;
} reflex_vm_task_runtime_t;

void reflex_vm_task_runtime_init(reflex_vm_task_runtime_t *runtime);
esp_err_t reflex_vm_task_start(reflex_vm_task_runtime_t *runtime,
                               const reflex_vm_image_t *image,
                               const reflex_vm_task_config_t *config);
esp_err_t reflex_vm_task_stop(reflex_vm_task_runtime_t *runtime);
bool reflex_vm_task_is_running(const reflex_vm_task_runtime_t *runtime);
esp_err_t reflex_vm_task_self_check(void);

#ifdef __cplusplus
}
#endif

#endif
