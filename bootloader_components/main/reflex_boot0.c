/*
 * Reflex OS Boot0 — Second-Stage Bootloader
 *
 * This is the first Reflex code that runs on the ESP32-C6. The ROM
 * bootloader loads this from flash address 0x0 and jumps to
 * call_start_cpu0. We initialize the hardware, find the Reflex OS
 * kernel image in flash, load it into memory, and jump to it.
 *
 * ROM functions (mask ROM, available without any SDK):
 *   esp_rom_printf       — print to console
 *   esp_rom_delay_us     — busy-wait
 *   esp_rom_get_reset_reason — query last reset
 *
 * Bootloader support (ESP-IDF HAL, used during build transition):
 *   bootloader_init      — clock, flash, cache/MMU, console, WDT
 *   bootloader_utility_*  — partition table, image loading
 *
 * The long-term goal is to replace the bootloader_support calls with
 * direct register access and ROM function calls, removing the last
 * ESP-IDF dependency from the boot path. This first version uses the
 * HAL for hardware init so we can validate the boot chain end-to-end
 * before we strip it down.
 */

#include <stdbool.h>
#include "esp_rom_sys.h"
#include "bootloader_init.h"
#include "bootloader_utility.h"
#include "bootloader_common.h"

#define REFLEX_BOOT0_TAG "reflex.boot0"

static int reflex_select_boot_partition(bootloader_state_t *bs)
{
    if (!bootloader_utility_load_partition_table(bs)) {
        esp_rom_printf("[%s] partition table load failed\n", REFLEX_BOOT0_TAG);
        return -1;
    }

    int idx = bootloader_utility_get_selected_boot_partition(bs);
    if (idx < 0 && bs->factory.offset != 0) {
        idx = 0;
    }
    return idx;
}

/*
 * Entry point. The ROM bootloader jumps here after loading us from
 * flash. We have a stack but hardware is mostly unconfigured.
 */
void __attribute__((noreturn)) call_start_cpu0(void)
{
    /* 1. Hardware initialization: clocks, flash, cache/MMU, console,
     *    watchdog. Uses the bootloader_support HAL for now — this is
     *    the code we'll replace with direct register access. */
    if (bootloader_init() != 0) {
        esp_rom_printf("[%s] hardware init failed, resetting\n", REFLEX_BOOT0_TAG);
        bootloader_reset();
    }

    esp_rom_printf("\n[%s] Reflex OS bootloader\n", REFLEX_BOOT0_TAG);

    /* 2. Find the kernel partition. */
    bootloader_state_t bs = {0};
    int boot_index = reflex_select_boot_partition(&bs);
    if (boot_index < 0) {
        esp_rom_printf("[%s] no valid partition, resetting\n", REFLEX_BOOT0_TAG);
        bootloader_reset();
    }

    esp_rom_printf("[%s] booting partition %d\n", REFLEX_BOOT0_TAG, boot_index);

    /* 3. Load and execute the Reflex OS kernel. This function does
     *    not return — it maps the image into memory and jumps to its
     *    entry point. */
    bootloader_utility_load_boot_image(&bs, boot_index);

    /* Should never reach here. */
    esp_rom_printf("[%s] boot failed\n", REFLEX_BOOT0_TAG);
    while (1) { esp_rom_delay_us(1000000); }
}
