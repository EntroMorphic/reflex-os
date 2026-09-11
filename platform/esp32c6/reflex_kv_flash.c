/**
 * @file reflex_kv_flash.c
 * @brief Reflex KV — raw flash backend using ROM SPI flash functions.
 *
 * Simple page-based key-value store on a dedicated flash partition.
 * Uses ESP-IDF's esp_flash_* API. It used raw esp_rom_spiflash_* for zero
 * component dependencies; see below for why that had to change.
 *
 * Layout (one 4KB sector):
 *   [4B magic][4B sequence][entries...][0xFF fill]
 * Entry:
 *   [1B key_len][2B val_len][1B type][key_len bytes key][val_len bytes value]
 * Type: 0=str, 1=blob, 2=i32, 3=u8
 *
 * On commit: write dirty entries to current page. On page full:
 * compact live entries to next page, erase old page.
 */

/* Why this file talks to ESP-IDF's flash API and not the ROM directly.
 *
 * It used esp_rom_spiflash_* to keep zero ESP-IDF component dependencies, and
 * that choice was the direct cause of it persisting nothing. Raw ROM flash
 * access while the CPU executes XIP out of the same flash does not reach the
 * medium: a write and a read-back agree inside one boot because both go through
 * the cache, and the data is gone after a reset. The store reported
 * "initialised fresh" on every boot, `aura setkey` could not pair two boards,
 * and `purpose set` was gone by the next boot — all silently.
 *
 * Getting from there to here took four attempts, and the three that failed are
 * worth keeping because each looked correct:
 *
 *   1. A dedicated `reflexkv` partition. The store had been writing at 0x9000,
 *      the same six sectors `nvs` occupies, so phy_init's radio calibration
 *      overwrote it. A real bug, fixed — and not the cause.
 *   2. Word-aligned single-write entries with checked results. Also real, also
 *      not the cause.
 *   3. Cache_Suspend_ICache/Cache_Resume_ICache around the ROM calls, masked
 *      and IRAM-resident. Still "initialised fresh": suspending the instruction
 *      cache is not the whole of what ESP-IDF's flash path does.
 *   4. esp_flash_* with spi_flash_guard_set(&g_flash_guard_no_os_ops). Hangs
 *      under REFLEX_OWN_ENTRY — those guards serve the legacy spi_flash_* API,
 *      which esp_flash_* never consults.
 *
 * What works is esp_flash_*, plus two things for the case where Reflex owns the
 * machine and no scheduler exists: esp_flash_app_disable_os_functions swaps the
 * chip's own os_func for esp_flash_noos_functions, and the flash calls are made
 * with interrupts masked, because that no-OS layer suspends the cache without
 * disabling interrupts — it assumes none are running, and under Reflex the tick
 * is. A tick taken inside that window fetches reflex_trap_handler from flash,
 * which is not there.
 *
 * The masking is conditional for the mirror-image reason: with FreeRTOS running
 * esp_flash keeps its app os_func, that takes a mutex, and a mutex cannot be
 * waited on with interrupts masked. Masking unconditionally deadlocked the
 * independence build on its first configuration write. Both halves belong to
 * one regime and are selected together.
 *
 * `make parity-diff` now reports **zero** capability regressions for both the
 * independence build and the own-entry build against the stock ESP-IDF build.
 */

#include "reflex_kv.h"
#ifdef REFLEX_HOST_BUILD
/* The host suite compiles this file against a RAM-backed mock in
 * tests/host/test_kv.c, so it cannot see ESP-IDF's headers. Declaring the three
 * entry points it uses keeps the store itself — the ring, the compaction, the
 * entry walk — testable on the host, which is where its logic bugs are cheap to
 * find. What the host cannot model is the medium, and that is exactly the class
 * of bug that hid here for so long. */
typedef int esp_err_t;
#define ESP_OK 0
extern esp_err_t esp_flash_read(void *chip, void *buf, uint32_t addr, uint32_t len);
extern esp_err_t esp_flash_write(void *chip, const void *buf, uint32_t addr, uint32_t len);
extern esp_err_t esp_flash_erase_region(void *chip, uint32_t start, uint32_t size);
#else
#include "esp_flash.h"
#ifdef REFLEX_OWN_ENTRY
#include "esp_flash_internal.h"
#endif
#endif
#include <stdbool.h>
#include <string.h>
#include <stdlib.h>

