/**
 * @file test_kv.c
 * @brief Test KV store compaction logic on host.
 *
 * Uses a RAM-backed flash mock (overrides esp_rom_spiflash_* functions).
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "reflex_types.h"

/* RAM-backed flash mock (24KB = 6 sectors of 4KB) */
#define MOCK_FLASH_SIZE (6 * 4096)
/* Must track KV_FLASH_BASE in reflex_kv_flash.c: the store moved to its own
 * `reflexkv` partition after it was found sharing the six sectors NVS occupies. */
#define MOCK_FLASH_BASE 0x10000
#define KV_KEY_MAX_TEST 15

static uint8_t s_mock_flash[MOCK_FLASH_SIZE];

/* The store talks to ESP-IDF's flash API now, because raw ROM access never
 * reached the medium. The mock follows it, and keeps refusing unaligned writes
 * the way the chip does. */
int esp_flash_read(void *chip, void *buf, uint32_t addr, uint32_t len) {
    (void)chip;
    uint32_t offset = addr - MOCK_FLASH_BASE;
    if (offset + len > MOCK_FLASH_SIZE) return -1;
    memcpy(buf, s_mock_flash + offset, len);
    return 0;
}

int esp_flash_write(void *chip, const void *buf, uint32_t addr, uint32_t len) {
    (void)chip;
    uint32_t offset = addr - MOCK_FLASH_BASE;
    if (offset + len > MOCK_FLASH_SIZE) return -1;
    if ((addr & 3u) != 0) return -1;
    for (uint32_t i = 0; i < len; i++) {
        s_mock_flash[offset + i] &= ((const uint8_t *)buf)[i];
    }
    return 0;
}

int esp_flash_erase_region(void *chip, uint32_t start, uint32_t size) {
    (void)chip;
    uint32_t offset = start - MOCK_FLASH_BASE;
    if (offset + size > MOCK_FLASH_SIZE) return -1;
    memset(s_mock_flash + offset, 0xFF, size);
    return 0;
}

int esp_rom_spiflash_read(uint32_t addr, uint32_t *dest, int len) {
    uint32_t offset = addr - MOCK_FLASH_BASE;
    if (offset + len > MOCK_FLASH_SIZE) return -1;
    memcpy(dest, s_mock_flash + offset, len);
    return 0;
}

/* reflex_kv_init reports which branch it took; on the host that goes nowhere. */
int esp_rom_printf(const char *fmt, ...) {
    (void)fmt;
    return 0;
}

int esp_rom_spiflash_unlock(void) {
    return 0; /* the real ROM clears block protection; the mock has none */
}

int esp_rom_spiflash_write(uint32_t addr, const uint32_t *src, int len) {
    uint32_t offset = addr - MOCK_FLASH_BASE;
    if (offset + len > MOCK_FLASH_SIZE) return -1;
    /* The hardware requires a word-aligned destination and silently does
     * nothing otherwise. A mock that accepts any address cannot catch the bug
     * where entries were appended at offsets 13 and 21 and never reached the
     * medium, so it refuses them the way the chip does. */
    if ((addr & 3u) != 0) return -1;
    /* Flash write: can only clear bits (AND with existing) */
    for (int i = 0; i < len; i++) {
        s_mock_flash[offset + i] &= ((const uint8_t *)src)[i];
    }
    return 0;
}

int esp_rom_spiflash_erase_sector(uint32_t sector) {
    uint32_t offset = (sector * 4096) - MOCK_FLASH_BASE;
    if (offset + 4096 > MOCK_FLASH_SIZE) return -1;
    memset(s_mock_flash + offset, 0xFF, 4096);
    return 0;
}

/* Include the KV implementation directly (it uses the above mocks) */
#include "../../platform/esp32c6/reflex_kv_flash.c"

#define CHECK_KV(name, expr) do { \
        if (!(expr)) { printf("FAIL %s\n", (name)); failures++; } \
    } while (0)

