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

/* ROM entry points — mask ROM, part of the silicon. Declared by Reflex now,
 * which also retires the three ad-hoc externs that used to sit here because
 * esp_rom_uart.h could not be included in a bootloader build. */
#include "reflex_rom_esp32c6.h"

/* ROM SHA-256 engine — still ESP-IDF's header.
 *
 * The remaining ESP-IDF include in this file's ROM group, and deliberately so.
 * ets_sha_* takes an opaque SHA_CTX whose layout Reflex would have to
 * reproduce by hand, and getting that wrong corrupts the stack silently rather
 * than failing to build — the one transcription risk this work has been
 * avoiding everywhere else. The alternative is to drop ROM SHA entirely for
 * the software SHA-256 in platform/reflex_crypto.c, which is already verified
 * against the RFC 4231 vectors; that trades roughly 2 KB of bootloader and a
 * few hundred milliseconds of boot-time hashing for one fewer dependency, and
 * is a decision about boot behaviour rather than a mechanical substitution. */
#include "rom/sha.h"

/* Register constants and accessors — Reflex's own.
 *
 * These were nine ESP-IDF `soc` register headers. Nothing in them was ESP-IDF
 * code:
 * they carry register addresses and bit positions, which are silicon facts,
 * plus volatile load/store macros the compiler already knows how to emit.
 * reflex_soc_esp32c6.h is generated from the same vendor SVD that backs the
 * 12,738-node shadow atlas, and every constant in it is proved identical to
 * the ESP-IDF macro it replaces by `make soc-bridge`. */
#include "reflex_regops.h"
#include "reflex_soc_esp32c6.h"

/* Clock configuration is the last ESP-IDF register dependency here.
 *
 * rtc_clk_init is the chip-level PLL driver and RTC_CLK_CONFIG_DEFAULT is a
 * struct initialiser rather than an address, so neither is derivable from the
 * SVD the way the constants above are. Porting the PLL bring-up is real work
 * on real analogue behaviour — not the mechanical substitution the rest of
 * this file just went through — and is deliberately left standing. */
#include "soc/rtc.h"
#include "hal/clk_tree_ll.h"
#include "esp_private/regi2c_ctrl.h"
#include "soc/regi2c_lp_bias.h"

/* Bootloader support — ONLY for linker symbols and startup assembly.
 * No high-level bootloader functions are called. */

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
#define BOOT_FAIL_REG REFLEX_LP_AON_STORE0_REG
#define BOOT_FAIL_MAGIC     0xBF000000u
#define BOOT_FAIL_MASK      0xFF000000u
#define BOOT_FAIL_COUNT(v)  ((v) & 0xFF)

static int get_fail_count(void) {
    uint32_t val = REFLEX_REG_READ(BOOT_FAIL_REG);
    if ((val & BOOT_FAIL_MASK) != BOOT_FAIL_MAGIC) return 0;
    return BOOT_FAIL_COUNT(val);
}
static void set_fail_count(int c) {
    REFLEX_REG_WRITE(BOOT_FAIL_REG, BOOT_FAIL_MAGIC | (c & 0xFF));
}
static void clear_fail_count(void) {
    REFLEX_REG_WRITE(BOOT_FAIL_REG, 0);
}

/* ---- Halt helper ---- */

static void __attribute__((noreturn)) halt(const char *msg) {
    esp_rom_printf("[%s] %s\n", TAG, msg);
    while (1) { esp_rom_delay_us(1000000); }
}

/* ---- Hardware init (direct register + ROM calls) ---- */

static void hw_feed_wdt(void) {
    REFLEX_REG_WRITE(REFLEX_LP_WDT_SWD_WPROTECT_REG, 0x50D83AA1);
    REFLEX_REG_SET_BIT(REFLEX_LP_WDT_SWD_CONFIG_REG, REFLEX_LP_WDT_SWD_AUTO_FEED_EN);
    REFLEX_REG_WRITE(REFLEX_LP_WDT_SWD_WPROTECT_REG, 0);
}

/* Feed the Timer Group 0 main watchdog.
 *
 * The ROM bootloader arms MWDT0 before handing control to us and expects the
 * second-stage loader to be brief. Hashing the whole image is not brief — a
 * 1.7 MB read via the ROM SPI routines comfortably outruns the timeout — so
 * the verify loop feeds it explicitly. The watchdog is deliberately left armed
 * rather than disabled: it is the only thing that catches a boot0 that wedges
 * somewhere without its own retry path. */
