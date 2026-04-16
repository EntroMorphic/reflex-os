#include "reflex_ternary.h"

#include <string.h>

#include "reflex_types.h"

#define REFLEX_PACKED_CODE_NEG 0x0u
#define REFLEX_PACKED_CODE_ZERO 0x1u
#define REFLEX_PACKED_CODE_POS 0x2u
#define REFLEX_PACKED_CODE_INVALID 0x3u

static uint8_t reflex_trit_to_code(reflex_trit_t trit)
{
    switch (trit) {
    case REFLEX_TRIT_NEG:
        return REFLEX_PACKED_CODE_NEG;
    case REFLEX_TRIT_ZERO:
        return REFLEX_PACKED_CODE_ZERO;
    case REFLEX_TRIT_POS:
        return REFLEX_PACKED_CODE_POS;
    default:
        return REFLEX_PACKED_CODE_INVALID;
    }
}

static reflex_err_t reflex_code_to_trit(uint8_t code, reflex_trit_t *out)
{
    switch (code & REFLEX_PACKED_TRIT_MASK) {
    case REFLEX_PACKED_CODE_NEG:
        *out = REFLEX_TRIT_NEG;
        return REFLEX_OK;
    case REFLEX_PACKED_CODE_ZERO:
        *out = REFLEX_TRIT_ZERO;
        return REFLEX_OK;
    case REFLEX_PACKED_CODE_POS:
        *out = REFLEX_TRIT_POS;
        return REFLEX_OK;
    default:
        return REFLEX_ERR_INVALID_RESPONSE;
    }
}

static reflex_trit_t reflex_normalize_sum(int sum, int *carry)
{
    if (sum > REFLEX_TRIT_POS) {
        *carry = 1;
        return (reflex_trit_t)(sum - 3);
    }

    if (sum < REFLEX_TRIT_NEG) {
        *carry = -1;
        return (reflex_trit_t)(sum + 3);
    }

    *carry = 0;
    return (reflex_trit_t)sum;
}

static void reflex_word18_zero(reflex_word18_t *value)
{
    memset(value->trits, REFLEX_TRIT_ZERO, sizeof(value->trits));
}

static reflex_err_t reflex_validate_trits(const reflex_trit_t *trits, size_t trit_count)
{
    REFLEX_RETURN_ON_FALSE(trits != NULL, REFLEX_ERR_INVALID_ARG, "ternary", "trits are required");

    for (size_t i = 0; i < trit_count; ++i) {
        REFLEX_RETURN_ON_FALSE(reflex_trit_is_valid(trits[i]),
                            REFLEX_ERR_INVALID_ARG,
                            "ternary",
                            "invalid trit sequence");
    }

    return REFLEX_OK;
}

static reflex_err_t reflex_pack_trits(const reflex_trit_t *trits,
                                   size_t trit_count,
                                   uint8_t *out,
                                   size_t out_len)
{
    size_t byte_index = 0;

    REFLEX_RETURN_ON_FALSE(trits != NULL, REFLEX_ERR_INVALID_ARG, "ternary", "trits is required");
    REFLEX_RETURN_ON_FALSE(out != NULL, REFLEX_ERR_INVALID_ARG, "ternary", "output is required");

    memset(out, 0, out_len);

    for (size_t i = 0; i < trit_count; ++i) {
        size_t bit_index = i * REFLEX_PACKED_TRIT_BITS;
        uint8_t shift = bit_index % 8;
        uint8_t code = reflex_trit_to_code(trits[i]);

        REFLEX_RETURN_ON_FALSE(code != REFLEX_PACKED_CODE_INVALID,
                            REFLEX_ERR_INVALID_ARG,
                            "ternary",
                            "invalid trit value");

        byte_index = bit_index / 8;
        REFLEX_RETURN_ON_FALSE(byte_index < out_len, REFLEX_ERR_INVALID_SIZE, "ternary", "packed output too small");

        out[byte_index] |= (uint8_t)(code << shift);
        if (shift > 6) {
            REFLEX_RETURN_ON_FALSE(byte_index + 1 < out_len,
                                REFLEX_ERR_INVALID_SIZE,
                                "ternary",
                                "packed output overflow");
            out[byte_index + 1] |= (uint8_t)(code >> (8 - shift));
        }
    }

    return REFLEX_OK;
}

