/** @file reflex_boot0.c
 * @brief Second-stage bootloader entry point.
 */
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
extern void esp_rom_output_tx_wait_idle(uint32_t uart_no);

/* Register definitions — just #define constants, no code */
#include "soc/lp_wdt_reg.h"
#include "soc/lp_aon_reg.h"
#include "soc/pcr_reg.h"
#include "soc/assist_debug_reg.h"

/* Cache/MMU register definitions */
#include "soc/extmem_reg.h"
#include "soc/spi_mem_reg.h"
#include "soc/ext_mem_defs.h"

/* ROM cache functions (mask ROM — silicon, not SDK) */
extern int Cache_Enable_ICache(uint32_t autoload);
extern int Cache_Disable_ICache(void);
extern int Cache_Suspend_ICache(void);
extern int Cache_Resume_ICache(uint32_t autoload);

/* Clock config — rtc_clk_init is the chip-level PLL driver */
#include "soc/rtc.h"
#include "hal/clk_tree_ll.h"
#include "esp_private/regi2c_ctrl.h"
#include "soc/regi2c_lp_bias.h"
#include "soc/lp_analog_peri_reg.h"
#include "soc/pmu_reg.h"

/* Bootloader support — ONLY for linker symbols, startup assembly,
 * and the REG_READ/REG_WRITE macros from soc/soc.h. No high-level
 * bootloader functions are called. */

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
    REG_WRITE(LP_WDT_SWD_WPROTECT_REG, 0x50D83AA1);
    REG_SET_BIT(LP_WDT_SWD_CONFIG_REG, LP_WDT_SWD_AUTO_FEED_EN);
    REG_WRITE(LP_WDT_SWD_WPROTECT_REG, 0);
}

static void hw_debug_init(void) {
    REG_SET_BIT(PCR_ASSIST_CONF_REG, PCR_ASSIST_CLK_EN);
    REG_CLR_BIT(PCR_ASSIST_CONF_REG, PCR_ASSIST_RST_EN);
    REG_WRITE(ASSIST_DEBUG_CORE_0_RCD_EN_REG,
              ASSIST_DEBUG_CORE_0_RCD_PDEBUGEN | ASSIST_DEBUG_CORE_0_RCD_RECORDEN);
}

static void hw_clock_init(void) {
    _regi2c_ctrl_ll_master_enable_clock(true);
    regi2c_ctrl_ll_master_configure_clock();

    /* Configure clocks via rtc_clk_init — chip-level PLL/regulator
     * driver from esp_hw_support (hardware register writes, not boot
     * policy). This function configures BBPLL, voltage regulators,
     * CPU frequency divider, and RC oscillator tuning. */
    esp_rom_output_tx_wait_idle(0);

    rtc_clk_config_t clk_cfg = RTC_CLK_CONFIG_DEFAULT();
    clk_cfg.cpu_freq_mhz = 80;

    soc_rtc_slow_clk_src_t slow_src = rtc_clk_slow_src_get();
    clk_cfg.slow_clk_src = (slow_src == SOC_RTC_SLOW_CLK_SRC_INVALID)
                         ? SOC_RTC_SLOW_CLK_SRC_RC_SLOW : slow_src;

    /* RC_FAST chosen deliberately — C6 ECO0 has a timing issue with
     * XTAL_D2 as the fast clock source that can prevent the chip from
     * capturing the reset reason. RC_FAST avoids this on all revisions. */
    clk_cfg.fast_clk_src = SOC_RTC_FAST_CLK_SRC_RC_FAST;

    rtc_clk_init(clk_cfg);

    /* Clear all LP interrupt enables and pending bits so stale brownout,
     * WDT, or sleep interrupts from a previous boot don't fire during
     * early OS init. */
    CLEAR_PERI_REG_MASK(LP_WDT_INT_ENA_REG, LP_WDT_SUPER_WDT_INT_ENA);
    CLEAR_PERI_REG_MASK(LP_WDT_INT_ENA_REG, LP_WDT_LP_WDT_INT_ENA);
    CLEAR_PERI_REG_MASK(LP_ANALOG_PERI_LP_ANA_LP_INT_ENA_REG,
                        LP_ANALOG_PERI_LP_ANA_BOD_MODE0_LP_INT_ENA);
    CLEAR_PERI_REG_MASK(PMU_HP_INT_ENA_REG, PMU_SOC_WAKEUP_INT_ENA);
    CLEAR_PERI_REG_MASK(PMU_HP_INT_ENA_REG, PMU_SOC_SLEEP_REJECT_INT_ENA);
    SET_PERI_REG_MASK(LP_WDT_INT_CLR_REG, LP_WDT_SUPER_WDT_INT_CLR);
    SET_PERI_REG_MASK(LP_WDT_INT_CLR_REG, LP_WDT_LP_WDT_INT_CLR);
    SET_PERI_REG_MASK(LP_ANALOG_PERI_LP_ANA_LP_INT_CLR_REG,
                      LP_ANALOG_PERI_LP_ANA_BOD_MODE0_LP_INT_CLR);
}

