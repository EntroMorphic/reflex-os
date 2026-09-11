/* Reflex's own flash driver. See include/reflex_flash.h.
 *
 * This programs the SPI1 flash controller directly. That is the whole point:
 * the alternative was the mask ROM's legacy routines, and they are *not*
 * self-sufficient. Measured on hardware: at early boot a ROM read returns the
 * same fixed garbage for every address, and it starts working only after
 * ESP-IDF's esp_flash driver has performed an operation and left SPI1
 * configured. One esp_flash_read repairs it; nothing else does. A store built
 * on the ROM path therefore reads nothing at boot, erases its own partition
 * believing it empty, and loses everything — which is exactly what happened,
 * and is what the ledger long recorded as "raw ROM flash access never reaches
 * the medium". The medium was fine. The controller was unconfigured.
 *
 * So Reflex configures it. Every transaction sets CTRL, USER, USER1, USER2 and
 * the lengths from scratch, exactly as ESP-IDF's host driver does, and depends
 * on no state left behind by anyone else.
 *
 * Two hardware details that are easy to get wrong and are taken from ESP-IDF's
 * own sequences rather than guessed:
 *   - page program carries its byte count in the *top byte of the address
 *     register*: ADDR = (addr & 0xFFFFFF) | (len << 24).
 *   - a command is started by writing its bit to CMD, and the register reads
 *     back zero when the controller is done. That is the completion test.
 *
 * The data registers are sixteen words, so every transfer is chunked to 64
 * bytes. Erase and program are followed by polling the flash chip's own
 * write-in-progress bit, because the controller's auto-wait feature is a
 * configuration this driver deliberately does not depend on.
 *
 * All of it runs with interrupts masked and the cache suspended: the CPU
 * executes XIP from the same chip, so a trap taken inside the window would
 * fetch its handler from flash that is not readable. Everything between
 * suspend and resume is IRAM-resident, which is also why the operation is
 * selected with if/else rather than a switch — a jump table would live in
 * .rodata, in flash.
 */

#include "reflex_flash.h"

#ifdef CONFIG_IDF_TARGET_ESP32C6

#include "reflex_rom_esp32c6.h"
#include "reflex_soc_esp32c6.h"
#include "reflex_regops.h"
#include <stdbool.h>
#include <string.h>

#define IRAM_FN __attribute__((section(".iram1"), noinline))

/* Cache_Resume_ICache's argument is a mask-ROM API value, not a register
 * field, so it cannot come through the bridge. It is 1, the same bit as
 * AUTOLOAD_ENA. */
#define REFLEX_ROM_ICACHE_AUTOLOAD 1u

#define FLASH_SECTOR_SIZE 4096u
#define SPI1_CHUNK        64u      /* sixteen data words */
#define FLASH_PAGE_SIZE   256u     /* page program wraps within this */
#define FLASH_CMD_READ    0x03u    /* single-line read, no dummy cycles */
#define ADDR_BITS_24      24u
#define FLASH_STATUS_WIP  0x01u
/* Bounded so a wedged controller fails instead of hanging the machine with
 * interrupts masked. Generous: a sector erase is tens of milliseconds. */
#define SPI1_SPIN_LIMIT   40000000u

static bool IRAM_FN spi1_wait_cmd(void) {
    for (uint32_t i = 0; i < SPI1_SPIN_LIMIT; i++) {
        if (REFLEX_REG_READ(REFLEX_SPI1_CMD_REG) == 0u) return true;
    }
    return false;
}

static bool IRAM_FN spi1_wait_not_busy(void) {
    for (uint32_t i = 0; i < SPI1_SPIN_LIMIT; i++) {
        REFLEX_REG_WRITE(REFLEX_SPI1_CMD_REG, REFLEX_SPI1_CMD_FLASH_RDSR);
        if (!spi1_wait_cmd()) return false;
        if ((REFLEX_REG_READ(REFLEX_SPI1_RD_STATUS_REG) & FLASH_STATUS_WIP) == 0u) return true;
    }
    return false;
}

static bool IRAM_FN spi1_write_enable(void) {
    REFLEX_REG_WRITE(REFLEX_SPI1_CMD_REG, REFLEX_SPI1_CMD_FLASH_WREN);
    return spi1_wait_cmd();
}

static void IRAM_FN spi1_set_addr_phase(uint32_t addr_reg_value) {
    REFLEX_REG_WRITE(REFLEX_SPI1_CTRL_REG, 0u);
    REFLEX_REG_WRITE(REFLEX_SPI1_USER1_REG,
                     ((ADDR_BITS_24 - 1u) & REFLEX_SPI1_USR_ADDR_BITLEN_V)
                         << REFLEX_SPI1_USR_ADDR_BITLEN_S);
    REFLEX_REG_WRITE(REFLEX_SPI1_ADDR_REG, addr_reg_value);
}