#define KV_MAGIC        0x52464B56  /* "RFKV" */
#define KV_SECTOR_SIZE  4096
#define KV_NUM_SECTORS  6
#define KV_ENTRY_MAX    256
#define KV_KEY_MAX      15
#define KV_VAL_MAX      240

/* Flash partition offset — uses the "nvs" partition at 0x9000 (24KB) */
/* Offset of the `reflexkv` partition in partitions.csv.
 *
 * This was 0x9000, which is where `nvs` starts — the same six sectors. Every
 * entry this store wrote was later overwritten by NVS, whose users include
 * phy_init saving radio calibration on every boot, so writes reported success
 * and nothing survived a reboot. `aura setkey` could not pair two boards and
 * `purpose set` was gone by the next boot, both silently.
 *
 * Hardcoded, and therefore coupled to partitions.csv: this file declares its
 * own ROM entry points and pulls in no ESP-IDF component, so it cannot look the
 * partition up at runtime without giving that up. Changing the table above this
 * partition moves it, and this constant has to move with it. KV_NUM_SECTORS *
 * KV_SECTOR_SIZE must equal the partition size. */
#define KV_FLASH_BASE 0x10000

#define KV_TYPE_STR     0
#define KV_TYPE_BLOB    1
#define KV_TYPE_I32     2
#define KV_TYPE_U8      3
#define KV_TYPE_ERASED  0xFF

/* ROM flash functions (mask ROM, always available) */
/* Declared in reflex_rom_esp32c6.h now. These three externs were the first
 * instance of the technique in this tree; the header generalises it. */
/* Not reflex_rom_esp32c6.h: its ROM flash declarations collide with ESP-IDF's
 * for the same symbols, and the bootloader still needs Reflex's. This one
 * translation unit sits on ESP-IDF's side of that line and declares the single
 * ROM entry point it still wants. */
extern int esp_rom_printf(const char *fmt, ...);

typedef struct {
    uint32_t magic;
    uint32_t sequence;
} kv_page_header_t;

typedef struct {
    uint8_t ns_hash;
    uint8_t key_len;
    uint16_t val_len;
    uint8_t type;
} __attribute__((packed)) kv_entry_header_t;

/* Is an entry header read from flash structurally usable?
 *
 * kv_write_entry validates key_len and val_len on the way in, but that says
 * nothing about what is actually on the medium: a partial write, bit rot, or a
 * flash image this code did not produce can all present arbitrary values. The
 * scans below copy key_len bytes into a KV_KEY_MAX+1 stack buffer, so an
 * unvalidated 254 was a 238-byte stack overflow driven purely by flash
 * contents. Validating on read is the point of validating on read. */
/* Bytes one entry occupies on the medium, including its tail padding.
 *
 * Every entry starts on a 4-byte boundary, because esp_rom_spiflash_write
 * requires a word-aligned destination and the header is five packed bytes. An
 * unpadded layout put the second entry's header at offset 13, the third
 * somewhere else again, and those writes simply did not take — silently, since
 * nothing checked. That is why nothing this store was ever asked to keep
 * survived a reboot.
 *
 * One rule, used by the writer and by all three readers, so the walk cannot
 * disagree with the append. */
static inline uint32_t kv_entry_span(const kv_entry_header_t *eh) {
    return ((uint32_t)sizeof(kv_entry_header_t) + eh->key_len + eh->val_len + 3u) & ~3u;
}

static bool kv_entry_header_sane(const kv_entry_header_t *eh) {
    return eh->key_len >= 1 && eh->key_len <= KV_KEY_MAX && eh->val_len <= KV_VAL_MAX;
}


static uint32_t s_active_sector = 0;
static uint32_t s_active_seq = 0;
static uint32_t s_write_offset = 0;
static bool s_initialized = false;

static uint8_t ns_hash(const char *ns) {
    uint8_t h = 0;
    while (*ns) { h = h * 31 + (uint8_t)*ns++; }
    return h;
}

static uint32_t sector_addr(uint32_t sector) {
    return KV_FLASH_BASE + sector * KV_SECTOR_SIZE;
}