static void hw_feed_mwdt0(void) {
    REFLEX_REG_WRITE(REFLEX_TIMG0_WDTWPROTECT_REG, REFLEX_TIMG_WDT_WKEY_VALUE);
    REFLEX_REG_WRITE(REFLEX_TIMG0_WDTFEED_REG, 1);
    REFLEX_REG_WRITE(REFLEX_TIMG0_WDTWPROTECT_REG, 0);
}

static void hw_debug_init(void) {
    REFLEX_REG_SET_BIT(REFLEX_PCR_ASSIST_CONF_REG, REFLEX_PCR_ASSIST_CLK_EN);
    REFLEX_REG_CLR_BIT(REFLEX_PCR_ASSIST_CONF_REG, REFLEX_PCR_ASSIST_RST_EN);
    REFLEX_REG_WRITE(REFLEX_ASSIST_DEBUG_CORE_0_RCD_EN_REG,
                     REFLEX_ASSIST_DEBUG_CORE_0_RCD_PDEBUGEN |
                         REFLEX_ASSIST_DEBUG_CORE_0_RCD_RECORDEN);
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
    REFLEX_REG_CLR_BIT(REFLEX_LP_WDT_INT_ENA_REG, REFLEX_LP_WDT_SUPER_WDT_INT_ENA);
    REFLEX_REG_CLR_BIT(REFLEX_LP_WDT_INT_ENA_REG, REFLEX_LP_WDT_LP_WDT_INT_ENA);
    REFLEX_REG_CLR_BIT(REFLEX_LP_ANA_INT_ENA_REG, REFLEX_LP_ANA_BOD_MODE0_INT_ENA);
    REFLEX_REG_CLR_BIT(REFLEX_PMU_HP_INT_ENA_REG, REFLEX_PMU_SOC_WAKEUP_INT_ENA);
    REFLEX_REG_CLR_BIT(REFLEX_PMU_HP_INT_ENA_REG, REFLEX_PMU_SOC_SLEEP_REJECT_INT_ENA);
    REFLEX_REG_SET_BIT(REFLEX_LP_WDT_INT_CLR_REG, REFLEX_LP_WDT_SUPER_WDT_INT_CLR);
    REFLEX_REG_SET_BIT(REFLEX_LP_WDT_INT_CLR_REG, REFLEX_LP_WDT_LP_WDT_INT_CLR);
    REFLEX_REG_SET_BIT(REFLEX_LP_ANA_INT_CLR_REG, REFLEX_LP_ANA_BOD_MODE0_INT_CLR);
}

static void hw_console_init(void) {
    /* The ROM primitive directly; see reflex_rom_esp32c6.h. */
    ets_install_putc1(esp_rom_output_putc);
}

/* ---- Direct cache/MMU management (no HAL) ---- */

#define MMU_PAGE_SIZE       0x10000  /* 64 KB */
#define MMU_PAGE_SHIFT      16
#define MMU_ENTRY_NUM       256
#define MMU_VALID_BIT       (1 << 9)
#define MMU_INVALID_VAL     0

static void reflex_mmu_unmap_all(void) {
    for (int i = 0; i < MMU_ENTRY_NUM; i++) {
        REFLEX_REG_WRITE(REFLEX_SPI_MEM_MMU_ITEM_INDEX_REG, i);
        REFLEX_REG_WRITE(REFLEX_SPI_MEM_MMU_ITEM_CONTENT_REG, MMU_INVALID_VAL);
    }
}

static void reflex_mmu_map_page(uint32_t entry_id, uint32_t paddr_page) {
    REFLEX_REG_WRITE(REFLEX_SPI_MEM_MMU_ITEM_INDEX_REG, entry_id);
    REFLEX_REG_WRITE(REFLEX_SPI_MEM_MMU_ITEM_CONTENT_REG, paddr_page | MMU_VALID_BIT);
}

static void reflex_cache_disable(void) {
    Cache_Disable_ICache();
}

static void reflex_cache_enable(void) {
    /* Enable IBUS and DBUS by clearing the shut bits */
    REFLEX_REG_CLR_BIT(REFLEX_EXTMEM_L1_CACHE_CTRL_REG, REFLEX_EXTMEM_L1_CACHE_SHUT_IBUS);
    REFLEX_REG_CLR_BIT(REFLEX_EXTMEM_L1_CACHE_CTRL_REG, REFLEX_EXTMEM_L1_CACHE_SHUT_DBUS);
    /* autoload=0: the app configures cache autoload during startup */
    Cache_Enable_ICache(0);
}