static reflex_err_t reflex_unpack_trits(const uint8_t *in,
                                     size_t in_len,
                                     size_t trit_count,
                                     reflex_trit_t *out)
{
    REFLEX_RETURN_ON_FALSE(in != NULL, REFLEX_ERR_INVALID_ARG, "ternary", "input is required");
    REFLEX_RETURN_ON_FALSE(out != NULL, REFLEX_ERR_INVALID_ARG, "ternary", "output is required");

    for (size_t i = 0; i < trit_count; ++i) {
        size_t bit_index = i * REFLEX_PACKED_TRIT_BITS;
        size_t byte_index = bit_index / 8;
        uint8_t shift = bit_index % 8;
        uint8_t code;

        REFLEX_RETURN_ON_FALSE(byte_index < in_len, REFLEX_ERR_INVALID_SIZE, "ternary", "packed input too small");

        code = (uint8_t)((in[byte_index] >> shift) & REFLEX_PACKED_TRIT_MASK);
        if (shift > 6) {
            REFLEX_RETURN_ON_FALSE(byte_index + 1 < in_len,
                                REFLEX_ERR_INVALID_SIZE,
                                "ternary",
                                "packed input overflow");
            code |= (uint8_t)((in[byte_index + 1] << (8 - shift)) & REFLEX_PACKED_TRIT_MASK);
        }

        REFLEX_RETURN_ON_ERROR(reflex_code_to_trit(code, &out[i]), "ternary", "invalid packed trit code");
    }

    return REFLEX_OK;
}

bool reflex_trit_is_valid(int value)
{
    return value >= REFLEX_TRIT_NEG && value <= REFLEX_TRIT_POS;
}

bool reflex_tryte9_is_valid(const reflex_tryte9_t *value)
{
    return value != NULL && reflex_validate_trits(value->trits, REFLEX_TRYTE9_TRITS) == REFLEX_OK;
}

bool reflex_word18_is_valid(const reflex_word18_t *value)
{
    return value != NULL && reflex_validate_trits(value->trits, REFLEX_WORD18_TRITS) == REFLEX_OK;
}

bool reflex_tryte9_equal(const reflex_tryte9_t *lhs, const reflex_tryte9_t *rhs)
{
    return lhs != NULL && rhs != NULL && memcmp(lhs->trits, rhs->trits, sizeof(lhs->trits)) == 0;
}

bool reflex_word18_equal(const reflex_word18_t *lhs, const reflex_word18_t *rhs)
{
    return lhs != NULL && rhs != NULL && memcmp(lhs->trits, rhs->trits, sizeof(lhs->trits)) == 0;
}

reflex_err_t reflex_word18_compare(const reflex_word18_t *lhs,
                                const reflex_word18_t *rhs,
                                reflex_trit_t *out)
{
    REFLEX_RETURN_ON_FALSE(lhs != NULL, REFLEX_ERR_INVALID_ARG, "ternary", "lhs is required");
    REFLEX_RETURN_ON_FALSE(rhs != NULL, REFLEX_ERR_INVALID_ARG, "ternary", "rhs is required");
    REFLEX_RETURN_ON_FALSE(out != NULL, REFLEX_ERR_INVALID_ARG, "ternary", "compare result is required");
    REFLEX_RETURN_ON_FALSE(reflex_word18_is_valid(lhs), REFLEX_ERR_INVALID_ARG, "ternary", "lhs is invalid");
    REFLEX_RETURN_ON_FALSE(reflex_word18_is_valid(rhs), REFLEX_ERR_INVALID_ARG, "ternary", "rhs is invalid");

    for (int i = REFLEX_WORD18_TRITS - 1; i >= 0; --i) {
        if (lhs->trits[i] < rhs->trits[i]) {
            *out = REFLEX_TRIT_NEG;
            return REFLEX_OK;
        }
        if (lhs->trits[i] > rhs->trits[i]) {
            *out = REFLEX_TRIT_POS;
            return REFLEX_OK;
        }
    }

    *out = REFLEX_TRIT_ZERO;
    return REFLEX_OK;
}