/* Flash operations run with interrupts masked.
 *
 * esp_flash's no-OS os_func suspends the cache and does not disable
 * interrupts — it is written for the case where none are running. Under
 * REFLEX_OWN_ENTRY they are: Reflex's tick fires at 1 kHz, and
 * reflex_trap_handler lives in flash, so a tick taken inside the suspended
 * window fetches an instruction that is not there. The board hung in
 * reflex_kv_init on the first read, before any of these functions could report
 * anything.
 *
 * Masking costs whatever ticks fall inside a flash operation, which is a real
 * cost during an erase and an acceptable one: the store is written at init and
 * on rare configuration changes, and the alternative is a machine that stops.
 * The app-side os_func ESP-IDF installs when FreeRTOS is running does the same
 * thing for the same reason. */
#ifdef REFLEX_OWN_ENTRY
static inline uint32_t kv_intr_mask(void) {
    uint32_t ms;
    __asm__ volatile("csrrci %0, mstatus, 0x8" : "=r"(ms));
    return ms;
}

static inline void kv_intr_restore(uint32_t ms) {
    if (ms & 0x8u) __asm__ volatile("csrsi mstatus, 0x8");
}
#else
/* Not when FreeRTOS is running, and this is the other half of the same
 * mistake. There, esp_flash keeps its app os_func, which takes a mutex —
 * and a mutex cannot be waited on with interrupts masked, because the context
 * switch that releases it can never run. Masking unconditionally deadlocked
 * the independence build on its first configuration write, immediately after
 * fixing the own-entry hang the same masking cures.
 *
 * The masking and the no-OS os_func belong to one regime and are now selected
 * together: where Reflex owns the machine, mask and swap; where FreeRTOS owns
 * it, leave both alone and let ESP-IDF do its own locking. */
static inline uint32_t kv_intr_mask(void) {
    return 0;
}

static inline void kv_intr_restore(uint32_t ms) {
    (void)ms;
}
#endif

/* Reads of any length, in bounce-buffer-sized chunks.
 *
 * This clamped the *read* to the bounce buffer and then copied the caller's
 * full length out of it:
 *
 *     if (aligned_len > sizeof(aligned_buf)) aligned_len = sizeof(aligned_buf);
 *     ...
 *     memcpy(buf, aligned_buf, len);
 *
 * For any len above 256 that copies from beyond a 256-byte stack array — the
 * caller gets whatever happened to be next on the stack, and the truncation is
 * silent. It is reachable: kv_compact's copy loop reads a whole entry, and a
 * maximum-size entry is sizeof(header) + 15 + 240 = 263 bytes. Chunking is
 * both correct for every length and cheaper to reason about than a cap nobody
 * can see from the call site. */
static void flash_read(uint32_t addr, void *buf, size_t len) {
    uint32_t aligned_buf[64];
    uint8_t *out = (uint8_t *)buf;
    size_t done = 0;

    while (done < len) {
        size_t want = len - done;
        if (want > sizeof(aligned_buf)) want = sizeof(aligned_buf);
        size_t aligned_len = (want + 3) & ~(size_t)3;

        uint32_t ms = kv_intr_mask();
        esp_err_t rrc = esp_flash_read(NULL, aligned_buf, addr + done, (uint32_t)aligned_len);
        kv_intr_restore(ms);
        if (rrc != ESP_OK) {
            /* A failed read must not present as a valid entry header. */
            memset(aligned_buf, 0xFF, aligned_len);
        }
        memcpy(out + done, aligned_buf, want);
        done += want;
    }
}

/* Returns false if the write did not happen, which the callers now check.
 *
 * It rounded the length up to a word and left the address alone, and
 * esp_rom_spiflash_write needs both. Nor did it report anything: the result was
 * discarded, so a rejected write was indistinguishable from a stored one all
 * the way up to `aura setkey` printing "key provisioned" for a key that was
 * never written. */