static void hw_console_init(void) {
    esp_rom_install_channel_putc(1, esp_rom_output_putc);
}

/* ---- Direct cache/MMU management (no HAL) ---- */

#define MMU_PAGE_SIZE       0x10000  /* 64 KB */
#define MMU_PAGE_SHIFT      16
#define MMU_ENTRY_NUM       256
#define MMU_VALID_BIT       (1 << 9)
#define MMU_INVALID_VAL     0

static void reflex_mmu_unmap_all(void) {
    for (int i = 0; i < MMU_ENTRY_NUM; i++) {
        REG_WRITE(SPI_MEM_MMU_ITEM_INDEX_REG(0), i);
        REG_WRITE(SPI_MEM_MMU_ITEM_CONTENT_REG(0), MMU_INVALID_VAL);
    }
}

static void reflex_mmu_map_page(uint32_t entry_id, uint32_t paddr_page) {
    REG_WRITE(SPI_MEM_MMU_ITEM_INDEX_REG(0), entry_id);
    REG_WRITE(SPI_MEM_MMU_ITEM_CONTENT_REG(0), paddr_page | MMU_VALID_BIT);
}

static void reflex_cache_disable(void) {
    Cache_Disable_ICache();
}

static void reflex_cache_enable(void) {
    /* Enable IBUS and DBUS by clearing the shut bits */
    REG_CLR_BIT(EXTMEM_L1_CACHE_CTRL_REG, EXTMEM_L1_CACHE_SHUT_IBUS);
    REG_CLR_BIT(EXTMEM_L1_CACHE_CTRL_REG, EXTMEM_L1_CACHE_SHUT_DBUS);
    /* autoload=0: the app configures cache autoload during startup */
    Cache_Enable_ICache(0);
}

static void hw_cache_init(void) {
    /* Set page size to 64 KB (code 0) */
    REG_SET_FIELD(SPI_MEM_MMU_POWER_CTRL_REG(0), SPI_MEM_MMU_PAGE_SIZE, 0);
    reflex_mmu_unmap_all();
    reflex_cache_enable();
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
    reflex_cache_disable();
    reflex_mmu_unmap_all();

    for (int i = 0; i < map_count; i++) {
        uint32_t vaddr_aligned = maps[i].vaddr & ~(MMU_PAGE_SIZE - 1);
        uint32_t paddr_aligned = maps[i].paddr & ~(MMU_PAGE_SIZE - 1);
        uint32_t end = maps[i].paddr + maps[i].len;
        uint32_t paddr_cur = paddr_aligned;

        while (paddr_cur < end) {
            uint32_t entry_id = (vaddr_aligned >> MMU_PAGE_SHIFT) & (MMU_ENTRY_NUM - 1);
            uint32_t ppage = paddr_cur >> MMU_PAGE_SHIFT;
            reflex_mmu_map_page(entry_id, ppage);
            vaddr_aligned += MMU_PAGE_SIZE;
            paddr_cur += MMU_PAGE_SIZE;
        }
    }

    reflex_cache_enable();

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
