#ifndef REFLEX_LOG_H
#define REFLEX_LOG_H

#include "esp_log.h"

#ifdef __cplusplus
extern "C" {
#endif

void reflex_log_init(void);
void reflex_log_set_level(esp_log_level_t level);
esp_log_level_t reflex_log_get_level(void);

#define REFLEX_BOOT_TAG "reflex.boot"
#define REFLEX_SHELL_TAG "reflex.shell"

#define REFLEX_LOGI(tag, format, ...) ESP_LOGI(tag, format, ##__VA_ARGS__)
#define REFLEX_LOGW(tag, format, ...) ESP_LOGW(tag, format, ##__VA_ARGS__)
#define REFLEX_LOGE(tag, format, ...) ESP_LOGE(tag, format, ##__VA_ARGS__)

#ifdef __cplusplus
}
#endif

#endif
