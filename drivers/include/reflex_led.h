#ifndef REFLEX_LED_H
#define REFLEX_LED_H

/**
 * @file reflex_led.h
 * @brief Minimal GPIO abstraction for the XIAO ESP32-C6 onboard
 * LED on GPIO 15.
 *
 * Thin driver — only level control. Service-level behavior
 * (fabric-wired toggle, blink patterns, supervisor routing) is in
 * reflex_led_service.h / services/led_service.c.
 *
 * @note GPIO 15 is a HP-side pin; the ESP32-C6 LP core can only
 * drive LP I/O (GPIO 0–7), so the LP heartbeat cannot use this
 * driver — LP-side liveness is exposed as a counter instead (see
 * `goose_lp_heartbeat_count`).
 */

#include <stdbool.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief GPIO number of the onboard LED. */
#define REFLEX_LED_PIN 15

/** @brief Configure the LED pin as a push-pull output, initial level low. */
esp_err_t reflex_led_init(void);

/** @brief Drive the LED to @p on (true = lit, false = off). */
esp_err_t reflex_led_set(bool on);

/** @brief Flip the LED state. */
esp_err_t reflex_led_toggle(void);

/** @brief Read the last commanded LED state. Not a GPIO sample. */
bool reflex_led_get(void);

#ifdef __cplusplus
}
#endif

#endif