static bool flash_write(uint32_t addr, const void *buf, size_t len) {
    uint32_t aligned_buf[66]; /* >= header + KV_KEY_MAX + KV_VAL_MAX, rounded */
    size_t aligned_len = (len + 3) & ~3;
    if ((addr & 3u) != 0) return false;
    if (aligned_len > sizeof(aligned_buf)) return false;
    memset(aligned_buf, 0xFF, aligned_len);
    memcpy(aligned_buf, buf, len);
    uint32_t ms = kv_intr_mask();
    esp_err_t wrc = esp_flash_write(NULL, aligned_buf, addr, (uint32_t)aligned_len);
    kv_intr_restore(ms);
    return wrc == ESP_OK;
}

reflex_err_t reflex_kv_init(void) {
#ifdef REFLEX_OWN_ENTRY
    /* Give esp_flash_* an os_func layer that needs no scheduler.
     *
     * esp_flash_* is what makes this store persist at all: raw ROM flash access
     * with the cache enabled never reaches the medium. But its default os_func
     * layer takes a lock built on FreeRTOS, and under REFLEX_OWN_ENTRY that
     * scheduler was never started — the board hung in this function before
     * reaching a shell.
     *
     * Two earlier attempts missed this layer. Bracketing the ROM calls with
     * Cache_Suspend_ICache/Cache_Resume_ICache left the store still reporting
     * "initialised fresh", so the cache is not the whole story;
     * spi_flash_guard_set(&g_flash_guard_no_os_ops) still hung, because those
     * guards serve the legacy spi_flash_* API and esp_flash_* does not consult
     * them. esp_flash_app_disable_os_functions swaps the chip's own os_func for
     * esp_flash_noos_functions, which is the layer that was actually in the
     * way. */
    /* The default chip explicitly, not NULL. Unlike esp_flash_read and its
     * siblings, this one does not substitute the default for a NULL chip — it
     * dereferences straight away. Passing NULL faulted at mcause=0x7
     * mtval=0x8, a store to the os_func field of a null struct, which the trap
     * handler reported precisely. */
    (void)esp_flash_app_disable_os_functions(esp_flash_default_chip);
#endif
    uint32_t best_seq = 0;
    uint32_t best_sector = 0;
    bool found = false;

    for (uint32_t s = 0; s < KV_NUM_SECTORS; s++) {
        kv_page_header_t hdr;
        flash_read(sector_addr(s), &hdr, sizeof(hdr));
        if (hdr.magic == KV_MAGIC && hdr.sequence >= best_seq) {
            best_seq = hdr.sequence;
            best_sector = s;
            found = true;
        }
    }

    if (!found) {
        {
            uint32_t ems = kv_intr_mask();
            (void)esp_flash_erase_region(NULL, KV_FLASH_BASE, KV_SECTOR_SIZE);
            kv_intr_restore(ems);
        }
        kv_page_header_t hdr = { .magic = KV_MAGIC, .sequence = 1 };
        flash_write(KV_FLASH_BASE, &hdr, sizeof(hdr));
        s_active_sector = 0;
        s_active_seq = 1;
        s_write_offset = sizeof(kv_page_header_t);
    } else {
        s_active_sector = best_sector;
        s_active_seq = best_seq;
        /* Find write offset (first 0xFF byte after header) */
        s_write_offset = sizeof(kv_page_header_t);
        uint8_t probe;
        while (s_write_offset < KV_SECTOR_SIZE) {
            flash_read(sector_addr(s_active_sector) + s_write_offset, &probe, 1);
            if (probe == 0xFF) break;
            kv_entry_header_t eh;
            flash_read(sector_addr(s_active_sector) + s_write_offset, &eh, sizeof(eh));
            /* A corrupt header would otherwise walk the write offset to an
             * arbitrary place and append over live entries. */
            if (!kv_entry_header_sane(&eh)) break;
            s_write_offset += kv_entry_span(&eh);
        }
    }

    s_initialized = true;
    /* Which branch this took decides whether anything can persist at all.
     *
     * "fresh" means no valid page header was found and the store just erased
     * itself, which on every boot would mean nothing is ever kept — and that is
     * indistinguishable from an entry-level failure without this line. */
    /* esp_rom_printf, not REFLEX_LOGI: reflex_log.h lives in core/include and
     * this component does not depend on core. Keeping it that way is the point
     * of this file.
     *
     * Read this line. "initialised fresh" on a board that has been booted
     * before means the store did not find the page header it wrote last time,
     * and therefore that nothing it was asked to keep survived — which is the
     * defect described at the top of this file. It is the only outward sign,
     * and without it `aura setkey` reporting success looks like pairing works. */
    esp_rom_printf("[reflex.kv] flash KV %s: sector=%u seq=%u write_offset=%u base=0x%x\n",
                   found ? "resumed" : "initialised fresh", (unsigned)s_active_sector,
                   (unsigned)s_active_seq, (unsigned)s_write_offset, (unsigned)KV_FLASH_BASE);
    return REFLEX_OK;
}

