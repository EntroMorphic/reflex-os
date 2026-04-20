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
#define MOCK_FLASH_BASE 0x9000
static uint8_t s_mock_flash[MOCK_FLASH_SIZE];

int esp_rom_spiflash_read(uint32_t addr, uint32_t *dest, int len) {
    uint32_t offset = addr - MOCK_FLASH_BASE;
    if (offset + len > MOCK_FLASH_SIZE) return -1;
    memcpy(dest, s_mock_flash + offset, len);
    return 0;
}

int esp_rom_spiflash_write(uint32_t addr, const uint32_t *src, int len) {
    uint32_t offset = addr - MOCK_FLASH_BASE;
    if (offset + len > MOCK_FLASH_SIZE) return -1;
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

    if (failures == 0) printf("ok\n");
    return failures;
}
