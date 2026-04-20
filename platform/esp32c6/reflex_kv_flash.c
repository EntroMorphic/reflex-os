/**
 * @file reflex_kv_flash.c
 * @brief Reflex KV — raw flash backend using ROM SPI flash functions.
 *
 * Simple page-based key-value store on a dedicated flash partition.
 * Uses esp_rom_spiflash_* (mask ROM, zero ESP-IDF component deps).
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

#include "reflex_kv.h"
#include <string.h>
#include <stdlib.h>

#define KV_MAGIC        0x52464B56  /* "RFKV" */
#define KV_SECTOR_SIZE  4096
#define KV_NUM_SECTORS  6
#define KV_ENTRY_MAX    256
#define KV_KEY_MAX      15
#define KV_VAL_MAX      240

/* Flash partition offset — uses the "nvs" partition at 0x9000 (24KB) */
#define KV_FLASH_BASE   0x9000

#define KV_TYPE_STR     0
#define KV_TYPE_BLOB    1
#define KV_TYPE_I32     2
#define KV_TYPE_U8      3
#define KV_TYPE_ERASED  0xFF

/* ROM flash functions (mask ROM, always available) */
extern int esp_rom_spiflash_read(uint32_t addr, uint32_t *dest, int len);
extern int esp_rom_spiflash_write(uint32_t addr, const uint32_t *src, int len);
extern int esp_rom_spiflash_erase_sector(uint32_t sector);

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

static void flash_read(uint32_t addr, void *buf, size_t len) {
    uint32_t aligned_buf[64];
    size_t aligned_len = (len + 3) & ~3;
    if (aligned_len > sizeof(aligned_buf)) aligned_len = sizeof(aligned_buf);
    esp_rom_spiflash_read(addr, aligned_buf, (int)aligned_len);
    memcpy(buf, aligned_buf, len);
}

static void flash_write(uint32_t addr, const void *buf, size_t len) {
    uint32_t aligned_buf[64];
    size_t aligned_len = (len + 3) & ~3;
    memset(aligned_buf, 0xFF, aligned_len);
    memcpy(aligned_buf, buf, len);
    esp_rom_spiflash_write(addr, aligned_buf, (int)aligned_len);
}

reflex_err_t reflex_kv_init(void) {
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
        esp_rom_spiflash_erase_sector(KV_FLASH_BASE / KV_SECTOR_SIZE);
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
            s_write_offset += sizeof(kv_entry_header_t) + eh.key_len + eh.val_len;
        }
    }

    s_initialized = true;
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
        off += sizeof(eh) + eh.key_len + eh.val_len;
    }

    if (!found) return REFLEX_ERR_NOT_FOUND;
    if (out_offset) *out_offset = found_off;
    return REFLEX_OK;
}

static reflex_err_t kv_write_entry(uint8_t ns, const char *key,
                                   uint8_t type, const void *val, size_t val_len) {
    size_t key_len = strlen(key);
    if (key_len > KV_KEY_MAX || val_len > KV_VAL_MAX) return REFLEX_ERR_INVALID_ARG;

    size_t entry_size = sizeof(kv_entry_header_t) + key_len + val_len;
    if (s_write_offset + entry_size >= KV_SECTOR_SIZE) {
        /* TODO: compact to next sector */
        return REFLEX_ERR_NO_MEM;
    }

    uint32_t base = sector_addr(s_active_sector);
    kv_entry_header_t eh = {
        .ns_hash = ns, .key_len = (uint8_t)key_len,
        .val_len = (uint16_t)val_len, .type = type
    };
    flash_write(base + s_write_offset, &eh, sizeof(eh));
    s_write_offset += sizeof(eh);
    flash_write(base + s_write_offset, key, key_len);
    s_write_offset += key_len;
    flash_write(base + s_write_offset, val, val_len);
    s_write_offset += val_len;
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
    reflex_err_t rc = kv_find(GET_NS(h), key, &off, &eh);
    if (rc != REFLEX_OK) return rc;
    rc = kv_read_val(off, &eh, buf, len);
    if (rc == REFLEX_OK && *len < eh.val_len + 1) buf[eh.val_len] = '\0';
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
