/*
 * Reflex OS Boot0 — Second-Stage Bootloader
 *
 * This is the first Reflex code that runs on the ESP32-C6. The ROM
 * bootloader loads this from flash 0x0 and jumps to call_start_cpu0.
 *
 * Dependencies (all silicon-level, not SDK):
 *   - ROM functions: flash read, printf, delay (mask ROM)
 *   - SOC headers: register address constants (no code)
 *   - HAL: cache_hal_init/enable, mmu_hal_init/map_region (thin
 *     register wrappers over silicon — acceptable as the hardware
 *     abstraction layer between us and the specific chip revision)
 *
 * NOT used: bootloader_init, bootloader_utility_*, or any other
 * bootloader_support high-level function. The boot mechanism is ours.
 */

#include <stdbool.h>
#include <string.h>
#include <stdint.h>

/* ROM functions — these are in mask ROM, part of the silicon */
#include "esp_rom_sys.h"
#include "esp_rom_spiflash.h"
/* esp_rom_uart.h excluded — needs hal/uart_ll.h which isn't available
 * in the bootloader build. Console is set up via install_channel_putc
 * which is declared in esp_rom_sys.h. */
extern void esp_rom_output_putc(char c);
extern void esp_rom_install_channel_putc(int channel, void (*putc)(char c));

/* Register definitions — just #define constants, no code */
#include "soc/lp_wdt_reg.h"
#include "soc/lp_aon_reg.h"
#include "soc/pcr_reg.h"
#include "soc/assist_debug_reg.h"

/* HAL — thin register wrappers over the cache/MMU silicon */
#include "hal/cache_hal.h"
#include "hal/cache_ll.h"
#include "hal/mmu_hal.h"
#include "hal/mmu_types.h"
#include "hal/cache_types.h"
#include "hal/lpwdt_ll.h"

/* Chip info for clock config */
#include "soc/rtc.h"
#include "hal/clk_tree_ll.h"
#include "esp_private/regi2c_ctrl.h"
#include "soc/regi2c_lp_bias.h"

/* Bootloader support — ONLY for linker symbols and startup assembly.
 * We don't call bootloader_init or bootloader_utility_*. */
#include "bootloader_common.h"

#define TAG "reflex.boot0"

/* ---- Image and partition format constants ---- */

#define IMAGE_MAGIC             0xE9
#define IMAGE_MAX_SEGMENTS      16
#define PARTITION_TABLE_ADDR    0x8000
#define PARTITION_MAGIC         0x50AA
#define PARTITION_MAGIC_MD5     0xEBEB
#define PART_TYPE_APP           0x00
#define PART_SUBTYPE_FACTORY    0x00
#define FLASH_MAP_BASE          0x42000000

#pragma pack(push, 1)
typedef struct {
    uint8_t  magic;
    uint8_t  segment_count;
    uint8_t  spi_mode;
    uint8_t  spi_speed_size;
    uint32_t entry_addr;
    uint8_t  wp_pin;
    uint8_t  spi_pin_drv[3];
    uint16_t chip_id;
    uint8_t  min_chip_rev;
    uint16_t min_chip_rev_full;
    uint16_t max_chip_rev_full;
    uint8_t  reserved[4];
    uint8_t  hash_appended;
} reflex_image_header_t;

typedef struct {
    uint32_t load_addr;
    uint32_t data_len;
} reflex_segment_header_t;

typedef struct {
    uint16_t magic;
    uint8_t  type;
    uint8_t  subtype;
    uint32_t offset;
    uint32_t size;
    uint8_t  label[16];
    uint32_t flags;
} reflex_partition_entry_t;
#pragma pack(pop)

/* ---- Boot-loop protection ---- */

#define BOOT_FAIL_MAX       3
#define BOOT_FAIL_REG       LP_AON_STORE0_REG
#define BOOT_FAIL_MAGIC     0xBF000000u
#define BOOT_FAIL_MASK      0xFF000000u
#define BOOT_FAIL_COUNT(v)  ((v) & 0xFF)

static int get_fail_count(void) {
    uint32_t val = REG_READ(BOOT_FAIL_REG);
    if ((val & BOOT_FAIL_MASK) != BOOT_FAIL_MAGIC) return 0;
    return BOOT_FAIL_COUNT(val);
}
static void set_fail_count(int c) { REG_WRITE(BOOT_FAIL_REG, BOOT_FAIL_MAGIC | (c & 0xFF)); }
static void clear_fail_count(void) { REG_WRITE(BOOT_FAIL_REG, 0); }

