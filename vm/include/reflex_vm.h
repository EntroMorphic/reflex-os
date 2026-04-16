#ifndef REFLEX_VM_H
#define REFLEX_VM_H

/**
 * @file reflex_vm.h
 * @brief Public interface to the Reflex OS ternary VM interpreter.
 *
 * The VM is a register-based, host-indexed ternary machine that runs
 * inside the binary host layer. Programs are loaded via the loader
 * (see reflex_vm_loader.h) into a reflex_vm_state_t instance, and
 * executed one step at a time (#reflex_vm_step) or in a bounded
 * batch (#reflex_vm_run). Long-running VM programs usually live
 * inside a reflex_vm_task_runtime_t (see reflex_vm_task.h) which
 * hosts a platform task around the interpreter.
 *
 * Syscalls from ternary code into host services go through a
 * swappable handler installed via #reflex_vm_set_syscall_handler.
 * Most callers want the project default set by
 * #reflex_vm_use_default_syscalls.
 */

#include <stdint.h>

#include "reflex_types.h"

#include "reflex_vm_state.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Zero the execution state of @p vm without tearing down its
 * program, MMU, cache, or syscall handler bindings.
 *
 * Clears registers, sets `ip = 0`, clears the fault code, resets
 * `steps_executed`, and transitions `status` back to `READY`. The
 * loaded program and any owned private memory remain in place and
 * the VM can be stepped again immediately.
 */
void reflex_vm_reset(reflex_vm_state_t *vm);

/**
 * @brief Release any program memory the VM owns and put it in an
 * unloaded state.
 *
 * Frees `owned_private_memory` and the program pointer if
 * `owns_program` is set. After unload the VM must be loaded again
 * (via the loader) before it can be stepped.
 */
void reflex_vm_unload(reflex_vm_state_t *vm);

/**
 * @brief Install a custom syscall dispatcher.
 *
 * The handler is invoked whenever VM code executes a `TSYS`
 * instruction. @p context is passed opaquely as the handler's last
 * argument so the caller can thread through per-instance state
 * without globals.
 */
void reflex_vm_set_syscall_handler(reflex_vm_state_t *vm,
                                   reflex_vm_syscall_handler_t handler,
                                   void *context);

/**
 * @brief Install the Reflex OS default syscall dispatcher (log,
 * uptime, config-get, delay). Equivalent to
 * `reflex_vm_set_syscall_handler(vm, reflex_default_syscalls, NULL)`.
 */
void reflex_vm_use_default_syscalls(reflex_vm_state_t *vm);

/**
 * @brief Execute one instruction of @p vm.
 *
 * Fetches at `ip`, dispatches to the opcode handler, updates
 * registers / memory / fault state, and advances `ip`. On fault, @p
 * vm->status is set to `FAULTED` and the return value is the first
 * error encountered; subsequent calls return without making
 * progress until the caller resets the VM.
 */
reflex_err_t reflex_vm_step(reflex_vm_state_t *vm);

/**
 * @brief Execute up to @p max_steps instructions or until the VM
 * halts or faults, whichever comes first.
 *
 * Intended for bounded slices from a scheduler (see
 * reflex_vm_task.h). Does not busy-wait on blocked operations; the
 * interpreter is expected to return control on each syscall or
 * inter-step boundary so the host can preempt.
 */
reflex_err_t reflex_vm_run(reflex_vm_state_t *vm, uint32_t max_steps);

/**
 * @brief Boot-time self-check that exercises the interpreter
 * against a small canned program and known-bad inputs.
 *
 * Logs diagnostic ERROR messages for the known-bad paths (by
 * design — they're intentional negative cases). Returns REFLEX_OK if
 * every positive path produces the expected result.
 */
reflex_err_t reflex_vm_self_check(void);

#ifdef __cplusplus
}
#endif

#endif