reflex_err_t reflex_word18_add(const reflex_word18_t *lhs,
                            const reflex_word18_t *rhs,
                            reflex_word18_t *out)
{
    int carry = 0;
    reflex_word18_t result;

    REFLEX_RETURN_ON_FALSE(lhs != NULL, REFLEX_ERR_INVALID_ARG, "ternary", "lhs is required");
    REFLEX_RETURN_ON_FALSE(rhs != NULL, REFLEX_ERR_INVALID_ARG, "ternary", "rhs is required");
    REFLEX_RETURN_ON_FALSE(out != NULL, REFLEX_ERR_INVALID_ARG, "ternary", "output is required");
    REFLEX_RETURN_ON_FALSE(reflex_word18_is_valid(lhs), REFLEX_ERR_INVALID_ARG, "ternary", "lhs is invalid");
    REFLEX_RETURN_ON_FALSE(reflex_word18_is_valid(rhs), REFLEX_ERR_INVALID_ARG, "ternary", "rhs is invalid");

    for (size_t i = 0; i < REFLEX_WORD18_TRITS; ++i) {
        int sum = lhs->trits[i] + rhs->trits[i] + carry;
        result.trits[i] = reflex_normalize_sum(sum, &carry);
    }

    REFLEX_RETURN_ON_FALSE(carry == 0, REFLEX_ERR_INVALID_STATE, "ternary", "word18 addition overflow");
    memcpy(out->trits, result.trits, sizeof(out->trits));
    return REFLEX_OK;
}

reflex_err_t reflex_word18_subtract(const reflex_word18_t *lhs,
                                 const reflex_word18_t *rhs,
                                 reflex_word18_t *out)
{
    reflex_word18_t negated_rhs;

    REFLEX_RETURN_ON_FALSE(lhs != NULL, REFLEX_ERR_INVALID_ARG, "ternary", "lhs is required");
    REFLEX_RETURN_ON_FALSE(rhs != NULL, REFLEX_ERR_INVALID_ARG, "ternary", "rhs is required");
    REFLEX_RETURN_ON_FALSE(out != NULL, REFLEX_ERR_INVALID_ARG, "ternary", "output is required");
    REFLEX_RETURN_ON_FALSE(reflex_word18_is_valid(lhs), REFLEX_ERR_INVALID_ARG, "ternary", "lhs is invalid");
    REFLEX_RETURN_ON_FALSE(reflex_word18_is_valid(rhs), REFLEX_ERR_INVALID_ARG, "ternary", "rhs is invalid");

    for (size_t i = 0; i < REFLEX_WORD18_TRITS; ++i) {
        negated_rhs.trits[i] = (reflex_trit_t)(-rhs->trits[i]);
    }

    return reflex_word18_add(lhs, &negated_rhs, out);
}