int test_kv(void) {
    int failures = 0;
    printf("[kv]      ");

    /* Reset mock flash to erased state */
    memset(s_mock_flash, 0xFF, MOCK_FLASH_SIZE);

    /* Init */
    s_initialized = false;
    if (reflex_kv_init() != REFLEX_OK) { printf("FAIL init\n"); return 1; }

    /* Basic write/read */
    reflex_kv_handle_t h;
    reflex_kv_open("test", false, &h);

    reflex_kv_set_str(h, "hello", "world");
    char buf[32]; size_t len = sizeof(buf);
    reflex_err_t rc = reflex_kv_get_str(h, "hello", buf, &len);
    if (rc != REFLEX_OK || strcmp(buf, "world") != 0) { printf("FAIL read\n"); failures++; }

    /* Overwrite triggers new entry (old one remains until compact) */
    reflex_kv_set_str(h, "hello", "earth");
    len = sizeof(buf);
    rc = reflex_kv_get_str(h, "hello", buf, &len);
    if (rc != REFLEX_OK || strcmp(buf, "earth") != 0) { printf("FAIL overwrite\n"); failures++; }

    /* Fill sector to trigger compaction */
    char key[16], val[64];
    int writes = 0;
    for (int i = 0; i < 200; i++) {
        snprintf(key, sizeof(key), "k%d", i % 20);
        snprintf(val, sizeof(val), "val_%d_pad_to_fill", i);
        rc = reflex_kv_set_str(h, key, val);
        if (rc != REFLEX_OK) break;
        writes++;
    }
    /* Should have written all 200 (compaction handles sector full) */
    if (writes < 100) { printf("FAIL fill (only %d writes)\n", writes); failures++; }

    /* After compaction(s), "hello" key should still be readable */
    len = sizeof(buf);
    rc = reflex_kv_get_str(h, "hello", buf, &len);
    if (rc != REFLEX_OK || strcmp(buf, "earth") != 0) {
        printf("FAIL post-compact hello\n"); failures++;
    }

    /* A maximum-size entry carried through compaction.
     *
     * This is the case that made flash_read's truncation reachable. An entry
     * of a 15-character key and a 240-byte value spans
     * sizeof(header) + 15 + 240 = 263 bytes, and kv_compact copies whole
     * entries — so compaction asked flash_read for 263 bytes, which it served
     * by reading 256 and then copying 263 out of a 256-byte stack array. The
     * tail of every such entry was whatever happened to be next on the stack.
     *
     * The mock cannot model a stack, but it does not have to: it returns the
     * right bytes, and the old flash_read still mangled them, so this fails
     * against the defect and passes against the fix. */
    {
        char bigkey[KV_KEY_MAX_TEST + 1];
        memset(bigkey, 'k', KV_KEY_MAX_TEST);
        bigkey[KV_KEY_MAX_TEST] = '\0';

        uint8_t big[240];
        for (size_t i = 0; i < sizeof(big); i++) big[i] = (uint8_t)(i * 7u + 3u);

        if (reflex_kv_set_blob(h, bigkey, big, sizeof(big)) != REFLEX_OK) {
            printf("FAIL max-entry write\n"); failures++;
        }

        /* Force at least one compaction so the entry is copied rather than
         * merely read in place. */
        for (int i = 0; i < 300; i++) {
            snprintf(key, sizeof(key), "f%d", i % 16);
            snprintf(val, sizeof(val), "pad_%d_________________", i);
            if (reflex_kv_set_str(h, key, val) != REFLEX_OK) break;
        }

        uint8_t back[240];
        size_t blen = sizeof(back);
        memset(back, 0, sizeof(back));
        rc = reflex_kv_get_blob(h, bigkey, back, &blen);
        if (rc != REFLEX_OK || blen != sizeof(big) || memcmp(back, big, sizeof(big)) != 0) {
            printf("FAIL max-entry survived compaction (rc=0x%x len=%u)\n",
                   (unsigned)rc, (unsigned)blen);
            failures++;
        }
    }

    /* A key rewritten after the dedup table is full must keep its new value.
     *
     * The store is append-only with the newest version of a key last, so a
     * compaction scan that gives up when its table fills carries the *old*
     * value forward — silently, for a key it is still tracking. That is the
     * regression a first attempt at reporting the overflow introduced: it
     * reported, and then it broke out of the scan.
     *
     * The ordering is the whole test. The rewrite has to appear *after* a key
     * the table had no room for, or the scan reaches it before it would have
     * given up and the bug is invisible — which is exactly how the first
     * version of this test passed against the defect it was written for.
     *
     * Sized to stay inside one 4 KB sector so no compaction happens early and
     * the layout is exactly as described: 300 four-character keys at a 12-byte
     * span is 3,600 bytes, plus two 16-byte "tracked" entries and the page
     * header. More unique keys than KV_ENTRY_MAX fit, so the overflow is
     * reachable rather than theoretical. */
    {
        memset(s_mock_flash, 0xFF, MOCK_FLASH_SIZE);
        s_initialized = false;
        if (reflex_kv_init() != REFLEX_OK) { printf("FAIL tracked reinit\n"); failures++; }

        reflex_kv_handle_t h2;
        reflex_kv_open("test", false, &h2);

        if (reflex_kv_set_str(h2, "tracked", "old") != REFLEX_OK) {
            printf("FAIL tracked seed\n"); failures++;
        }
        for (int i = 0; i < 300; i++) {
            snprintf(key, sizeof(key), "u%03d", i);
            if (reflex_kv_set_str(h2, key, "x") != REFLEX_OK) break;
        }
        /* After the table is already full of u-keys. */
        if (reflex_kv_set_str(h2, "tracked", "new") != REFLEX_OK) {
            printf("FAIL tracked rewrite\n"); failures++;
        }
        /* Overflow the sector with repeats of an existing key, so compaction
         * runs without introducing further unique keys. */
        for (int i = 0; i < 120; i++) {
            if (reflex_kv_set_str(h2, "u000", "y") != REFLEX_OK) break;
        }

        len = sizeof(buf);
        rc = reflex_kv_get_str(h2, "tracked", buf, &len);
        if (rc != REFLEX_OK || strcmp(buf, "new") != 0) {
            printf("FAIL tracked key reverted across compaction (rc=0x%x got=%s)\n",
                   (unsigned)rc, rc == REFLEX_OK ? buf : "-");
            failures++;
        }
    }

    /* reflex_kv_value_max() is a contract, not a hint.
     *
     * goose_snapshot_save now sizes its per-field blob against it, having
     * previously sized it at 612 bytes against a comment reading "well within
     * NVS limits" on a build that does not use NVS. If the reported maximum
     * were ever larger than what a write actually accepts, that caller would
     * silently go back to losing whole fields — so the number is pinned to the
     * behaviour here: exactly the maximum must be accepted, one byte more must
     * be refused. */
    {
        memset(s_mock_flash, 0xFF, MOCK_FLASH_SIZE);
        s_initialized = false;
        reflex_kv_init();
        reflex_kv_handle_t h3;
        reflex_kv_open("test", false, &h3);

        size_t vmax = reflex_kv_value_max();
        CHECK_KV("value max is a sane, non-zero size", vmax > 0 && vmax < KV_SECTOR_SIZE);

        static uint8_t at_max[512];
        for (size_t i = 0; i < sizeof(at_max); i++) at_max[i] = (uint8_t)i;

        if (vmax <= sizeof(at_max)) {
            CHECK_KV("a value of exactly the reported maximum is accepted",
                     reflex_kv_set_blob(h3, "atmax", at_max, vmax) == REFLEX_OK);
            CHECK_KV("one byte over the reported maximum is refused",
                     reflex_kv_set_blob(h3, "over", at_max, vmax + 1) != REFLEX_OK);

            uint8_t back2[512];
            size_t blen2 = sizeof(back2);
            CHECK_KV("the maximum-size value reads back intact",
                     reflex_kv_get_blob(h3, "atmax", back2, &blen2) == REFLEX_OK &&
                     blen2 == vmax && memcmp(back2, at_max, vmax) == 0);
        }
    }

    if (failures == 0) printf("ok\n");
    return failures;
}
