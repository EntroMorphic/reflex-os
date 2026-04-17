/**
 * @file reflex_crypto_esp32c6.c
 * @brief Reflex Crypto — standalone SHA-256 + HMAC (no mbedtls).
 *
 * FIPS 180-4 compliant SHA-256 and RFC 2104 HMAC-SHA-256.
 * Replaces the mbedtls dependency with ~150 lines of portable C.
 */

#include "reflex_crypto.h"
#include <string.h>

/* ---- SHA-256 (FIPS 180-4) ---- */

static const uint32_t K[64] = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5,
    0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
    0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3,
    0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
    0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc,
    0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7,
    0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
    0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13,
    0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
    0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3,
    0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5,
    0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
    0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208,
    0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2,
};

#define RR(x, n) (((x) >> (n)) | ((x) << (32 - (n))))
#define CH(x,y,z) (((x) & (y)) ^ (~(x) & (z)))
#define MAJ(x,y,z) (((x) & (y)) ^ ((x) & (z)) ^ ((y) & (z)))
#define EP0(x) (RR(x,2) ^ RR(x,13) ^ RR(x,22))
#define EP1(x) (RR(x,6) ^ RR(x,11) ^ RR(x,25))
#define SIG0(x) (RR(x,7) ^ RR(x,18) ^ ((x) >> 3))
#define SIG1(x) (RR(x,17) ^ RR(x,19) ^ ((x) >> 10))

typedef struct {
    uint32_t state[8];
    uint64_t bitlen;
    uint8_t buf[64];
    uint32_t buflen;
} sha256_ctx_t;

static void sha256_transform(sha256_ctx_t *ctx) {
    uint32_t w[64], a, b, c, d, e, f, g, h, t1, t2;
    for (int i = 0; i < 16; i++)
        w[i] = ((uint32_t)ctx->buf[i*4] << 24) | ((uint32_t)ctx->buf[i*4+1] << 16)
             | ((uint32_t)ctx->buf[i*4+2] << 8) | ctx->buf[i*4+3];
    for (int i = 16; i < 64; i++)
        w[i] = SIG1(w[i-2]) + w[i-7] + SIG0(w[i-15]) + w[i-16];

    a = ctx->state[0]; b = ctx->state[1]; c = ctx->state[2]; d = ctx->state[3];
    e = ctx->state[4]; f = ctx->state[5]; g = ctx->state[6]; h = ctx->state[7];

    for (int i = 0; i < 64; i++) {
        t1 = h + EP1(e) + CH(e,f,g) + K[i] + w[i];
        t2 = EP0(a) + MAJ(a,b,c);
        h = g; g = f; f = e; e = d + t1;
        d = c; c = b; b = a; a = t1 + t2;
    }

    ctx->state[0] += a; ctx->state[1] += b; ctx->state[2] += c; ctx->state[3] += d;
    ctx->state[4] += e; ctx->state[5] += f; ctx->state[6] += g; ctx->state[7] += h;
}

static void sha256_init(sha256_ctx_t *ctx) {
    ctx->state[0] = 0x6a09e667; ctx->state[1] = 0xbb67ae85;
    ctx->state[2] = 0x3c6ef372; ctx->state[3] = 0xa54ff53a;
    ctx->state[4] = 0x510e527f; ctx->state[5] = 0x9b05688c;
    ctx->state[6] = 0x1f83d9ab; ctx->state[7] = 0x5be0cd19;
    ctx->bitlen = 0;
    ctx->buflen = 0;
}

static void sha256_update(sha256_ctx_t *ctx, const uint8_t *data, size_t len) {
    for (size_t i = 0; i < len; i++) {
        ctx->buf[ctx->buflen++] = data[i];
        if (ctx->buflen == 64) {
            sha256_transform(ctx);
            ctx->bitlen += 512;
            ctx->buflen = 0;
        }
    }
}

static void sha256_final(sha256_ctx_t *ctx, uint8_t out[32]) {
    uint32_t i = ctx->buflen;
    ctx->buf[i++] = 0x80;
    if (i > 56) {
        while (i < 64) ctx->buf[i++] = 0;
        sha256_transform(ctx);
        i = 0;
    }
    while (i < 56) ctx->buf[i++] = 0;
    ctx->bitlen += ctx->buflen * 8;
    for (int j = 7; j >= 0; j--)
        ctx->buf[56 + (7 - j)] = (uint8_t)(ctx->bitlen >> (j * 8));
    sha256_transform(ctx);
    for (int j = 0; j < 8; j++) {
        out[j*4]   = (uint8_t)(ctx->state[j] >> 24);
        out[j*4+1] = (uint8_t)(ctx->state[j] >> 16);
        out[j*4+2] = (uint8_t)(ctx->state[j] >> 8);
        out[j*4+3] = (uint8_t)(ctx->state[j]);
    }
}

/* ---- HMAC-SHA-256 (RFC 2104) ---- */

reflex_err_t reflex_hmac_sha256(const uint8_t *key, size_t key_len,
                                const uint8_t *data, size_t data_len,
                                uint8_t out[32]) {
    sha256_ctx_t ctx;
    uint8_t k_pad[64];
    uint8_t k_hash[32];
    const uint8_t *k_use = key;
    size_t k_use_len = key_len;

    if (key_len > 64) {
        sha256_init(&ctx);
        sha256_update(&ctx, key, key_len);
        sha256_final(&ctx, k_hash);
        k_use = k_hash;
        k_use_len = 32;
    }

    /* Inner: SHA-256(K ^ ipad || data) */
    memset(k_pad, 0x36, 64);
    for (size_t i = 0; i < k_use_len; i++) k_pad[i] ^= k_use[i];

    sha256_init(&ctx);
    sha256_update(&ctx, k_pad, 64);
    sha256_update(&ctx, data, data_len);
    uint8_t inner[32];
    sha256_final(&ctx, inner);

    /* Outer: SHA-256(K ^ opad || inner) */
    memset(k_pad, 0x5c, 64);
    for (size_t i = 0; i < k_use_len; i++) k_pad[i] ^= k_use[i];

    sha256_init(&ctx);
    sha256_update(&ctx, k_pad, 64);
    sha256_update(&ctx, inner, 32);
    sha256_final(&ctx, out);

    return REFLEX_OK;
}