reflex_err_t reflex_word18_select(reflex_trit_t selector,
                               const reflex_word18_t *negative,
                               const reflex_word18_t *zero,
                               const reflex_word18_t *positive,
                               reflex_word18_t *out)
{
    const reflex_word18_t *selected;

    REFLEX_RETURN_ON_FALSE(reflex_trit_is_valid(selector), REFLEX_ERR_INVALID_ARG, "ternary", "selector is invalid");
    REFLEX_RETURN_ON_FALSE(negative != NULL, REFLEX_ERR_INVALID_ARG, "ternary", "negative branch is required");
    REFLEX_RETURN_ON_FALSE(zero != NULL, REFLEX_ERR_INVALID_ARG, "ternary", "zero branch is required");
    REFLEX_RETURN_ON_FALSE(positive != NULL, REFLEX_ERR_INVALID_ARG, "ternary", "positive branch is required");
    REFLEX_RETURN_ON_FALSE(out != NULL, REFLEX_ERR_INVALID_ARG, "ternary", "output is required");
    REFLEX_RETURN_ON_FALSE(reflex_word18_is_valid(negative), REFLEX_ERR_INVALID_ARG, "ternary", "negative branch is invalid");
    REFLEX_RETURN_ON_FALSE(reflex_word18_is_valid(zero), REFLEX_ERR_INVALID_ARG, "ternary", "zero branch is invalid");
    REFLEX_RETURN_ON_FALSE(reflex_word18_is_valid(positive), REFLEX_ERR_INVALID_ARG, "ternary", "positive branch is invalid");

    if (selector == REFLEX_TRIT_NEG) {
        selected = negative;
    } else if (selector == REFLEX_TRIT_ZERO) {
        selected = zero;
    } else {
        selected = positive;
    }

    memcpy(out->trits, selected->trits, sizeof(out->trits));
    return REFLEX_OK;
}

reflex_err_t reflex_word18_from_int32(int32_t value, reflex_word18_t *out)
{
    int32_t remaining = value;

    REFLEX_RETURN_ON_FALSE(out != NULL, REFLEX_ERR_INVALID_ARG, "ternary", "output is required");

    reflex_word18_zero(out);

    for (size_t i = 0; i < REFLEX_WORD18_TRITS; ++i) {
        int32_t remainder = remaining % 3;

        remaining /= 3;
        if (remainder == 2) {
            remainder = -1;
            remaining += 1;
        } else if (remainder == -2) {
            remainder = 1;
            remaining -= 1;
        }

        out->trits[i] = (reflex_trit_t)remainder;
    }

    REFLEX_RETURN_ON_FALSE(remaining == 0, REFLEX_ERR_INVALID_SIZE, "ternary", "value does not fit in word18");
    return REFLEX_OK;
}

reflex_err_t reflex_word18_to_int32(const reflex_word18_t *value, int32_t *out)
{
    int32_t result = 0;
    int32_t place = 1;

    REFLEX_RETURN_ON_FALSE(value != NULL, REFLEX_ERR_INVALID_ARG, "ternary", "value is required");
    REFLEX_RETURN_ON_FALSE(out != NULL, REFLEX_ERR_INVALID_ARG, "ternary", "output is required");
    REFLEX_RETURN_ON_FALSE(reflex_word18_is_valid(value), REFLEX_ERR_INVALID_ARG, "ternary", "word is invalid");

    for (size_t i = 0; i < REFLEX_WORD18_TRITS; ++i) {
        result += value->trits[i] * place;
        place *= 3;
    }

    *out = result;
    return REFLEX_OK;
}

reflex_err_t reflex_tryte9_pack(const reflex_tryte9_t *value,
                             uint8_t out[REFLEX_PACKED_TRYTE9_BYTES])
{
    REFLEX_RETURN_ON_FALSE(value != NULL, REFLEX_ERR_INVALID_ARG, "ternary", "value is required");
    return reflex_pack_trits(value->trits, REFLEX_TRYTE9_TRITS, out, REFLEX_PACKED_TRYTE9_BYTES);
}

reflex_err_t reflex_tryte9_unpack(const uint8_t in[REFLEX_PACKED_TRYTE9_BYTES],
                               reflex_tryte9_t *out)
{
    REFLEX_RETURN_ON_FALSE(out != NULL, REFLEX_ERR_INVALID_ARG, "ternary", "output is required");
    return reflex_unpack_trits(in, REFLEX_PACKED_TRYTE9_BYTES, REFLEX_TRYTE9_TRITS, out->trits);
}