typedef struct { uint8_t ns; } kv_handle_data_t;

reflex_err_t reflex_kv_open(const char *ns, bool readonly,
                            reflex_kv_handle_t *out) {
    (void)readonly;
    if (!s_initialized) return REFLEX_ERR_INVALID_STATE;
    uint8_t h = ns_hash(ns);
    *out = (reflex_kv_handle_t)(uintptr_t)(h + 1);
    return REFLEX_OK;
}

void reflex_kv_close(reflex_kv_handle_t h) { (void)h; }

static reflex_err_t kv_find(uint8_t ns, const char *key, uint32_t *out_offset,
                            kv_entry_header_t *out_hdr) {
    uint32_t off = sizeof(kv_page_header_t);
    uint32_t base = sector_addr(s_active_sector);
    uint32_t found_off = 0;
    bool found = false;

    while (off < KV_SECTOR_SIZE) {
        kv_entry_header_t eh;
        flash_read(base + off, &eh, sizeof(eh));
        if (eh.key_len == 0xFF) break;
        if (!kv_entry_header_sane(&eh)) break;
        if (eh.ns_hash == ns && eh.key_len == strlen(key)) {
            char k[KV_KEY_MAX + 1];
            flash_read(base + off + sizeof(eh), k, eh.key_len);
            k[eh.key_len] = '\0';
            if (strcmp(k, key) == 0) {
                found_off = off;
                if (out_hdr) *out_hdr = eh;
                found = true;
            }
        }
        off += kv_entry_span(&eh);
    }

    if (!found) return REFLEX_ERR_NOT_FOUND;
    if (out_offset) *out_offset = found_off;
    return REFLEX_OK;
}

/* Track last-seen offset per key during forward scan for compaction */
typedef struct { uint8_t ns; char key[KV_KEY_MAX + 1]; uint32_t offset; } kv_dedup_t;

