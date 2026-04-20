/** @file led.c
 * @brief GPIO LED driver.
 */
#include "reflex_hal.h"
#include "reflex_led.h"

static bool s_led_on = false;

reflex_err_t reflex_led_init(void)
{
    REFLEX_RETURN_ON_ERROR(reflex_hal_gpio_init_output(REFLEX_LED_PIN), "reflex.led", "config failed");
    s_led_on = false;
    return reflex_hal_gpio_set_level(REFLEX_LED_PIN, 0);
}

reflex_err_t reflex_led_set(bool on)
{
    s_led_on = on;
    return reflex_hal_gpio_set_level(REFLEX_LED_PIN, on ? 1 : 0);
}

reflex_err_t reflex_led_toggle(void)
{
    return reflex_led_set(!s_led_on);
}

bool reflex_led_get(void)
{
    return s_led_on;
}