static bool IRAM_FN spi1_read_chunk(uint32_t addr, uint32_t *dst, uint32_t len) {
    spi1_set_addr_phase(addr & 0xFFFFFFu);
    REFLEX_REG_WRITE(REFLEX_SPI1_USER_REG,
                     REFLEX_SPI1_USR_COMMAND | REFLEX_SPI1_USR_ADDR | REFLEX_SPI1_USR_MISO);
    REFLEX_REG_WRITE(REFLEX_SPI1_USER2_REG,
                     ((FLASH_CMD_READ & REFLEX_SPI1_USR_COMMAND_VALUE_V)
                          << REFLEX_SPI1_USR_COMMAND_VALUE_S) |
                     ((8u - 1u) << REFLEX_SPI1_USR_COMMAND_BITLEN_S));
    REFLEX_REG_WRITE(REFLEX_SPI1_MISO_DLEN_REG,
                     ((len * 8u - 1u) & REFLEX_SPI1_USR_MISO_DBITLEN_V)
                         << REFLEX_SPI1_USR_MISO_DBITLEN_S);
    REFLEX_REG_WRITE(REFLEX_SPI1_MOSI_DLEN_REG, 0u);
    REFLEX_REG_WRITE(REFLEX_SPI1_CMD_REG, REFLEX_SPI1_CMD_USR);
    if (!spi1_wait_cmd()) return false;
    for (uint32_t i = 0; i < (len + 3u) / 4u; i++) {
        dst[i] = REFLEX_REG_READ(REFLEX_SPI1_W0_REG + 4u * i);
    }
    return true;
}

static bool IRAM_FN spi1_program_chunk(uint32_t addr, const uint32_t *src, uint32_t len) {
    if (!spi1_write_enable()) return false;
    /* The byte count travels in the top byte of the address register. */
    spi1_set_addr_phase((addr & 0xFFFFFFu) | (len << 24));
    REFLEX_REG_WRITE(REFLEX_SPI1_USER_REG, 0u);   /* no dummy phase */
    for (uint32_t i = 0; i < (len + 3u) / 4u; i++) {
        REFLEX_REG_WRITE(REFLEX_SPI1_W0_REG + 4u * i, src[i]);
    }
    REFLEX_REG_WRITE(REFLEX_SPI1_CMD_REG, REFLEX_SPI1_CMD_FLASH_PP);
    if (!spi1_wait_cmd()) return false;
    return spi1_wait_not_busy();
}

static bool IRAM_FN spi1_erase_sector(uint32_t addr) {
    if (!spi1_write_enable()) return false;
    spi1_set_addr_phase(addr & 0xFFFFFFu);
    REFLEX_REG_WRITE(REFLEX_SPI1_CMD_REG, REFLEX_SPI1_CMD_FLASH_SE);
    if (!spi1_wait_cmd()) return false;
    return spi1_wait_not_busy();
}

typedef enum { FLASH_OP_READ, FLASH_OP_WRITE, FLASH_OP_ERASE } flash_op_t;

