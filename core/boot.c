/** @file boot.c
 * @brief Reflex OS boot sequence and chip info.
 */
#include "reflex_boot.h"
#include "reflex_log.h"
#include "reflex_hal.h"

/* Platform-specific chip info. On ESP-IDF these come from esp_chip_info
 * and esp_app_desc. For now we use compile-time constants — the values
 * are properties of the board, not runtime-discovered. When we own the
 * eFuse reader, we can read chip revision and flash size from registers. */
#ifndef REFLEX_CHIP_NAME
#define REFLEX_CHIP_NAME "esp32-c6"
#endif
#ifndef REFLEX_VERSION
#define REFLEX_VERSION "v3.0-dev"
#endif

void reflex_boot_print_banner(void)
{
    reflex_log_init();
    REFLEX_LOGI(REFLEX_BOOT_TAG, "reflex-os boot");
    REFLEX_LOGI(REFLEX_BOOT_TAG, "chip=%s version=%s", REFLEX_CHIP_NAME, REFLEX_VERSION);
}
