#include "reflex_led.h"

#include "driver/gpio.h"
#include "esp_check.h"

static bool s_led_on = false;

esp_err_t reflex_led_init(void)
{
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << REFLEX_LED_PIN),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_RETURN_ON_ERROR(gpio_config(&io_conf), "reflex.led", "config failed");
    
    s_led_on = false;
    return gpio_set_level(REFLEX_LED_PIN, 0);
}

esp_err_t reflex_led_set(bool on)
{
    s_led_on = on;
    return gpio_set_level(REFLEX_LED_PIN, on ? 1 : 0);
}

esp_err_t reflex_led_toggle(void)
{
    return reflex_led_set(!s_led_on);
}

bool reflex_led_get(void)
{
    return s_led_on;
}
