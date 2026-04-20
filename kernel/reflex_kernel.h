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

#ifdef __cplusplus
}
#endif

#endif /* REFLEX_KERNEL_H */
