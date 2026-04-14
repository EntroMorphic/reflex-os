#ifndef REFLEX_VM_H
#define REFLEX_VM_H

#include <stdint.h>

#include "esp_err.h"

#include "reflex_vm_state.h"
#include "reflex_vm_task.h"

#ifdef __cplusplus
extern "C" {
#endif

void reflex_vm_reset(reflex_vm_state_t *vm);
void reflex_vm_unload(reflex_vm_state_t *vm);
void reflex_vm_set_syscall_handler(reflex_vm_state_t *vm,
                                   reflex_vm_syscall_handler_t handler,
                                   void *context);
void reflex_vm_use_default_syscalls(reflex_vm_state_t *vm);
esp_err_t reflex_vm_step(reflex_vm_state_t *vm);
esp_err_t reflex_vm_run(reflex_vm_state_t *vm, uint32_t max_steps);
esp_err_t reflex_vm_self_check(void);

#ifdef __cplusplus
}
#endif

#endif