static void hw_cache_init(void) {
    /* Set page size to 64 KB (code 0) */
    REFLEX_REG_SET_FIELD(REFLEX_SPI_MEM_MMU_POWER_CTRL_REG, REFLEX_SPI_MEM_MMU_PAGE_SIZE,
                         REFLEX_SPI_MEM_MMU_PAGE_SIZE_S, 0);
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

/* RAM windows, taken from the SoC definitions rather than restated here.
 * An earlier revision hardcoded the LP window as 0x50000000-0x50002000 (8 KB);
 * the C6 actually has 16 KB (REFLEX_SOC_RTC_IRAM_HIGH == 0x50004000), and a real
 * image does place a 0x3490-byte segment at 0x50000800. That mistake was
 * invisible while the bound was never enforced — the loader only tested the
 * start address — and became a false rejection the moment it was. */
#define SRAM_LOW REFLEX_SOC_IRAM_LOW
#define SRAM_HIGH REFLEX_SOC_IRAM_HIGH
#define RTC_LOW REFLEX_SOC_RTC_IRAM_LOW
#define RTC_HIGH REFLEX_SOC_RTC_IRAM_HIGH
#define IMAGE_HASH_LEN      32
#define IMAGE_MAX_SEG_LEN   (16u * 1024u * 1024u)  /* larger than any sane segment */

static bool is_sram_addr(uint32_t addr) {
    return (addr >= SRAM_LOW && addr < SRAM_HIGH);
}

static bool is_rtc_addr(uint32_t addr) {
    return (addr >= RTC_LOW && addr < RTC_HIGH);
}

static bool is_flash_mapped_addr(uint32_t addr) {
    return (addr >= FLASH_MAP_BASE && addr < 0x44000000);
}

/* Is [addr, addr+len) entirely inside [low, high)?
 *
 * Overflow-safe: the subtraction is only evaluated once addr is known to be
 * below high, so `high - addr` cannot wrap, and comparing len against it
 * cannot be defeated by an addr+len that wraps past 2^32. */
static bool range_within(uint32_t addr, uint32_t len, uint32_t low, uint32_t high) {
    if (addr < low || addr >= high) return false;
    return len <= (high - addr);
}

/* Verify the SHA-256 that esptool appends when hash_appended is set.
 *
 * Layout (confirmed against a real build): the digest covers every byte from
 * the image header through the 1-byte checksum that terminates the 16-byte
 * aligned body, and the 32-byte digest follows immediately after.
 *
 * This is corruption detection, not authentication — the digest is not signed,
 * so anyone who can rewrite flash can rewrite the digest too. It closes the
 * "jumped into a half-erased image" failure mode, which is the one that
 * actually bites in the field. Real authentication needs secure boot. */
static bool image_verify_sha256(uint32_t part_offset, uint32_t body_len) {
    /* static, not stack: SHA_CTX alone is ~620 bytes and the bootloader stack
     * is small. Boot0 is single-threaded, so BSS is the right home for these. */
    static SHA_CTX ctx;
    static __attribute__((aligned(4))) uint8_t buf[4096];
    static __attribute__((aligned(4))) uint8_t expected[IMAGE_HASH_LEN];
    static __attribute__((aligned(4))) uint8_t digest[IMAGE_HASH_LEN];

    if (flash_read(part_offset + body_len, expected, IMAGE_HASH_LEN) != 0) {
        esp_rom_printf("[%s] hash read failed\n", TAG);
        return false;
    }

    ets_sha_enable();
    if (ets_sha_init(&ctx, SHA2_256) != ETS_OK || ets_sha_starts(&ctx, 0) != ETS_OK) {
        ets_sha_disable();
        esp_rom_printf("[%s] sha init failed\n", TAG);
        return false;
    }

    /* body_len is 16-byte aligned and the buffer is a multiple of 16, so every
     * chunk stays word-aligned for the ROM flash reader. */
    for (uint32_t off = 0; off < body_len; ) {
        uint32_t chunk = body_len - off;
        if (chunk > sizeof(buf)) chunk = sizeof(buf);
        if (flash_read(part_offset + off, buf, chunk) != 0) {
            ets_sha_disable();
            esp_rom_printf("[%s] hash read failed at 0x%lx\n", TAG, (unsigned long)off);
            return false;
        }
        ets_sha_update(&ctx, buf, chunk, true);
        off += chunk;
        hw_feed_mwdt0();
    }

    ets_status_t rc = ets_sha_finish(&ctx, digest);
    ets_sha_disable();
    if (rc != ETS_OK) {
        esp_rom_printf("[%s] sha finish failed\n", TAG);
        return false;
    }

    uint8_t diff = 0;
    for (int i = 0; i < IMAGE_HASH_LEN; i++) diff |= (uint8_t)(digest[i] ^ expected[i]);
    return diff == 0;
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
    static reflex_segment_header_t segs[IMAGE_MAX_SEGMENTS];
    static uint32_t seg_data_off[IMAGE_MAX_SEGMENTS];
    int map_count = 0;

    /* Pass 1: walk the segment table, bounds-check every destination, and
     * measure the image. Nothing is written to RAM here — a segment cannot be
     * trusted until the whole image has been verified. */
    for (int i = 0; i < hdr.segment_count; i++) {
        reflex_segment_header_t seg;
        if (flash_read(flash_offset, &seg, sizeof(seg)) != 0) {
            esp_rom_printf("[%s] seg %d header read failed\n", TAG, i);
            return 0;
        }
        flash_offset += sizeof(seg);

        if (seg.data_len > IMAGE_MAX_SEG_LEN) {
            esp_rom_printf("[%s] seg %d absurd length 0x%lx\n",
                           TAG, i, (unsigned long)seg.data_len);
            return 0;
        }

        /* A destination inside a RAM window must fit *entirely* inside it.
         * Validating only the start address (as an earlier revision did) lets
         * a malformed image write past the end of SRAM or the 8 KB RTC window,
         * over the bootloader's own stack and code. Reject rather than clamp:
         * a truncated kernel is corruption, not recovery. */
        if (is_sram_addr(seg.load_addr) || is_rtc_addr(seg.load_addr)) {
            bool fits = range_within(seg.load_addr, seg.data_len, SRAM_LOW, SRAM_HIGH) ||
                        range_within(seg.load_addr, seg.data_len, RTC_LOW, RTC_HIGH);
            if (!fits) {
                esp_rom_printf("[%s] seg %d overruns its RAM window (addr=0x%08lx len=0x%lx)\n",
                               TAG, i, (unsigned long)seg.load_addr, (unsigned long)seg.data_len);
                return 0;
            }
        }

        segs[i] = seg;
        seg_data_off[i] = flash_offset;
        flash_offset += seg.data_len;
    }

    /* Verify before trusting. The body is the header, segment table, segment
     * data, padding and the trailing checksum byte, rounded up to 16 bytes;
     * the digest sits immediately after it. */
    if (hdr.hash_appended) {
        uint32_t body_len = ((flash_offset - part_offset) + 1u + 15u) & ~15u;
        if (!image_verify_sha256(part_offset, body_len)) {
            esp_rom_printf("[%s] IMAGE HASH MISMATCH - refusing to boot\n", TAG);
            return 0;
        }
        esp_rom_printf("[%s] image hash ok (%lu bytes)\n", TAG, (unsigned long)body_len);
    } else {
        esp_rom_printf("[%s] WARNING: image has no appended hash, loading unverified\n", TAG);
    }

    /* Pass 2: now that the image is known good, load RAM segments and record
     * the flash-mapped regions. */
    for (int i = 0; i < hdr.segment_count; i++) {
        if (is_sram_addr(segs[i].load_addr) || is_rtc_addr(segs[i].load_addr)) {
            if (flash_read(seg_data_off[i], (void *)segs[i].load_addr, segs[i].data_len) != 0) {
                esp_rom_printf("[%s] seg %d data read failed\n", TAG, i);
                return 0;
            }
        } else if (is_flash_mapped_addr(segs[i].load_addr) && map_count < IMAGE_MAX_SEGMENTS) {
            maps[map_count].vaddr = segs[i].load_addr;
            maps[map_count].paddr = seg_data_off[i];
            maps[map_count].len = segs[i].data_len;
            map_count++;
        }
    }

    /* Pass 3: set up MMU for flash-mapped segments */
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
        /* Software reset to retry. REFLEX_LP_AON_HPSYS_SW_RESET is bit 31 of
         * REFLEX_LP_AON_SYS_CFG_REG; the only other defined bit in that register is
         * FORCE_DOWNLOAD_BOOT (bit 30). Use the named constants — an earlier
         * revision wrote the literal 0x8000 (bit 15, reserved) to a hardcoded
         * 0x600B1034, which is a no-op, so this loop span forever instead of
         * resetting. The fail counter is left incremented on purpose: if the
         * reset does not take, the next boot still converges on the halt. */
        while (1) {
            REFLEX_REG_WRITE(REFLEX_LP_AON_SYS_CFG_REG, REFLEX_LP_AON_HPSYS_SW_RESET);
        }
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
