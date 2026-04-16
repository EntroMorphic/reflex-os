/**
 * @file reflex_crypto_esp32c6.c
 * @brief Reflex Crypto — mbedtls backend.
 */

#include "reflex_crypto.h"
#include "mbedtls/md.h"

reflex_err_t reflex_hmac_sha256(const uint8_t *key, size_t key_len,
                                const uint8_t *data, size_t data_len,
                                uint8_t out[32]) {
    mbedtls_md_context_t ctx;
    mbedtls_md_init(&ctx);
    const mbedtls_md_info_t *md = mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
    if (!md) { mbedtls_md_free(&ctx); return REFLEX_FAIL; }
    if (mbedtls_md_setup(&ctx, md, 1) != 0 ||
        mbedtls_md_hmac_starts(&ctx, key, key_len) != 0 ||
        mbedtls_md_hmac_update(&ctx, data, data_len) != 0) {
        mbedtls_md_free(&ctx);
        return REFLEX_FAIL;
    }
    int rc = mbedtls_md_hmac_finish(&ctx, out);
    mbedtls_md_free(&ctx);
    return (rc == 0) ? REFLEX_OK : REFLEX_FAIL;
}
