/** @file syscall.c
 * @brief VM syscall dispatch handler.
 */
#include "reflex_vm.h"

#include <stdio.h>
#include <string.h>

#include "reflex_hal.h"
#include "reflex_task.h"

/* Phase 2 boundary: reflex_config.h still uses esp_err_t. Values are
 * compatible (both int, matching codes) so the cast is safe today.
 * This include migrates when storage/ moves to reflex_types.h. */
#include "reflex_config.h"

static reflex_err_t reflex_vm_default_syscall_handler(reflex_vm_state_t *vm,
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

    REFLEX_RETURN_ON_FALSE(out != NULL, REFLEX_ERR_INVALID_ARG, "vm_syscall", "output is required");

    switch (syscall_id) {
    case REFLEX_VM_SYSCALL_LOG:
        REFLEX_RETURN_ON_ERROR(reflex_word18_to_int32(src_a, &scalar), "vm_syscall", "log input invalid");
        printf("vm log value=%ld\n", (long)scalar);
        return reflex_word18_from_int32(0, out);
    case REFLEX_VM_SYSCALL_UPTIME:
        return reflex_word18_from_int32((int32_t)(reflex_hal_time_us() / 1000), out);
    case REFLEX_VM_SYSCALL_CONFIG_GET:
        REFLEX_RETURN_ON_ERROR(reflex_word18_to_int32(src_a, &scalar), "vm_syscall", "config key invalid");
        if (scalar == REFLEX_VM_CONFIG_DEVICE_NAME_LENGTH) {
            char device_name[32];
            REFLEX_RETURN_ON_ERROR(reflex_config_get_device_name(device_name, sizeof(device_name)),
                                "vm_syscall",
                                "device name read failed");
            return reflex_word18_from_int32((int32_t)strlen(device_name), out);
        }
        if (scalar == REFLEX_VM_CONFIG_LOG_LEVEL) {
            int32_t log_level = 0;
            REFLEX_RETURN_ON_ERROR(reflex_config_get_log_level(&log_level),
                                "vm_syscall",
                                "log level read failed");
            return reflex_word18_from_int32(log_level, out);
        }
        return REFLEX_ERR_NOT_SUPPORTED;
    case REFLEX_VM_SYSCALL_DELAY:
        REFLEX_RETURN_ON_ERROR(reflex_word18_to_int32(src_a, &scalar), "vm_syscall", "delay input invalid");
        reflex_task_delay_ms(scalar);
        return reflex_word18_from_int32(0, out);
    default:
        return REFLEX_ERR_NOT_SUPPORTED;
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
