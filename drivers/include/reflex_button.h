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

#include "reflex_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief GPIO number of the onboard button. */
/* Board pin assignments.
 *
 * These are the only two pins the tree hardcodes, and they were fixed at the
 * XIAO ESP32-C6's values regardless of target. GPIO 9 is unusable on a classic
 * ESP32: GPIO 6-11 are wired to the SPI flash on essentially every module, so
 * configuring it as an input made reflex_button_init fail, which failed
 * reflex_service_start_all, which made main.c take its early-return path and
 * skip the atmospheric arcing block entirely. The board booted to a working
 * shell with no mesh at all, logging "esp now not init!" — a hardcoded pin
 * quietly costing the ESP32 its entire radio substrate.
 *
 * Defaults now follow the boards this project is developed on: XIAO ESP32-C6
 * (user button GPIO9, LED GPIO15) and classic ESP32 devkits (BOOT button
 * GPIO0, on-board LED GPIO2).
 */
#if CONFIG_IDF_TARGET_ESP32
#define REFLEX_BUTTON_PIN 0
#else
#define REFLEX_BUTTON_PIN 9
#endif

/** @brief Configure the button pin as an input with the correct pull. */
reflex_err_t reflex_button_init(void);

#ifdef __cplusplus
}
#endif

#endif