reflex_err_t reflex_word18_pack(const reflex_word18_t *value,
                             uint8_t out[REFLEX_PACKED_WORD18_BYTES])
{
    REFLEX_RETURN_ON_FALSE(value != NULL, REFLEX_ERR_INVALID_ARG, "ternary", "value is required");
    return reflex_pack_trits(value->trits, REFLEX_WORD18_TRITS, out, REFLEX_PACKED_WORD18_BYTES);
}

reflex_err_t reflex_word18_unpack(const uint8_t in[REFLEX_PACKED_WORD18_BYTES],
                               reflex_word18_t *out)
{
    REFLEX_RETURN_ON_FALSE(out != NULL, REFLEX_ERR_INVALID_ARG, "ternary", "output is required");
    return reflex_unpack_trits(in, REFLEX_PACKED_WORD18_BYTES, REFLEX_WORD18_TRITS, out->trits);
}

reflex_err_t reflex_ternary_self_check(void)
{
    reflex_trit_t compare_result;
    static const reflex_tryte9_t sample_tryte = {
        .trits = {
            REFLEX_TRIT_NEG, REFLEX_TRIT_ZERO, REFLEX_TRIT_POS,
            REFLEX_TRIT_POS, REFLEX_TRIT_ZERO, REFLEX_TRIT_NEG,
            REFLEX_TRIT_ZERO, REFLEX_TRIT_POS, REFLEX_TRIT_NEG,
        },
    };
    static const reflex_word18_t sample_word = {
        .trits = {
            REFLEX_TRIT_NEG, REFLEX_TRIT_ZERO, REFLEX_TRIT_POS,
            REFLEX_TRIT_POS, REFLEX_TRIT_ZERO, REFLEX_TRIT_NEG,
            REFLEX_TRIT_ZERO, REFLEX_TRIT_POS, REFLEX_TRIT_NEG,
            REFLEX_TRIT_POS, REFLEX_TRIT_NEG, REFLEX_TRIT_ZERO,
            REFLEX_TRIT_NEG, REFLEX_TRIT_POS, REFLEX_TRIT_ZERO,
            REFLEX_TRIT_ZERO, REFLEX_TRIT_NEG, REFLEX_TRIT_POS,
        },
    };
    uint8_t packed_tryte[REFLEX_PACKED_TRYTE9_BYTES];
    uint8_t packed_word[REFLEX_PACKED_WORD18_BYTES];
    reflex_tryte9_t unpacked_tryte;
    reflex_word18_t unpacked_word;
    reflex_word18_t zero_word;
    reflex_word18_t sum_word;
    reflex_word18_t diff_word;
    reflex_word18_t selected_word;
    reflex_word18_t invalid_word = {0};
    reflex_word18_t overflow_word = {0};
    uint8_t invalid_tryte[REFLEX_PACKED_TRYTE9_BYTES] = {0};

    reflex_word18_zero(&zero_word);

    REFLEX_RETURN_ON_ERROR(reflex_tryte9_pack(&sample_tryte, packed_tryte), "ternary", "tryte pack failed");
    REFLEX_RETURN_ON_ERROR(reflex_tryte9_unpack(packed_tryte, &unpacked_tryte), "ternary", "tryte unpack failed");
    REFLEX_RETURN_ON_FALSE(reflex_tryte9_equal(&sample_tryte, &unpacked_tryte),
                        REFLEX_FAIL,
                        "ternary",
                        "tryte round-trip mismatch");

    REFLEX_RETURN_ON_ERROR(reflex_word18_pack(&sample_word, packed_word), "ternary", "word pack failed");
    REFLEX_RETURN_ON_ERROR(reflex_word18_unpack(packed_word, &unpacked_word), "ternary", "word unpack failed");
    REFLEX_RETURN_ON_FALSE(reflex_word18_equal(&sample_word, &unpacked_word),
                        REFLEX_FAIL,
                        "ternary",
                        "word round-trip mismatch");

    invalid_tryte[0] = REFLEX_PACKED_CODE_INVALID;
    REFLEX_RETURN_ON_FALSE(reflex_tryte9_unpack(invalid_tryte, &unpacked_tryte) == REFLEX_ERR_INVALID_RESPONSE,
                        REFLEX_FAIL,
                        "ternary",
                        "invalid packed trit must be rejected");

    invalid_word.trits[0] = (reflex_trit_t)2;
    REFLEX_RETURN_ON_FALSE(!reflex_word18_is_valid(&invalid_word),
                        REFLEX_FAIL,
                        "ternary",
                        "invalid word must fail validation");

    REFLEX_RETURN_ON_ERROR(reflex_word18_compare(&sample_word, &sample_word, &compare_result),
                        "ternary",
                        "word compare failed");
    REFLEX_RETURN_ON_FALSE(compare_result == REFLEX_TRIT_ZERO,
                        REFLEX_FAIL,
                        "ternary",
                        "word compare must report equality");
    REFLEX_RETURN_ON_ERROR(reflex_word18_compare(&zero_word, &sample_word, &compare_result),
                        "ternary",
                        "word compare ordering failed");
    REFLEX_RETURN_ON_FALSE(compare_result == REFLEX_TRIT_NEG,
                        REFLEX_FAIL,
                        "ternary",
                        "zero word must compare below sample word");
    REFLEX_RETURN_ON_FALSE(reflex_word18_compare(NULL, &sample_word, &compare_result) == REFLEX_ERR_INVALID_ARG,
                        REFLEX_FAIL,
                        "ternary",
                        "compare must reject null inputs");

    REFLEX_RETURN_ON_ERROR(reflex_word18_add(&sample_word, &zero_word, &sum_word),
                        "ternary",
                        "word add failed");
    REFLEX_RETURN_ON_FALSE(reflex_word18_equal(&sample_word, &sum_word),
                        REFLEX_FAIL,
                        "ternary",
                        "adding zero must preserve the word");

    REFLEX_RETURN_ON_ERROR(reflex_word18_subtract(&sample_word, &sample_word, &diff_word),
                        "ternary",
                        "word subtract failed");
    REFLEX_RETURN_ON_FALSE(reflex_word18_equal(&zero_word, &diff_word),
                        REFLEX_FAIL,
                        "ternary",
                        "subtracting a word from itself must yield zero");

    for (size_t i = 0; i < REFLEX_WORD18_TRITS; ++i) {
        overflow_word.trits[i] = REFLEX_TRIT_POS;
    }
    REFLEX_RETURN_ON_FALSE(reflex_word18_add(&overflow_word, &overflow_word, &sum_word) == REFLEX_ERR_INVALID_STATE,
                        REFLEX_FAIL,
                        "ternary",
                        "overflowing add must fail explicitly");
    REFLEX_RETURN_ON_FALSE(reflex_word18_add(&sample_word, &invalid_word, &sum_word) == REFLEX_ERR_INVALID_ARG,
                        REFLEX_FAIL,
                        "ternary",
                        "add must reject invalid operands");

    REFLEX_RETURN_ON_ERROR(reflex_word18_select(REFLEX_TRIT_POS,
                                             &zero_word,
                                             &diff_word,
                                             &sample_word,
                                             &selected_word),
                        "ternary",
                        "word select failed");
    REFLEX_RETURN_ON_FALSE(reflex_word18_equal(&sample_word, &selected_word),
                        REFLEX_FAIL,
                        "ternary",
                        "positive select must choose the positive branch");
    REFLEX_RETURN_ON_FALSE(reflex_word18_select(REFLEX_TRIT_POS,
                                             &invalid_word,
                                             &zero_word,
                                             &sample_word,
                                             &selected_word) == REFLEX_ERR_INVALID_ARG,
                        REFLEX_FAIL,
                        "ternary",
                        "select must reject invalid branches");

    return REFLEX_OK;
}
