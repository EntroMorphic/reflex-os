#ifndef REFLEX_TERNARY_H
#define REFLEX_TERNARY_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define REFLEX_TRYTE9_TRITS 9
#define REFLEX_WORD18_TRITS 18

#define REFLEX_PACKED_TRIT_BITS 2
#define REFLEX_PACKED_TRIT_MASK 0x3u

#define REFLEX_PACKED_TRYTE9_BYTES 3
#define REFLEX_PACKED_WORD18_BYTES 5

typedef enum {
    REFLEX_TRIT_NEG = -1,
    REFLEX_TRIT_ZERO = 0,
    REFLEX_TRIT_POS = 1,
} reflex_trit_t;

typedef struct {
    reflex_trit_t trits[REFLEX_TRYTE9_TRITS];
} reflex_tryte9_t;

typedef struct {
    reflex_trit_t trits[REFLEX_WORD18_TRITS];
} reflex_word18_t;

bool reflex_trit_is_valid(int value);
bool reflex_tryte9_is_valid(const reflex_tryte9_t *value);
bool reflex_word18_is_valid(const reflex_word18_t *value);
bool reflex_tryte9_equal(const reflex_tryte9_t *lhs, const reflex_tryte9_t *rhs);
bool reflex_word18_equal(const reflex_word18_t *lhs, const reflex_word18_t *rhs);
esp_err_t reflex_word18_compare(const reflex_word18_t *lhs,
                                const reflex_word18_t *rhs,
                                reflex_trit_t *out);

esp_err_t reflex_word18_add(const reflex_word18_t *lhs,
                            const reflex_word18_t *rhs,
                            reflex_word18_t *out);
esp_err_t reflex_word18_subtract(const reflex_word18_t *lhs,
                                 const reflex_word18_t *rhs,
                                 reflex_word18_t *out);
esp_err_t reflex_word18_select(reflex_trit_t selector,
                               const reflex_word18_t *negative,
                               const reflex_word18_t *zero,
                               const reflex_word18_t *positive,
                               reflex_word18_t *out);
esp_err_t reflex_word18_from_int32(int32_t value, reflex_word18_t *out);
esp_err_t reflex_word18_to_int32(const reflex_word18_t *value, int32_t *out);

esp_err_t reflex_tryte9_pack(const reflex_tryte9_t *value,
                             uint8_t out[REFLEX_PACKED_TRYTE9_BYTES]);
esp_err_t reflex_tryte9_unpack(const uint8_t in[REFLEX_PACKED_TRYTE9_BYTES],
                               reflex_tryte9_t *out);

esp_err_t reflex_word18_pack(const reflex_word18_t *value,
                             uint8_t out[REFLEX_PACKED_WORD18_BYTES]);
esp_err_t reflex_word18_unpack(const uint8_t in[REFLEX_PACKED_WORD18_BYTES],
                               reflex_word18_t *out);

esp_err_t reflex_ternary_self_check(void);

#ifdef __cplusplus
}
#endif

#endif
