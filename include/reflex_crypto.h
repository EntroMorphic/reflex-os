#ifndef REFLEX_CRYPTO_H
#define REFLEX_CRYPTO_H

/**
 * @file reflex_crypto.h
 * @brief Reflex OS portable cryptographic primitives.
 *
 * Currently provides HMAC-SHA256 for the Aura authentication protocol.
 * ESP-IDF backend uses mbedtls. Future backends may use hardware
 * accelerators or minimal software implementations.
 */

#include "reflex_types.h"

#ifdef __cplusplus
extern "C" {
#endif

reflex_err_t reflex_hmac_sha256(const uint8_t *key, size_t key_len,
                                const uint8_t *data, size_t data_len,
                                uint8_t out[32]);

#ifdef __cplusplus
}
#endif

#endif /* REFLEX_CRYPTO_H */
