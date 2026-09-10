/**
 * @file reflex_kernel.h
 * @brief Kernel policy hook API.
 *
 * The kernel supervisor calls a registered policy function at 1Hz.
 * The substrate (GOOSE) registers its implementation via
 * reflex_kernel_set_policy. The kernel knows nothing about cells,
 * routes, or purpose — it just calls a function pointer.
 */

#ifndef REFLEX_KERNEL_H
#define REFLEX_KERNEL_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*reflex_kernel_policy_fn)(uint32_t tick);

void reflex_kernel_set_policy(reflex_kernel_policy_fn fn);

/** @brief Start the supervisor task that drives the registered policy.
 *
 * Separated from the FreeRTOS scheduler wrap that used to be its only caller.
 * The supervisor is a Reflex task like any other — it is created through
 * reflex_task.h and delays through it — so it runs on whichever backend the
 * build selected. The wrap calls this where FreeRTOS is started; the entry
 * wrapper calls it where Reflex owns the machine and FreeRTOS never is.
 *
 * Without that split, taking Tier C to zero would have meant dropping the file
 * and silently losing the policy engine on exactly the configuration that
 * needs it. */
void reflex_kernel_start_supervisor(void);

#ifdef __cplusplus
}
#endif

#endif /* REFLEX_KERNEL_H */
