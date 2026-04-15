#ifndef REFLEX_TERNARY_H
#define REFLEX_TERNARY_H

/**
 * @file reflex_ternary.h
 * @brief Balanced ternary primitives: trit, tryte9, word18, and their
 * packed binary representations.
 *
 * Reflex OS uses balanced ternary values throughout the VM:
 *   -1 (NEG), 0 (ZERO), +1 (POS)
 *
 * A `tryte9` is 9 trits (roughly analogous to an 8-bit byte but
 * ternary). A `word18` is 18 trits (the VM register and memory
 * word). Packed forms exist for storage and wire transmission —
 * each trit takes 2 bits in the packed form (REFLEX_PACKED_TRIT_BITS).
 *
 * Every arithmetic and comparison helper returns an esp_err_t so
 * the ternary layer can be boundary-checked at its edges without
 * relying on ad-hoc sentinel values. Callers MUST check the return
 * code — a failed op leaves `out` unspecified.
 *
 * @note Shadow atlas coordinates (see `goose_make_shadow_coord`)
 * intentionally violate the balanced-ternary invariant on trits[6]
 * and trits[7], using those slots as opaque byte IDs. Trit-semantic
 * code must gate on cell type or avoid shadow coords.
 */

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

/** @brief A single balanced-ternary digit. */
typedef enum {
    REFLEX_TRIT_NEG = -1,
    REFLEX_TRIT_ZERO = 0,
    REFLEX_TRIT_POS = 1,
} reflex_trit_t;

/** @brief 9-trit ternary value. Used as a coordinate or small scalar. */
typedef struct {
    reflex_trit_t trits[REFLEX_TRYTE9_TRITS];
} reflex_tryte9_t;

/** @brief 18-trit ternary word. The VM register and memory type. */
typedef struct {
    reflex_trit_t trits[REFLEX_WORD18_TRITS];
} reflex_word18_t;

/** @brief Return true if @p value is in {-1, 0, +1}. */
bool reflex_trit_is_valid(int value);

/** @brief Return true if every trit in @p value is in the valid range. */
bool reflex_tryte9_is_valid(const reflex_tryte9_t *value);

/** @brief Return true if every trit in @p value is in the valid range. */
bool reflex_word18_is_valid(const reflex_word18_t *value);

/** @brief Byte-wise equality on `tryte9` values. Safe for opaque shadow coords. */
bool reflex_tryte9_equal(const reflex_tryte9_t *lhs, const reflex_tryte9_t *rhs);

/** @brief Byte-wise equality on `word18` values. */
bool reflex_word18_equal(const reflex_word18_t *lhs, const reflex_word18_t *rhs);

/**
 * @brief Balanced-ternary compare, writing the sign (-1/0/+1) into
 * @p out.
 */
esp_err_t reflex_word18_compare(const reflex_word18_t *lhs,
                                const reflex_word18_t *rhs,
                                reflex_trit_t *out);

/**
 * @brief Balanced-ternary addition with overflow detection.
 * Returns ESP_ERR_INVALID_STATE on overflow past 18 trits.
 */
esp_err_t reflex_word18_add(const reflex_word18_t *lhs,
                            const reflex_word18_t *rhs,
                            reflex_word18_t *out);

/**
 * @brief Balanced-ternary subtraction with overflow detection.
 */
esp_err_t reflex_word18_subtract(const reflex_word18_t *lhs,
                                 const reflex_word18_t *rhs,
                                 reflex_word18_t *out);

/**
 * @brief Three-way select: picks @p negative, @p zero, or @p
 * positive based on @p selector. Backs the TSEL opcode.
 */
esp_err_t reflex_word18_select(reflex_trit_t selector,
                               const reflex_word18_t *negative,
                               const reflex_word18_t *zero,
                               const reflex_word18_t *positive,
                               reflex_word18_t *out);

/**
 * @brief Convert a signed 32-bit integer to a word18.
 * Fails with ESP_ERR_INVALID_ARG if the value is outside the
 * representable 18-trit range (±(3^18-1)/2).
 */
esp_err_t reflex_word18_from_int32(int32_t value, reflex_word18_t *out);

/** @brief Inverse of reflex_word18_from_int32. */
esp_err_t reflex_word18_to_int32(const reflex_word18_t *value, int32_t *out);

/**
 * @brief Pack a tryte9 into 3 bytes (2 bits per trit).
 */
esp_err_t reflex_tryte9_pack(const reflex_tryte9_t *value,
                             uint8_t out[REFLEX_PACKED_TRYTE9_BYTES]);

/** @brief Unpack 3 bytes into a tryte9. */
esp_err_t reflex_tryte9_unpack(const uint8_t in[REFLEX_PACKED_TRYTE9_BYTES],
                               reflex_tryte9_t *out);

/**
 * @brief Pack a word18 into 5 bytes (2 bits per trit, with 4
 * trailing bits unused and zeroed).
 */
esp_err_t reflex_word18_pack(const reflex_word18_t *value,
                             uint8_t out[REFLEX_PACKED_WORD18_BYTES]);

/** @brief Unpack 5 bytes into a word18. */
esp_err_t reflex_word18_unpack(const uint8_t in[REFLEX_PACKED_WORD18_BYTES],
                               reflex_word18_t *out);

/**
 * @brief Boot-time self-check exercising the pack/unpack,
 * arithmetic, compare, and select helpers against known-good and
 * known-bad inputs. Like reflex_vm_self_check, the intentional
 * negative cases produce ERROR log entries by design.
 */
esp_err_t reflex_ternary_self_check(void);

#ifdef __cplusplus
}
#endif

#endif
