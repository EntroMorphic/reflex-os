#ifndef REFLEX_BUTTON_H
#define REFLEX_BUTTON_H

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define REFLEX_BUTTON_PIN 9

esp_err_t reflex_button_init(void);

#ifdef __cplusplus
}
#endif

#endif