/* ---- Halt helper ---- */

static void __attribute__((noreturn)) halt(const char *msg) {
    esp_rom_printf("[%s] %s\n", TAG, msg);
    while (1) { esp_rom_delay_us(1000000); }
}

/* ---- Hardware init (direct register + ROM calls) ---- */

static void hw_feed_wdt(void) {
    REG_WRITE(LP_WDT_SWD_WPROTECT_REG, LP_WDT_SWD_WKEY_VALUE);
    REG_SET_BIT(LP_WDT_SWD_CONFIG_REG, LP_WDT_SWD_AUTO_FEED_EN);
    REG_WRITE(LP_WDT_SWD_WPROTECT_REG, 0);
}

static void hw_debug_init(void) {
    REG_SET_BIT(PCR_ASSIST_CONF_REG, PCR_ASSIST_CLK_EN);
    REG_CLR_BIT(PCR_ASSIST_CONF_REG, PCR_ASSIST_RST_EN);
    REG_WRITE(ASSIST_DEBUG_CORE_0_RCD_EN_REG,
              ASSIST_DEBUG_CORE_0_RCD_PDEBUGEN | ASSIST_DEBUG_CORE_0_RCD_RECORDEN);
}

/* Forward declaration — bootloader_clock_configure is the one
 * remaining bootloader_support function we call. It configures the
 * PLL for 160MHz. Replacing it requires direct RTC register writes
 * specific to the C6's clock tree. Tracked for Phase 1. */
extern void bootloader_clock_configure(void);

static void hw_clock_init(void) {
    _regi2c_ctrl_ll_master_enable_clock(true);
    regi2c_ctrl_ll_master_configure_clock();
    bootloader_clock_configure();
}

static void hw_console_init(void) {
    esp_rom_install_channel_putc(1, esp_rom_output_putc);
}

static void hw_cache_init(void) {
    cache_hal_init();
    mmu_hal_init();
}

/* ---- Flash read wrapper ---- */

static int flash_read(uint32_t addr, void *buf, uint32_t len) {
    return esp_rom_spiflash_read(addr, (uint32_t *)buf, (int32_t)len);
}

/* ---- Partition table reader ---- */

static uint32_t find_factory_partition(void) {
    uint8_t buf[sizeof(reflex_partition_entry_t) * 32];
    if (flash_read(PARTITION_TABLE_ADDR, buf, sizeof(buf)) != 0) return 0;

    for (int i = 0; i < 32; i++) {
        reflex_partition_entry_t *e = (reflex_partition_entry_t *)&buf[i * sizeof(reflex_partition_entry_t)];
        if (e->magic == PARTITION_MAGIC_MD5 || e->magic == 0xFFFF) break;
        if (e->magic != PARTITION_MAGIC) continue;
        if (e->type == PART_TYPE_APP && e->subtype == PART_SUBTYPE_FACTORY) {
            return e->offset;
        }
    }
    return 0;
}

/* ---- Image loader ---- */

static bool is_sram_addr(uint32_t addr) {
    return (addr >= 0x40800000 && addr < 0x40880000);
}

static bool is_rtc_addr(uint32_t addr) {
    return (addr >= 0x50000000 && addr < 0x50002000);
}

static bool is_flash_mapped_addr(uint32_t addr) {
    return (addr >= FLASH_MAP_BASE && addr < 0x44000000);
}

typedef struct {
    uint32_t vaddr;
    uint32_t paddr;
    uint32_t len;
} flash_map_entry_t;

