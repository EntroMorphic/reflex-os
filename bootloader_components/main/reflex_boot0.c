/*
 * Reflex OS Boot0 — Second-Stage Bootloader (Phase 0)
 *
 * This is the Reflex bootloader entry point. It replaces ESP-IDF's
 * default bootloader_start.c, giving Reflex OS control over boot
 * policy (partition selection, boot-loop protection, diagnostics).
 *
 * HONEST STATE: the boot *mechanism* is still ESP-IDF. We call
 * bootloader_init (hardware setup), bootloader_utility_load_partition_table
 * (partition reader), and bootloader_utility_load_boot_image (segment
 * loader). These are ~17K lines of ESP-IDF bootloader_support code.
 * Our contribution is the boot policy: which partition to boot, what
 * to do on failure, and boot-loop protection.
 *
 * The path to full ownership:
 *   Phase 1: replace bootloader_init with direct register + ROM calls
 *   Phase 2: replace partition table reader with Reflex format
 *   Phase 3: replace image loader with Reflex segment mapper
 * Each phase is testable independently on hardware.
 */

#include <stdbool.h>
#include "esp_rom_sys.h"
#include "soc/lp_aon_reg.h"
#include "bootloader_init.h"
#include "bootloader_utility.h"
#include "bootloader_common.h"

#define REFLEX_BOOT0_TAG "reflex.boot0"

/* Boot-loop protection via LP AON scratch register. This register
 * survives software resets (but not power-on). We use it as a reset
 * counter: increment on each boot, clear on successful OS handoff.
 * If the counter reaches the threshold, we halt instead of looping. */
#define BOOT_FAIL_MAX       3
#define BOOT_FAIL_REG       LP_AON_STORE0_REG
#define BOOT_FAIL_MAGIC     0xBF000000u
#define BOOT_FAIL_MASK      0xFF000000u
#define BOOT_FAIL_COUNT(v)  ((v) & 0xFF)

static int reflex_get_boot_fail_count(void)
{
    uint32_t val = REG_READ(BOOT_FAIL_REG);
    if ((val & BOOT_FAIL_MASK) != BOOT_FAIL_MAGIC) return 0;
    return BOOT_FAIL_COUNT(val);
}

static void reflex_set_boot_fail_count(int count)
{
    REG_WRITE(BOOT_FAIL_REG, BOOT_FAIL_MAGIC | (count & 0xFF));
}

static void reflex_clear_boot_fail_count(void)
{
    REG_WRITE(BOOT_FAIL_REG, 0);
}

static int reflex_select_boot_partition(bootloader_state_t *bs)
{
    if (!bootloader_utility_load_partition_table(bs)) {
        esp_rom_printf("[%s] partition table load failed\n", REFLEX_BOOT0_TAG);
        return -2;
    }

    int idx = bootloader_utility_get_selected_boot_partition(bs);
    if (idx < 0 && bs->factory.offset != 0) {
        idx = -1; /* FACTORY_INDEX: tells load_boot_image to use bs->factory */
    }
    return idx;
}

/*
 * Entry point. The ROM bootloader jumps here after loading us from
 * flash. We have a stack but hardware is mostly unconfigured.
 */
void __attribute__((noreturn)) call_start_cpu0(void)
{
    /* 1. Hardware initialization (ESP-IDF bootloader_support — Phase 0).
     *    This is the code we'll replace with direct register access. */
    if (bootloader_init() != 0) {
        esp_rom_printf("[%s] hardware init failed, halting\n", REFLEX_BOOT0_TAG);
        while (1) { esp_rom_delay_us(1000000); }
    }

    esp_rom_printf("\n[%s] Reflex OS bootloader (phase 0)\n", REFLEX_BOOT0_TAG);

    /* 2. Boot-loop protection. */
    int fail_count = reflex_get_boot_fail_count();
    if (fail_count >= BOOT_FAIL_MAX) {
        esp_rom_printf("[%s] boot failed %d times, halting (power cycle to retry)\n",
                       REFLEX_BOOT0_TAG, fail_count);
        reflex_clear_boot_fail_count();
        while (1) { esp_rom_delay_us(1000000); }
    }
    reflex_set_boot_fail_count(fail_count + 1);

    /* 3. Find the kernel partition. */
    bootloader_state_t bs = {0};
    int boot_index = reflex_select_boot_partition(&bs);
    if (boot_index < -1) {
        esp_rom_printf("[%s] no valid partition (attempt %d/%d), resetting\n",
                       REFLEX_BOOT0_TAG, fail_count + 1, BOOT_FAIL_MAX);
        bootloader_reset();
    }

    esp_rom_printf("[%s] booting partition %d\n", REFLEX_BOOT0_TAG, boot_index);

    /* 4. Clear the fail counter — we found a valid partition.
     *    The OS stability task will confirm full health later. */
    reflex_clear_boot_fail_count();

    /* 5. Load and execute the Reflex OS kernel. Does not return. */
    bootloader_utility_load_boot_image(&bs, boot_index);

    esp_rom_printf("[%s] image load failed\n", REFLEX_BOOT0_TAG);
    while (1) { esp_rom_delay_us(1000000); }
}
