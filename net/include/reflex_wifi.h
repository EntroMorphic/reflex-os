#ifndef REFLEX_WIFI_H
#define REFLEX_WIFI_H

/**
 * @file reflex_wifi.h
 * @brief Wi-Fi service registration.
 *
 * The service's start hook brings Wi-Fi up in STA mode even when
 * no SSID is configured, so the radio is always available for
 * ESP-NOW atmospheric arcing (which is otherwise silent without a
 * running radio). If credentials are present, it also attempts an
 * AP connection. See net/wifi.c.
 */

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Register the Wi-Fi service with the service manager. */
esp_err_t reflex_wifi_service_register(void);

#ifdef __cplusplus
}
#endif

#endif
