#include "reflex_vm.h"

#include <stdio.h>
#include <string.h>

#include "esp_check.h"
#include "esp_timer.h"

static const char *REFLEX_VM_DEVICE_NAME = "reflex-os";
static const int32_t REFLEX_VM_DEFAULT_LOG_LEVEL = 2;

static esp_err_t reflex_vm_default_syscall_handler(reflex_vm_state_t *vm,
                                                   reflex_vm_syscall_t syscall_id,
                                                   const reflex_word18_t *src_a,
                                                   const reflex_word18_t *src_b,
                                                   reflex_word18_t *out,
                                                   void *context)
{
    int32_t scalar = 0;

    (void)vm;
    (void)src_b;
    (void)context;

    ESP_RETURN_ON_FALSE(out != NULL, ESP_ERR_INVALID_ARG, "vm_syscall", "output is required");

    switch (syscall_id) {
    case REFLEX_VM_SYSCALL_LOG:
        ESP_RETURN_ON_ERROR(reflex_word18_to_int32(src_a, &scalar), "vm_syscall", "log input invalid");
        printf("vm log value=%ld\n", (long)scalar);
        return reflex_word18_from_int32(0, out);
    case REFLEX_VM_SYSCALL_UPTIME:
        return reflex_word18_from_int32((int32_t)(esp_timer_get_time() / 1000), out);
    case REFLEX_VM_SYSCALL_CONFIG_GET:
        ESP_RETURN_ON_ERROR(reflex_word18_to_int32(src_a, &scalar), "vm_syscall", "config key invalid");
        if (scalar == REFLEX_VM_CONFIG_DEVICE_NAME_LENGTH) {
            return reflex_word18_from_int32((int32_t)strlen(REFLEX_VM_DEVICE_NAME), out);
        }
        if (scalar == REFLEX_VM_CONFIG_LOG_LEVEL) {
            return reflex_word18_from_int32(REFLEX_VM_DEFAULT_LOG_LEVEL, out);
        }
        return ESP_ERR_NOT_SUPPORTED;
    default:
        return ESP_ERR_NOT_SUPPORTED;
    }
}

void reflex_vm_set_syscall_handler(reflex_vm_state_t *vm,
                                   reflex_vm_syscall_handler_t handler,
                                   void *context)
{
    if (vm == NULL) {
        return;
    }

    vm->syscall_handler = handler;
    vm->syscall_context = context;
}

void reflex_vm_use_default_syscalls(reflex_vm_state_t *vm)
{
    reflex_vm_set_syscall_handler(vm, reflex_vm_default_syscall_handler, NULL);
}
