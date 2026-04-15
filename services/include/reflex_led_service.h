#ifndef REFLEX_LED_SERVICE_H
#define REFLEX_LED_SERVICE_H

/**
 * @file reflex_led_service.h
 * @brief LED service registration.
 *
 * The service wraps the thin reflex_led driver and subscribes to
 * ternary fabric messages (typically from a VM task or the
 * supervisor) to toggle the physical LED in response. This is the
 * end of the canonical button → fabric → VM → supervisor → LED
 * hardware-validated path.
 */

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Register the LED service with the service manager. */
esp_err_t reflex_led_service_register(void);

#ifdef __cplusplus
}
#endif

#endif
