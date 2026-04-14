#ifndef REFLEX_LED_H
#define REFLEX_LED_H

#include <stdbool.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define REFLEX_LED_PIN 15

esp_err_t reflex_led_init(void);
esp_err_t reflex_led_set(bool on);
esp_err_t reflex_led_toggle(void);
bool reflex_led_get(void);

#ifdef __cplusplus
}
#endif

#endif