static uint32_t load_image(uint32_t part_offset) {
    reflex_image_header_t hdr;
    if (flash_read(part_offset, &hdr, sizeof(hdr)) != 0) return 0;
    if (hdr.magic != IMAGE_MAGIC) {
        esp_rom_printf("[%s] bad image magic 0x%02x at 0x%lx\n",
                       TAG, hdr.magic, (unsigned long)part_offset);
        return 0;
    }
    if (hdr.segment_count > IMAGE_MAX_SEGMENTS) {
        esp_rom_printf("[%s] too many segments (%d)\n", TAG, hdr.segment_count);
        return 0;
    }

    esp_rom_printf("[%s] image: %d segments, entry=0x%08lx\n",
                   TAG, hdr.segment_count, (unsigned long)hdr.entry_addr);

    uint32_t flash_offset = part_offset + sizeof(hdr);
    flash_map_entry_t maps[IMAGE_MAX_SEGMENTS];
    int map_count = 0;

    /* First pass: load SRAM segments and record flash-mapped regions */
    for (int i = 0; i < hdr.segment_count; i++) {
        reflex_segment_header_t seg;
        if (flash_read(flash_offset, &seg, sizeof(seg)) != 0) {
            esp_rom_printf("[%s] seg %d header read failed\n", TAG, i);
            return 0;
        }
        flash_offset += sizeof(seg);

        if (is_sram_addr(seg.load_addr) || is_rtc_addr(seg.load_addr)) {
            if (flash_read(flash_offset, (void *)seg.load_addr, seg.data_len) != 0) {
                esp_rom_printf("[%s] seg %d data read failed\n", TAG, i);
                return 0;
            }
        } else if (is_flash_mapped_addr(seg.load_addr) && map_count < IMAGE_MAX_SEGMENTS) {
            maps[map_count].vaddr = seg.load_addr;
            maps[map_count].paddr = flash_offset;
            maps[map_count].len = seg.data_len;
            map_count++;
        }

        flash_offset += seg.data_len;
    }

    /* Second pass: set up MMU for flash-mapped segments */
    cache_hal_disable(CACHE_LL_LEVEL_EXT_MEM, CACHE_TYPE_ALL);
    mmu_hal_unmap_all();

    for (int i = 0; i < map_count; i++) {
        uint32_t page_size = CONFIG_MMU_PAGE_SIZE;
        uint32_t vaddr_aligned = maps[i].vaddr & ~(page_size - 1);
        uint32_t paddr_aligned = maps[i].paddr & ~(page_size - 1);
        uint32_t end = maps[i].paddr + maps[i].len;
        uint32_t map_len = end - paddr_aligned;
        /* Align up to page boundary */
        map_len = (map_len + page_size - 1) & ~(page_size - 1);
        uint32_t actual_len = 0;

        mmu_hal_map_region(0, MMU_TARGET_FLASH0,
                           vaddr_aligned, paddr_aligned,
                           map_len, &actual_len);
    }

    cache_hal_enable(CACHE_LL_LEVEL_EXT_MEM, CACHE_TYPE_ALL);

    return hdr.entry_addr;
}

/* ---- Entry point ---- */

void __attribute__((noreturn)) call_start_cpu0(void)
{
    /* 1. Minimal hardware init — direct register writes + ROM calls */
    hw_feed_wdt();
    hw_debug_init();
    hw_clock_init();
    hw_console_init();
    hw_cache_init();

    esp_rom_printf("\n[%s] Reflex OS bootloader\n", TAG);

    /* 2. Boot-loop protection */
    int fail_count = get_fail_count();
    if (fail_count >= BOOT_FAIL_MAX) {
        clear_fail_count();
        halt("boot failed too many times, halting (power cycle to retry)");
    }
    set_fail_count(fail_count + 1);

    /* 3. Find the factory partition */
    uint32_t part_offset = find_factory_partition();
    if (part_offset == 0) {
        esp_rom_printf("[%s] no factory partition (attempt %d/%d)\n",
                       TAG, fail_count + 1, BOOT_FAIL_MAX);
        esp_rom_delay_us(100000);
        /* Software reset to retry */
        while (1) { REG_WRITE(0x600B1034, 0x8000); } /* LP_WDT_SWD_WPROTECT + force reset */
    }

    esp_rom_printf("[%s] factory partition at 0x%lx\n", TAG, (unsigned long)part_offset);

    /* 4. Load the kernel image */
    uint32_t entry = load_image(part_offset);
    if (entry == 0) halt("image load failed");

    /* 5. Clear fail counter — we're about to jump */
    clear_fail_count();

    esp_rom_printf("[%s] jumping to 0x%08lx\n", TAG, (unsigned long)entry);

    /* 6. Jump to the kernel */
    typedef void (*entry_fn_t)(void);
    entry_fn_t app_entry = (entry_fn_t)entry;
    app_entry();

    halt("entry returned — should never happen");
}
