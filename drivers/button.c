#include "reflex_hal.h"
#include "driver/gpio.h"
#include "reflex_button.h"



#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "reflex_event.h"
#include "reflex_fabric.h"
#include "reflex_log.h"

static void reflex_button_task(void* arg)
{
    int last_level = 1;
    while (1) {
        int level = gpio_get_level(REFLEX_BUTTON_PIN);
        if (level != last_level) {
            if (level == 0) {
                reflex_event_publish(REFLEX_EVENT_BUTTON_PRESSED, NULL, 0);
                
                // Publish to Fabric for VM
                reflex_message_t msg = {
                    .to = REFLEX_NODE_VM,
                    .from = REFLEX_NODE_BUTTON,
                    .op = 1, // 'Pressed' Op
                    .channel = REFLEX_CHAN_SYSTEM
                };
                reflex_fabric_send(&msg);
            } else {
                reflex_event_publish(REFLEX_EVENT_BUTTON_RELEASED, NULL, 0);
            }
            last_level = level;
        }
        vTaskDelay(pdMS_TO_TICKS(50)); // Debounce
    }
}

reflex_err_t reflex_button_init(void)
{
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << REFLEX_BUTTON_PIN),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE, // Using polling task for reliability
    };
    REFLEX_RETURN_ON_ERROR(gpio_config(&io_conf), "reflex.btn", "config failed");

    if (xTaskCreate(reflex_button_task, "reflex-btn", 2048, NULL, 12, NULL) != pdPASS) {
        return ESP_FAIL;
    }

    REFLEX_LOGI(REFLEX_BOOT_TAG, "button_hal=ready (gpio%d)", REFLEX_BUTTON_PIN);
    return REFLEX_OK;
}
