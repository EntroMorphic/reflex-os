#ifndef REFLEX_SERVICE_H
#define REFLEX_SERVICE_H

/**
 * @file reflex_service.h
 * @brief Tiny service manager — the host-side registry of
 * long-lived background services (LED, button, Wi-Fi, VM task,
 * etc.) that boot together.
 *
 * A service is a reflex_service_desc_t: a name plus four optional
 * lifecycle hooks (init, start, stop, status) and an opaque
 * context pointer threaded through the callbacks. main/main.c
 * registers every service at boot and then calls
 * #reflex_service_start_all exactly once; individual services can
 * later be inspected from the shell via `services`.
 *
 * The shell iteration API (#reflex_service_get_count /
 * #reflex_service_get_by_index) is intentionally non-owning —
 * callers must not free the returned pointers.
 */

#include <stdbool.h>
#include <stdint.h>

#include "reflex_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Coarse lifecycle state of a registered service. */
typedef enum {
    REFLEX_SERVICE_STATUS_STOPPED = 0,
    REFLEX_SERVICE_STATUS_STARTED,
    REFLEX_SERVICE_STATUS_FAULTED,
} reflex_service_status_t;

/**
 * @brief Service descriptor.
 *
 * Any hook may be NULL; the manager treats a missing hook as
 * success. @p context is passed unchanged to every callback so the
 * service doesn't need a global.
 */
typedef struct reflex_service_desc {
    const char *name;
    reflex_err_t (*init)(void *ctx);
    reflex_err_t (*start)(void *ctx);
    reflex_err_t (*stop)(void *ctx);
    reflex_service_status_t (*status)(void *ctx);
    void *context;
} reflex_service_desc_t;

/** @brief Zero the internal service table. Call once at boot. */
reflex_err_t reflex_service_manager_init(void);

/**
 * @brief Record @p service in the manager. The descriptor pointer
 * itself is stored — the caller is responsible for keeping it
 * live (typical pattern: a static/global reflex_service_desc_t).
 */
reflex_err_t reflex_service_register(const reflex_service_desc_t *service);

/**
 * @brief Call init() then start() on every registered service in
 * registration order. Aborts on the first failure.
 */
reflex_err_t reflex_service_start_all(void);

/** @brief Call stop() on every registered service in reverse order. */
reflex_err_t reflex_service_stop_all(void);

/** @brief Count of registered services (for shell iteration). */
size_t reflex_service_get_count(void);

/**
 * @brief Non-owning accessor for iteration. Returns NULL if @p
 * index is out of range.
 */
const reflex_service_desc_t *reflex_service_get_by_index(size_t index);

#ifdef __cplusplus
}
#endif

#endif
