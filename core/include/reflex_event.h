#ifndef REFLEX_EVENT_H
#define REFLEX_EVENT_H

#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    REFLEX_EVENT_BOOT_COMPLETE = 0,
    REFLEX_EVENT_SERVICE_STARTED,
    REFLEX_EVENT_SERVICE_STOPPED,
    REFLEX_EVENT_SERVICE_FAILED,
    REFLEX_EVENT_BUTTON_PRESSED,
    REFLEX_EVENT_BUTTON_RELEASED,
    REFLEX_EVENT_WIFI_CONNECTED,
    REFLEX_EVENT_WIFI_DISCONNECTED,
    REFLEX_EVENT_IP_ACQUIRED,
    REFLEX_EVENT_CUSTOM,
} reflex_event_type_t;

typedef struct {
    reflex_event_type_t type;
    uint32_t timestamp_ms;
    void *data;
    size_t data_len;
} reflex_event_t;

typedef void (*reflex_event_handler_t)(const reflex_event_t *event, void *ctx);

esp_err_t reflex_event_bus_init(void);
esp_err_t reflex_event_subscribe(reflex_event_type_t type, reflex_event_handler_t handler, void *ctx);
esp_err_t reflex_event_publish(reflex_event_type_t type, void *data, size_t data_len);

#ifdef __cplusplus
}
#endif

#endif