static bool IRAM_FN flash_window(flash_op_t op, uint32_t addr, void *buf, size_t len) {
    uint32_t autoload =
        (REFLEX_REG_READ(REFLEX_EXTMEM_L1_CACHE_AUTOLOAD_CTRL_REG) &
         REFLEX_EXTMEM_L1_CACHE_AUTOLOAD_ENA) ? REFLEX_ROM_ICACHE_AUTOLOAD : 0u;

    uint32_t saved;
    __asm__ volatile("csrrci %0, mstatus, 0x8" : "=r"(saved));

    /* The cache is suspended only for operations that change the medium.
     *
     * A read does not: it moves bytes out of the chip through SPI1 and leaves
     * flash contents alone, so nothing the cache holds can go stale. Erase and
     * program do change it, and a cache still holding the old contents would
     * hand them back, so those keep the suspend.
     *
     * This is not a tidiness argument. Suspending and resuming around every
     * read broke the key-value store: writes landed and were witnessed by
     * esp_flash, yet the store's own walk could not find the entry it had just
     * written. An esp_rom_printf inside the walk made it work again, which is
     * the signature of a timing defect rather than a logic one, and the only
     * timing this driver adds per read is the suspend/resume pair. Interrupts
     * stay masked either way, and the code runs from IRAM, so no cache fetch
     * competes with the transfer. */
    bool mutates = (op != FLASH_OP_READ);
    if (mutates) Cache_Suspend_ICache();

    bool ok = true;
    if (op == FLASH_OP_ERASE) {
        for (uint32_t off = 0; ok && off < len; off += FLASH_SECTOR_SIZE) {
            ok = spi1_erase_sector(addr + off);
        }
    } else {
        uint8_t *bytes = (uint8_t *)buf;
        uint32_t off = 0;
        while (ok && off < len) {
            uint32_t n = (uint32_t)len - off;
            if (n > SPI1_CHUNK) n = SPI1_CHUNK;
            /* Page program wraps inside a 256-byte page: a program that runs
             * past a page boundary does not continue into the next page, it
             * wraps to the start of the same one and overwrites it. Chunking
             * to the data registers alone is not enough, because a 64-byte
             * chunk starting at, say, 0xE0 straddles the boundary at 0x100.
             * Reads have no such rule, but clamping both costs nothing. */
            uint32_t to_page_end = FLASH_PAGE_SIZE - ((addr + off) % FLASH_PAGE_SIZE);
            if (n > to_page_end) n = to_page_end;
            if (op == FLASH_OP_READ) {
                ok = spi1_read_chunk(addr + off, (uint32_t *)(void *)(bytes + off), n);
            } else {
                ok = spi1_program_chunk(addr + off, (const uint32_t *)(void *)(bytes + off), n);
            }
            off += n;
        }
    }

    if (mutates) Cache_Resume_ICache(autoload);
    if (saved & 0x8u) {
        __asm__ volatile("csrsi mstatus, 0x8");
    }
    return ok;
}

static bool word_aligned(uint32_t addr, const void *buf, size_t len) {
    return (addr & 3u) == 0 && (len & 3u) == 0 && (((uintptr_t)buf) & 3u) == 0;
}

/* Reads accept any address, length and buffer alignment.
 *
 * The controller moves whole words from word-aligned addresses, so an
 * unaligned request is served by reading the containing aligned range through
 * a bounce buffer and copying out the requested slice.
 *
 * This is not a convenience. The key-value store reads each entry's key at
 * `entry_offset + sizeof(header)`, and its header is five bytes — so every key
 * read lands on an address one past a word boundary. An earlier version of
 * this function refused those outright, the store's fallback filled the buffer
 * with 0xFF, every key comparison failed, and `kv_find` reported NOT_FOUND for
 * entries it had just written and could see. The entry *headers* sit at
 * aligned offsets and read perfectly, so the walk looked healthy while nothing
 * ever matched. esp_flash_read had always accepted unaligned reads, which is
 * why nothing noticed until the driver changed underneath it. */
reflex_err_t reflex_flash_read(uint32_t addr, void *dst, size_t len) {
    if (!dst || len == 0) return REFLEX_ERR_INVALID_ARG;

    uint8_t *out = (uint8_t *)dst;
    while (len > 0) {
        uint32_t aligned_addr = addr & ~3u;
        uint32_t head = addr - aligned_addr;              /* 0..3 */
        uint32_t want = (uint32_t)len;
        if (want > SPI1_CHUNK - head) want = SPI1_CHUNK - head;
        uint32_t span = (head + want + 3u) & ~3u;         /* whole words */

        uint32_t bounce[SPI1_CHUNK / 4 + 1];
        if (!flash_window(FLASH_OP_READ, aligned_addr, bounce, span)) {
            return REFLEX_ERR_INVALID_RESPONSE;
        }
        memcpy(out, (const uint8_t *)bounce + head, want);

        addr += want;
        out += want;
        len -= want;
    }
    return REFLEX_OK;
}

reflex_err_t reflex_flash_write(uint32_t addr, const void *src, size_t len) {
    if (!src || len == 0 || !word_aligned(addr, src, len)) return REFLEX_ERR_INVALID_ARG;
    return flash_window(FLASH_OP_WRITE, addr, (void *)(uintptr_t)src, len)
               ? REFLEX_OK : REFLEX_ERR_INVALID_RESPONSE;
}

reflex_err_t reflex_flash_erase(uint32_t addr, size_t len) {
    if (len == 0 || (addr % FLASH_SECTOR_SIZE) != 0 || (len % FLASH_SECTOR_SIZE) != 0) {
        return REFLEX_ERR_INVALID_ARG;
    }
    return flash_window(FLASH_OP_ERASE, addr, NULL, len) ? REFLEX_OK : REFLEX_ERR_INVALID_RESPONSE;
}

#endif /* CONFIG_IDF_TARGET_ESP32C6 */
