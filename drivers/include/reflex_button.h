#ifndef REFLEX_BUTTON_H
#define REFLEX_BUTTON_H

/**
 * @file reflex_button.h
 * @brief Minimal GPIO abstraction for the XIAO ESP32-C6 onboard
 * "boot" button on GPIO 9.
 *
 * The driver is intentionally thin — it only configures the pin.
 * Event delivery (edge detection, debouncing, fabric publication)
 * lives in the button service layer above it
 * (reflex_button_service.h).
 */

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief GPIO number of the onboard button. */
#define REFLEX_BUTTON_PIN 9

/** @brief Configure the button pin as an input with the correct pull. */
esp_err_t reflex_button_init(void);

#ifdef __cplusplus
}
#endif

#endif
