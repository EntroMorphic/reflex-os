#ifndef REFLEX_BUTTON_SERVICE_H
#define REFLEX_BUTTON_SERVICE_H

/**
 * @file reflex_button_service.h
 * @brief Button service registration.
 *
 * The service wraps the thin reflex_button driver and publishes
 * debounced press events onto the ternary message fabric, where
 * they can be consumed by a VM task or routed via the supervisor.
 */

#include "reflex_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Register the button service with the service manager. */
reflex_err_t reflex_button_service_register(void);

#ifdef __cplusplus
}
#endif

#endif
