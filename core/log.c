#include "reflex_log.h"

static esp_log_level_t s_reflex_log_level = ESP_LOG_INFO;

void reflex_log_init(void)
{
    esp_log_level_set("reflex.*", s_reflex_log_level);
}

void reflex_log_set_level(esp_log_level_t level)
{
    s_reflex_log_level = level;
    esp_log_level_set("reflex.*", s_reflex_log_level);
}

esp_log_level_t reflex_log_get_level(void)
{
    return s_reflex_log_level;
}
