/** @file button.c
 * @brief GPIO button driver with debounce.
 */
#include "reflex_hal.h"
#include "reflex_task.h"
#include "reflex_button.h"
#include "reflex_event.h"
#include "reflex_fabric.h"
#include "reflex_log.h"

static void reflex_button_task(void *arg)
{
    int last_level = 1;
    while (1) {
        int level = reflex_hal_gpio_get_level(REFLEX_BUTTON_PIN);
        if (level != last_level) {
            if (level == 0) {
                reflex_event_publish(REFLEX_EVENT_BUTTON_PRESSED, NULL, 0);
                reflex_message_t msg = {
                    .to = REFLEX_NODE_VM,
                    .from = REFLEX_NODE_BUTTON,
                    .op = 1,
                    .channel = REFLEX_CHAN_SYSTEM
                };
                reflex_fabric_send(&msg);
            } else {
                reflex_event_publish(REFLEX_EVENT_BUTTON_RELEASED, NULL, 0);
            }
            last_level = level;
        }
        reflex_task_delay_ms(50);
    }
}

reflex_err_t reflex_button_init(void)
{
    REFLEX_RETURN_ON_ERROR(reflex_hal_gpio_init_input(REFLEX_BUTTON_PIN, true),
                           "reflex.btn", "config failed");
    if (reflex_task_create(reflex_button_task, "reflex-btn", 2048, NULL, 12, NULL) != REFLEX_OK) {
        return REFLEX_FAIL;
    }
    REFLEX_LOGI(REFLEX_BOOT_TAG, "button_hal=ready (gpio%d)", REFLEX_BUTTON_PIN);
    return REFLEX_OK;
}