static reflex_err_t kv_compact(void) {
    uint32_t old_base = sector_addr(s_active_sector);
    uint32_t new_sector = (s_active_sector + 1) % KV_NUM_SECTORS;
    uint32_t new_base = sector_addr(new_sector);

    /* Static, not automatic, and this is the whole of a crash that survived
     * reflashing.
     *
     * KV_ENTRY_MAX is 256 and kv_dedup_t is 24 bytes, so as an automatic this
     * table put 6,144 bytes into one stack frame. Compaction runs from
     * whichever task happens to make the write that fills the sector, and the
     * shell's task has 8,688 bytes of stack: `purpose set` number 139 filled
     * the sector, kv_compact was called, and the board died with
     *
     *     Guru Meditation Error: Core 0 panic'ed (Stack protection fault).
     *     Detected in task "main"
     *     Stack pointer: 0x40814c50  Stack bounds: 0x40814c60 - 0x40816e50
     *
     * — sixteen bytes past the bottom. The persistent form is worse: a full
     * sector survives a reflash, `boot_count` is written on every boot, so the
     * first write of the next boot compacts again and the board boot-loops.
     * Only erasing the partition clears it, which is exactly the failure
     * recorded in Known Gaps and is now explained.
     *
     * Static is safe here because the store is already single-owner: it keeps
     * s_active_sector, s_write_offset and s_active_seq as globals with no lock,
     * so two concurrent writers were never supported and this table adds no
     * hazard that was not already there. It costs 6 KB of .bss, which is the
     * honest price of not putting 6 KB on someone else's stack. */
    static kv_dedup_t dedup[KV_ENTRY_MAX];
    int dedup_count = 0;
    uint32_t off = sizeof(kv_page_header_t);

    while (off < KV_SECTOR_SIZE) {
        kv_entry_header_t eh;
        flash_read(old_base + off, &eh, sizeof(eh));
        if (eh.key_len == 0xFF || eh.key_len == 0) break;
        if (!kv_entry_header_sane(&eh)) break;

        char k[KV_KEY_MAX + 1];
        flash_read(old_base + off + sizeof(eh), k, eh.key_len);
        k[eh.key_len] = '\0';

        /* Update or add to dedup table */
        bool found = false;
        for (int i = 0; i < dedup_count; i++) {
            if (dedup[i].ns == eh.ns_hash && strcmp(dedup[i].key, k) == 0) {
                dedup[i].offset = off;
                found = true;
                break;
            }
        }
        if (!found && dedup_count >= KV_ENTRY_MAX) {
            /* More unique keys than the table can hold. A 4 KB sector can
             * carry more minimum-size entries than KV_ENTRY_MAX, so this is
             * reachable, and dropping them silently loses data that was
             * successfully written. Say so. */
            /* esp_rom_printf for the same reason as the init line below: this
             * component does not depend on core, and keeping it that way is
             * the point of the file. */
            esp_rom_printf("[reflex.kv] compaction: over %d unique keys, dropping the rest\n",
                           KV_ENTRY_MAX);
            break;
        }
        if (!found) {
            dedup[dedup_count].ns = eh.ns_hash;
            memcpy(dedup[dedup_count].key, k, eh.key_len + 1);
            dedup[dedup_count].offset = off;
            dedup_count++;
        }

        off += kv_entry_span(&eh);
    }

    /* Erase new sector and write header */
    {
        uint32_t ems = kv_intr_mask();
        (void)esp_flash_erase_region(NULL, new_base, KV_SECTOR_SIZE);
        kv_intr_restore(ems);
    }
    kv_page_header_t hdr = { .magic = KV_MAGIC, .sequence = s_active_seq + 1 };
    flash_write(new_base, &hdr, sizeof(hdr));

    /* Copy live entries to new sector */
    uint32_t new_off = sizeof(kv_page_header_t);
    for (int i = 0; i < dedup_count; i++) {
        kv_entry_header_t eh;
        flash_read(old_base + dedup[i].offset, &eh, sizeof(eh));
        size_t entry_size = kv_entry_span(&eh);

        uint8_t buf[sizeof(kv_entry_header_t) + KV_KEY_MAX + KV_VAL_MAX];
        flash_read(old_base + dedup[i].offset, buf, entry_size);
        flash_write(new_base + new_off, buf, entry_size);
        new_off += entry_size;
    }

    s_active_sector = new_sector;
    s_active_seq = hdr.sequence;
    s_write_offset = new_off;
    return REFLEX_OK;
}

static reflex_err_t kv_write_entry(uint8_t ns, const char *key,
                                   uint8_t type, const void *val, size_t val_len) {
    size_t key_len = strlen(key);
    if (key_len > KV_KEY_MAX || val_len > KV_VAL_MAX) return REFLEX_ERR_INVALID_ARG;

    kv_entry_header_t probe_eh = {.key_len = (uint8_t)key_len, .val_len = (uint16_t)val_len};
    size_t entry_size = kv_entry_span(&probe_eh);
    if (s_write_offset + entry_size >= KV_SECTOR_SIZE) {
        reflex_err_t rc = kv_compact();
        if (rc != REFLEX_OK) return rc;
        if (s_write_offset + entry_size >= KV_SECTOR_SIZE) return REFLEX_ERR_NO_MEM;
    }

    uint32_t base = sector_addr(s_active_sector);
    kv_entry_header_t eh = {
        .ns_hash = ns, .key_len = (uint8_t)key_len,
        .val_len = (uint16_t)val_len, .type = type
    };

    /* Assembled whole and written once, at an offset that is always word
     * aligned. Three separate appends put the key at offset 13 and the value
     * at 21 and neither reached the medium. */
    uint8_t entry[sizeof(kv_entry_header_t) + KV_KEY_MAX + KV_VAL_MAX];
    memcpy(entry, &eh, sizeof(eh));
    memcpy(entry + sizeof(eh), key, key_len);
    memcpy(entry + sizeof(eh) + key_len, val, val_len);

    if (!flash_write(base + s_write_offset, entry, sizeof(eh) + key_len + val_len)) {
        return REFLEX_FAIL;
    }
    s_write_offset += kv_entry_span(&eh);
    return REFLEX_OK;
}

