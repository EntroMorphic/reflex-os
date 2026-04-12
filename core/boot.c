#include "reflex_boot.h"

#include "reflex_log.h"

#include "sdkconfig.h"
#include "esp_app_desc.h"
#include "esp_chip_info.h"
#include "esp_flash.h"
#include "esp_system.h"

static const char *reflex_boot_reset_reason_name(esp_reset_reason_t reason)
{
    switch (reason) {
    case ESP_RST_UNKNOWN:
        return "unknown";
    case ESP_RST_POWERON:
        return "power_on";
    case ESP_RST_EXT:
        return "external";
    case ESP_RST_SW:
        return "software";
    case ESP_RST_PANIC:
        return "panic";
    case ESP_RST_INT_WDT:
        return "int_wdt";
    case ESP_RST_TASK_WDT:
        return "task_wdt";
    case ESP_RST_WDT:
        return "wdt";
    case ESP_RST_DEEPSLEEP:
        return "deep_sleep";
    case ESP_RST_BROWNOUT:
        return "brownout";
    case ESP_RST_SDIO:
        return "sdio";
    case ESP_RST_USB:
        return "usb";
    case ESP_RST_JTAG:
        return "jtag";
    case ESP_RST_EFUSE:
        return "efuse";
    case ESP_RST_PWR_GLITCH:
        return "power_glitch";
    case ESP_RST_CPU_LOCKUP:
        return "cpu_lockup";
    default:
        return "unmapped";
    }
}

void reflex_boot_print_banner(void)
{
    const esp_app_desc_t *app_desc = esp_app_get_description();
    esp_chip_info_t chip_info = {0};
    uint32_t flash_size = 0;
    esp_reset_reason_t reset_reason = esp_reset_reason();

    reflex_log_init();
    esp_chip_info(&chip_info);
    esp_flash_get_size(NULL, &flash_size);

    REFLEX_LOGI(REFLEX_BOOT_TAG, "reflex-os boot");
    REFLEX_LOGI(REFLEX_BOOT_TAG,
                "project=%s version=%s target=%s",
                app_desc->project_name,
                app_desc->version,
                CONFIG_IDF_TARGET);
    REFLEX_LOGI(REFLEX_BOOT_TAG,
                "chip=esp32-c6 cores=%d revision=%d.%d flash=%luMB",
                chip_info.cores,
                chip_info.revision / 100,
                chip_info.revision % 100,
                (unsigned long)(flash_size / (1024 * 1024)));
    REFLEX_LOGI(REFLEX_BOOT_TAG,
                "reset_reason=%s console=usb_serial_jtag",
                reflex_boot_reset_reason_name(reset_reason));
}
