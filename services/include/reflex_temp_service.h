#ifndef REFLEX_TEMP_SERVICE_H
#define REFLEX_TEMP_SERVICE_H

/**
 * @file reflex_temp_service.h
 * @brief Temperature sensor service — first real perception cell.
 *
 * Reads the ESP32-C6 internal temperature sensor every 5 seconds and
 * projects the reading into the GOOSE fabric as `perception.temp.reading`.
 * The ternary state maps to cold (-1, <25C), normal (0, 25-35C), or
 * warm (+1, >35C). The raw float is accessible via reflex_temp_get_celsius.
 */

#include "reflex_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Register the temperature sensor service with the service manager. */
reflex_err_t reflex_temp_service_register(void);

/** @brief Read the last polled temperature in degrees Celsius. */
float reflex_temp_get_celsius(void);

#ifdef __cplusplus
}
#endif

#endif
