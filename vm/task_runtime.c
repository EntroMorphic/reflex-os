#include "reflex_vm_task.h"

#include <string.h>

#include "esp_check.h"

#include "reflex_vm.h"

#define REFLEX_VM_TASK_DEFAULT_STEPS 2
#define REFLEX_VM_TASK_DEFAULT_DELAY_MS 20
#define REFLEX_VM_TASK_DEFAULT_STACK 4096
#define REFLEX_VM_TASK_DEFAULT_PRIORITY 5

static void reflex_vm_task_apply_defaults(reflex_vm_task_config_t *config)
{
    if (config->steps_per_slice == 0) {
        config->steps_per_slice = REFLEX_VM_TASK_DEFAULT_STEPS;
    }
    if (config->delay_ms == 0) {
        config->delay_ms = REFLEX_VM_TASK_DEFAULT_DELAY_MS;
    }
    if (config->stack_size == 0) {
        config->stack_size = REFLEX_VM_TASK_DEFAULT_STACK;
    }
    if (config->priority == 0) {
        config->priority = REFLEX_VM_TASK_DEFAULT_PRIORITY;
    }
    if (config->name == NULL) {
        config->name = "reflex-vm";
    }
}

static void reflex_vm_task_entry(void *arg)
{
    reflex_vm_task_runtime_t *runtime = (reflex_vm_task_runtime_t *)arg;

    while (runtime->running) {
        if (runtime->vm.status == REFLEX_VM_STATUS_HALTED || runtime->vm.status == REFLEX_VM_STATUS_FAULTED) {
            runtime->running = false;
            break;
        }

        if (reflex_vm_run(&runtime->vm, runtime->config.steps_per_slice) == ESP_ERR_TIMEOUT) {
            vTaskDelay(pdMS_TO_TICKS(runtime->config.delay_ms));
            continue;
        }

        if (runtime->vm.status == REFLEX_VM_STATUS_READY) {
            vTaskDelay(pdMS_TO_TICKS(runtime->config.delay_ms));
        }
    }

    runtime->handle = NULL;
    vTaskDelete(NULL);
}

void reflex_vm_task_runtime_init(reflex_vm_task_runtime_t *runtime)
{
    if (runtime == NULL) {
        return;
    }

    memset(runtime, 0, sizeof(*runtime));
}

esp_err_t reflex_vm_task_start(reflex_vm_task_runtime_t *runtime,
                               const reflex_vm_image_t *image,
                               const reflex_vm_task_config_t *config)
{
    reflex_vm_task_config_t effective_config;

    ESP_RETURN_ON_FALSE(runtime != NULL, ESP_ERR_INVALID_ARG, "vm_task", "runtime is required");
    ESP_RETURN_ON_FALSE(image != NULL, ESP_ERR_INVALID_ARG, "vm_task", "image is required");
    ESP_RETURN_ON_FALSE(!runtime->running, ESP_ERR_INVALID_STATE, "vm_task", "runtime already running");
    ESP_RETURN_ON_ERROR(reflex_vm_validate_image(image), "vm_task", "image validation failed");

    memset(&effective_config, 0, sizeof(effective_config));
    if (config != NULL) {
        effective_config = *config;
    }
    reflex_vm_task_apply_defaults(&effective_config);

    runtime->image = image;
    runtime->config = effective_config;
    runtime->running = true;
    reflex_vm_use_default_syscalls(&runtime->vm);
    ESP_RETURN_ON_ERROR(reflex_vm_load_image(&runtime->vm, image), "vm_task", "image load failed");

    if (xTaskCreate(reflex_vm_task_entry,
                    effective_config.name,
                    effective_config.stack_size,
                    runtime,
                    effective_config.priority,
                    &runtime->handle) != pdPASS) {
        runtime->running = false;
        runtime->handle = NULL;
        return ESP_FAIL;
    }

    return ESP_OK;
}

esp_err_t reflex_vm_task_stop(reflex_vm_task_runtime_t *runtime)
{
    ESP_RETURN_ON_FALSE(runtime != NULL, ESP_ERR_INVALID_ARG, "vm_task", "runtime is required");

    runtime->running = false;
    if (runtime->handle != NULL) {
        while (runtime->handle != NULL) {
            vTaskDelay(pdMS_TO_TICKS(10));
        }
    }

    return ESP_OK;
}

bool reflex_vm_task_is_running(const reflex_vm_task_runtime_t *runtime)
{
    return runtime != NULL && runtime->running;
}

esp_err_t reflex_vm_task_self_check(void)
{
    static const reflex_vm_instruction_t program[] = {
        {.opcode = REFLEX_VM_OPCODE_TLDI, .dst = 0, .imm = 1},
        {.opcode = REFLEX_VM_OPCODE_TLDI, .dst = 1, .imm = -1},
        {.opcode = REFLEX_VM_OPCODE_TADD, .dst = 2, .src_a = 0, .src_b = 0},
        {.opcode = REFLEX_VM_OPCODE_TCMP, .dst = 3, .src_a = 2, .src_b = 0},
        {.opcode = REFLEX_VM_OPCODE_TBRPOS, .src_a = 3, .imm = 6},
        {.opcode = REFLEX_VM_OPCODE_THALT},
        {.opcode = REFLEX_VM_OPCODE_TSUB, .dst = 4, .src_a = 2, .src_b = 0},
        {.opcode = REFLEX_VM_OPCODE_THALT},
    };
    static const reflex_vm_image_t image = {
        .magic = REFLEX_VM_IMAGE_MAGIC,
        .version = REFLEX_VM_IMAGE_VERSION,
        .entry_ip = 0,
        .instructions = program,
        .instruction_count = sizeof(program) / sizeof(program[0]),
    };
    static reflex_vm_task_runtime_t runtime;
    static reflex_word18_t expected_one;
    uint32_t deadline = 100;

    reflex_vm_task_runtime_init(&runtime);
    ESP_RETURN_ON_ERROR(reflex_word18_from_int32(1, &expected_one), "vm_task", "failed to encode expected value");
    ESP_RETURN_ON_ERROR(reflex_vm_task_start(&runtime, &image, NULL), "vm_task", "failed to start runtime");

    while (deadline-- > 0 && reflex_vm_task_is_running(&runtime)) {
        vTaskDelay(pdMS_TO_TICKS(10));
    }

    ESP_RETURN_ON_FALSE(runtime.vm.status == REFLEX_VM_STATUS_HALTED,
                        ESP_FAIL,
                        "vm_task",
                        "runtime must halt cleanly");
    ESP_RETURN_ON_FALSE(reflex_word18_equal(&runtime.vm.registers[4], &expected_one),
                        ESP_FAIL,
                        "vm_task",
                        "runtime result register mismatch");

    return reflex_vm_task_stop(&runtime);
}