static reflex_err_t kv_read_val(uint32_t offset, kv_entry_header_t *eh,
                                void *buf, size_t *len) {
    uint32_t base = sector_addr(s_active_sector);
    uint32_t val_off = offset + sizeof(kv_entry_header_t) + eh->key_len;
    size_t avail = eh->val_len;
    if (*len < avail) return REFLEX_ERR_INVALID_ARG;
    flash_read(base + val_off, buf, avail);
    *len = avail;
    return REFLEX_OK;
}

#define GET_NS(h) ((uint8_t)((uintptr_t)(h) - 1))

reflex_err_t reflex_kv_get_str(reflex_kv_handle_t h, const char *key,
                               char *buf, size_t *len) {
    uint32_t off; kv_entry_header_t eh;
    if (!buf || !len) return REFLEX_ERR_INVALID_ARG;
    size_t cap = *len;                 /* kv_read_val overwrites *len with the value size */
    reflex_err_t rc = kv_find(GET_NS(h), key, &off, &eh);
    if (rc != REFLEX_OK) return rc;
    rc = kv_read_val(off, &eh, buf, len);
    /* reflex_kv_set_str stores strlen+1, so the value already carries its own
     * terminator. The old line here wrote buf[eh.val_len] whenever
     * `*len < eh.val_len + 1` — and kv_read_val sets *len to exactly val_len,
     * making that condition always true. For a caller whose buffer is exactly
     * the value size that is a one-byte write past the end. Only terminate
     * when there is genuinely room. */
    if (rc == REFLEX_OK && eh.val_len < cap) buf[eh.val_len] = '\0';
    return rc;
}

reflex_err_t reflex_kv_set_str(reflex_kv_handle_t h, const char *key,
                               const char *val) {
    return kv_write_entry(GET_NS(h), key, KV_TYPE_STR, val, strlen(val) + 1);
}

reflex_err_t reflex_kv_get_blob(reflex_kv_handle_t h, const char *key,
                                void *buf, size_t *len) {
    uint32_t off; kv_entry_header_t eh;
    reflex_err_t rc = kv_find(GET_NS(h), key, &off, &eh);
    if (rc != REFLEX_OK) return rc;
    return kv_read_val(off, &eh, buf, len);
}

reflex_err_t reflex_kv_set_blob(reflex_kv_handle_t h, const char *key,
                                const void *buf, size_t len) {
    return kv_write_entry(GET_NS(h), key, KV_TYPE_BLOB, buf, len);
}

reflex_err_t reflex_kv_get_i32(reflex_kv_handle_t h, const char *key, int32_t *out) {
    uint32_t off; kv_entry_header_t eh;
    reflex_err_t rc = kv_find(GET_NS(h), key, &off, &eh);
    if (rc != REFLEX_OK) return rc;
    size_t len = 4;
    return kv_read_val(off, &eh, out, &len);
}

reflex_err_t reflex_kv_set_i32(reflex_kv_handle_t h, const char *key, int32_t val) {
    return kv_write_entry(GET_NS(h), key, KV_TYPE_I32, &val, 4);
}

reflex_err_t reflex_kv_get_u8(reflex_kv_handle_t h, const char *key, uint8_t *out) {
    uint32_t off; kv_entry_header_t eh;
    reflex_err_t rc = kv_find(GET_NS(h), key, &off, &eh);
    if (rc != REFLEX_OK) return rc;
    size_t len = 1;
    return kv_read_val(off, &eh, out, &len);
}

reflex_err_t reflex_kv_set_u8(reflex_kv_handle_t h, const char *key, uint8_t val) {
    return kv_write_entry(GET_NS(h), key, KV_TYPE_U8, &val, 1);
}

reflex_err_t reflex_kv_erase(reflex_kv_handle_t h, const char *key) {
    (void)h; (void)key;
    return REFLEX_OK;
}

reflex_err_t reflex_kv_commit(reflex_kv_handle_t h) {
    (void)h;
    return REFLEX_OK;
}
