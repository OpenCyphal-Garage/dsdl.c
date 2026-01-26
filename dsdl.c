/// Cyphal DSDL Parser in C
///
/// This is a compact C99+ implementation of a Cyphal DSDL parser that allows
/// loading DSDL definitions at runtime without compile-time code generation.
///
/// See AGENTS.md and docs/IMPLEMENTATION_PLAN.md for design details.
///
/// Copyright (c) OpenCyphal Development Team

#include "dsdl.h"

#include <assert.h>
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>
#include <math.h>
#include <inttypes.h>
#include <float.h>

#if DSDL_CONFIG_TRACE
#define DSDL_TRACE(self, ...) dsdl_trace(self, __FILE__, __LINE__, __func__, __VA_ARGS__)
#else
#define DSDL_TRACE(self, ...) (void)self
#endif

// ============================================================================
// Internal type definitions
// ============================================================================

/// Forward declaration for bit length sets (used in deferred evaluation).
typedef struct dsdl_bls_t dsdl_bls_t;

#define DSDL_VALUE_FLAG_OWNED 0x01U

/// Evaluation context for deferred expressions.
typedef struct
{
    dsdl_t*             dsdl;
    dsdl_bls_t*         offset;
    bool                offset_is_defined;
    wkv_str_t           current_namespace;
    const wkv_str_t*    constant_names;
    const dsdl_value_t* constant_values;
    size_t              constant_count;
} dsdl_eval_context_t;

/// If the value is deferred, invokes it until it obtains a concrete value.
/// False if any evaluation fails.
static bool dsdl_resolve_value(dsdl_value_t* value)
{
    assert(value != NULL);
    while (value->kind == dsdl_value_deferred) {
        dsdl_value_t   out     = { 0 };
        dsdl_closure_t closure = value->as.deferred;
        const bool     ok      = closure.fun(&closure, &out);
        if (!ok) {
            return false;
        }
        if (closure.cleanup != NULL) {
            closure.cleanup(&closure);
        }
        *value = out;
    }
    if (value->kind == dsdl_value_set) {
        for (size_t i = 0; i < value->as.set.count; i++) {
            if (!dsdl_resolve_value(&value->as.set.elements[i])) {
                return false;
            }
        }
    }
    return true;
}

// ============================================================================
// Rational arithmetic
// ============================================================================

static void dsdl_bigint_zero(dsdl_bigint_t* const out)
{
    if (out == NULL) {
        return;
    }
    (void)memset(out->limbs, 0, sizeof(out->limbs));
    out->limb_count = 0U;
    out->negative   = false;
}

static bool dsdl_bigint_is_zero(const dsdl_bigint_t* const value)
{
    return (value == NULL) || (value->limb_count == 0U);
}

static bool dsdl_bigint_is_one(const dsdl_bigint_t* const value)
{
    return (value != NULL) && (!value->negative) && (value->limb_count == 1U) && (value->limbs[0] == 1U);
}

static bool      dsdl_bigint_to_uintmax(const dsdl_bigint_t* const value, uintmax_t* const out);
static uintmax_t dsdl_gcd_uintmax(uintmax_t a, uintmax_t b);

static void dsdl_bigint_trim(dsdl_bigint_t* const value)
{
    while ((value->limb_count > 0U) && (value->limbs[value->limb_count - 1U] == 0U)) {
        value->limb_count--;
    }
    if (value->limb_count == 0U) {
        value->negative = false;
    }
}

static bool dsdl_bigint_from_uintmax(dsdl_bigint_t* const out, uintmax_t value)
{
    dsdl_bigint_zero(out);
    if (value == 0U) {
        return true;
    }
    while (value > 0U) {
        if (out->limb_count >= DSDL_BIGINT_LIMB_COUNT) {
            dsdl_bigint_zero(out);
            return false;
        }
        out->limbs[out->limb_count++] = (uint32_t)(value % (uintmax_t)DSDL_BIGINT_BASE);
        value /= (uintmax_t)DSDL_BIGINT_BASE;
    }
    return true;
}

static bool dsdl_bigint_from_intmax(dsdl_bigint_t* const out, const intmax_t value)
{
    const uintmax_t abs_value = (value < 0) ? (uintmax_t)(-(value + 1)) + 1U : (uintmax_t)value;
    if (!dsdl_bigint_from_uintmax(out, abs_value)) {
        return false;
    }
    out->negative = (value < 0);
    return true;
}

static int dsdl_bigint_cmp_abs(const dsdl_bigint_t* const a, const dsdl_bigint_t* const b)
{
    if (a->limb_count < b->limb_count) {
        return -1;
    }
    if (a->limb_count > b->limb_count) {
        return 1;
    }
    for (uint_least8_t i = a->limb_count; i-- > 0U;) {
        if (a->limbs[i] < b->limbs[i]) {
            return -1;
        }
        if (a->limbs[i] > b->limbs[i]) {
            return 1;
        }
    }
    return 0;
}

static int dsdl_bigint_cmp(const dsdl_bigint_t* const a, const dsdl_bigint_t* const b)
{
    if (a->negative != b->negative) {
        if (dsdl_bigint_is_zero(a) && dsdl_bigint_is_zero(b)) {
            return 0;
        }
        return a->negative ? -1 : 1;
    }
    const int cmp = dsdl_bigint_cmp_abs(a, b);
    return a->negative ? -cmp : cmp;
}

static bool dsdl_bigint_add_abs(const dsdl_bigint_t* const a, const dsdl_bigint_t* const b, dsdl_bigint_t* const out)
{
    dsdl_bigint_zero(out);
    uint64_t            carry     = 0U;
    const uint_least8_t max_count = (a->limb_count > b->limb_count) ? a->limb_count : b->limb_count;
    if (max_count > DSDL_BIGINT_LIMB_COUNT) {
        return false;
    }
    for (uint_least8_t i = 0U; i < max_count; i++) {
        uint64_t sum = carry;
        if (i < a->limb_count) {
            sum += a->limbs[i];
        }
        if (i < b->limb_count) {
            sum += b->limbs[i];
        }
        out->limbs[i] = (uint32_t)(sum % DSDL_BIGINT_BASE);
        carry         = sum / DSDL_BIGINT_BASE;
    }
    uint_least8_t count = max_count;
    if (carry > 0U) {
        if (count >= DSDL_BIGINT_LIMB_COUNT) {
            return false;
        }
        out->limbs[count++] = (uint32_t)carry;
    }
    out->limb_count = count;
    out->negative   = false;
    return true;
}

static bool dsdl_bigint_sub_abs(const dsdl_bigint_t* const a, const dsdl_bigint_t* const b, dsdl_bigint_t* const out)
{
    dsdl_bigint_zero(out);
    if (dsdl_bigint_cmp_abs(a, b) < 0) {
        return false;
    }
    int64_t borrow = 0;
    for (uint_least8_t i = 0U; i < a->limb_count; i++) {
        int64_t diff = (int64_t)a->limbs[i] - borrow;
        if (i < b->limb_count) {
            diff -= (int64_t)b->limbs[i];
        }
        if (diff < 0) {
            diff += (int64_t)DSDL_BIGINT_BASE;
            borrow = 1;
        } else {
            borrow = 0;
        }
        out->limbs[i] = (uint32_t)diff;
    }
    out->limb_count = a->limb_count;
    out->negative   = false;
    dsdl_bigint_trim(out);
    return true;
}

static bool dsdl_bigint_sub_abs_inplace(dsdl_bigint_t* const a, const dsdl_bigint_t* const b)
{
    dsdl_bigint_t tmp;
    if (!dsdl_bigint_sub_abs(a, b, &tmp)) {
        return false;
    }
    *a = tmp;
    return true;
}

static bool dsdl_bigint_add_signed(const dsdl_bigint_t* const a, const dsdl_bigint_t* const b, dsdl_bigint_t* const out)
{
    if (a->negative == b->negative) {
        if (!dsdl_bigint_add_abs(a, b, out)) {
            return false;
        }
        out->negative = a->negative;
        return true;
    }
    const int cmp = dsdl_bigint_cmp_abs(a, b);
    if (cmp == 0) {
        dsdl_bigint_zero(out);
        return true;
    }
    if (cmp > 0) {
        if (!dsdl_bigint_sub_abs(a, b, out)) {
            return false;
        }
        out->negative = a->negative;
        return true;
    }
    if (!dsdl_bigint_sub_abs(b, a, out)) {
        return false;
    }
    out->negative = b->negative;
    return true;
}

static bool dsdl_bigint_mul_abs(const dsdl_bigint_t* const a, const dsdl_bigint_t* const b, dsdl_bigint_t* const out)
{
    dsdl_bigint_zero(out);
    if (dsdl_bigint_is_zero(a) || dsdl_bigint_is_zero(b)) {
        return true;
    }
    for (uint_least8_t i = 0U; i < a->limb_count; i++) {
        uint64_t carry = 0U;
        for (uint_least8_t j = 0U; j < b->limb_count; j++) {
            const uint_least8_t idx = (uint_least8_t)(i + j);
            if (idx >= DSDL_BIGINT_LIMB_COUNT) {
                return false;
            }
            uint64_t cur = out->limbs[idx];
            cur += (uint64_t)a->limbs[i] * (uint64_t)b->limbs[j];
            cur += carry;
            out->limbs[idx] = (uint32_t)(cur % DSDL_BIGINT_BASE);
            carry           = cur / DSDL_BIGINT_BASE;
        }
        uint_least8_t idx = (uint_least8_t)(i + b->limb_count);
        while (carry > 0U) {
            if (idx >= DSDL_BIGINT_LIMB_COUNT) {
                return false;
            }
            uint64_t cur    = out->limbs[idx] + carry;
            out->limbs[idx] = (uint32_t)(cur % DSDL_BIGINT_BASE);
            carry           = cur / DSDL_BIGINT_BASE;
            idx++;
        }
    }
    out->limb_count = 0U;
    for (uint_least8_t i = DSDL_BIGINT_LIMB_COUNT; i-- > 0U;) {
        if (out->limbs[i] != 0U) {
            out->limb_count = (uint_least8_t)(i + 1U);
            break;
        }
    }
    out->negative = false;
    return true;
}

static bool dsdl_bigint_mul_small_inplace(dsdl_bigint_t* const value, const uint32_t mul)
{
    if (dsdl_bigint_is_zero(value)) {
        return true;
    }
    if (mul == 0U) {
        dsdl_bigint_zero(value);
        return true;
    }
    uint64_t carry = 0U;
    for (uint_least8_t i = 0U; i < value->limb_count; i++) {
        uint64_t cur    = (uint64_t)value->limbs[i] * (uint64_t)mul + carry;
        value->limbs[i] = (uint32_t)(cur % DSDL_BIGINT_BASE);
        carry           = cur / DSDL_BIGINT_BASE;
    }
    while (carry > 0U) {
        if (value->limb_count >= DSDL_BIGINT_LIMB_COUNT) {
            return false;
        }
        value->limbs[value->limb_count++] = (uint32_t)(carry % DSDL_BIGINT_BASE);
        carry /= DSDL_BIGINT_BASE;
    }
    return true;
}

static bool dsdl_bigint_add_small_inplace(dsdl_bigint_t* const value, const uint32_t add)
{
    uint64_t      carry = add;
    uint_least8_t i     = 0U;
    while (carry > 0U) {
        if (i >= DSDL_BIGINT_LIMB_COUNT) {
            return false;
        }
        if (i >= value->limb_count) {
            value->limbs[i]   = 0U;
            value->limb_count = (uint_least8_t)(i + 1U);
        }
        uint64_t cur    = (uint64_t)value->limbs[i] + carry;
        value->limbs[i] = (uint32_t)(cur % DSDL_BIGINT_BASE);
        carry           = cur / DSDL_BIGINT_BASE;
        i++;
    }
    return true;
}

static bool dsdl_bigint_mul_add_small_inplace(dsdl_bigint_t* const value, const uint32_t mul, const uint32_t add)
{
    if (!dsdl_bigint_mul_small_inplace(value, mul)) {
        return false;
    }
    return dsdl_bigint_add_small_inplace(value, add);
}

static bool dsdl_bigint_div_small_inplace(dsdl_bigint_t* const value, const uint32_t div, uint32_t* const rem)
{
    if (div == 0U) {
        return false;
    }
    if (dsdl_bigint_is_zero(value)) {
        if (rem != NULL) {
            *rem = 0U;
        }
        return true;
    }
    uint64_t r = 0U;
    for (uint_least8_t i = value->limb_count; i-- > 0U;) {
        const uint64_t cur = value->limbs[i] + (r * (uint64_t)DSDL_BIGINT_BASE);
        value->limbs[i]    = (uint32_t)(cur / div);
        r                  = cur % div;
    }
    dsdl_bigint_trim(value);
    if (rem != NULL) {
        *rem = (uint32_t)r;
    }
    return true;
}

static bool dsdl_bigint_shift_base_add(dsdl_bigint_t* const value, const uint32_t digit)
{
    if (dsdl_bigint_is_zero(value)) {
        if (digit == 0U) {
            return true;
        }
        value->limbs[0]   = digit;
        value->limb_count = 1U;
        value->negative   = false;
        return true;
    }
    if (value->limb_count >= DSDL_BIGINT_LIMB_COUNT) {
        return false;
    }
    for (uint_least8_t i = value->limb_count; i > 0U; i--) {
        value->limbs[i] = value->limbs[i - 1U];
    }
    value->limbs[0] = digit;
    value->limb_count++;
    return true;
}

static bool dsdl_bigint_mul_small(const dsdl_bigint_t* const value, const uint32_t mul, dsdl_bigint_t* const out)
{
    *out          = *value;
    out->negative = false;
    return dsdl_bigint_mul_small_inplace(out, mul);
}

static uint32_t dsdl_bigint_estimate_quotient(const dsdl_bigint_t* const rem, const dsdl_bigint_t* const den)
{
    if ((rem->limb_count == 0U) || (den->limb_count == 0U)) {
        return 0U;
    }
    const uint64_t rem_hi   = rem->limbs[rem->limb_count - 1U];
    const uint64_t rem_next = (rem->limb_count > 1U) ? rem->limbs[rem->limb_count - 2U] : 0U;
    const uint64_t den_hi   = den->limbs[den->limb_count - 1U];
    if (den_hi == 0U) {
        return 0U;
    }
    uint64_t num = rem_hi * (uint64_t)DSDL_BIGINT_BASE + rem_next;
    uint64_t q   = num / den_hi;
    if (q >= DSDL_BIGINT_BASE) {
        q = DSDL_BIGINT_BASE - 1U;
    }
    return (uint32_t)q;
}

static bool dsdl_bigint_div_mod_abs(const dsdl_bigint_t* const num,
                                    const dsdl_bigint_t* const den,
                                    dsdl_bigint_t* const       quot,
                                    dsdl_bigint_t* const       rem)
{
    if ((num == NULL) || (den == NULL) || (quot == NULL) || (rem == NULL)) {
        return false;
    }
    if (dsdl_bigint_is_zero(den)) {
        return false;
    }
    if (dsdl_bigint_is_zero(num)) {
        dsdl_bigint_zero(quot);
        dsdl_bigint_zero(rem);
        return true;
    }
    if (dsdl_bigint_cmp_abs(num, den) < 0) {
        dsdl_bigint_zero(quot);
        *rem          = *num;
        rem->negative = false;
        return true;
    }
    uintmax_t num_u = 0U;
    uintmax_t den_u = 0U;
    if (dsdl_bigint_to_uintmax(num, &num_u) && dsdl_bigint_to_uintmax(den, &den_u)) {
        if (den_u == 0U) {
            return false;
        }
        const uintmax_t q = num_u / den_u;
        const uintmax_t r = num_u % den_u;
        if (!dsdl_bigint_from_uintmax(quot, q) || !dsdl_bigint_from_uintmax(rem, r)) {
            return false;
        }
        return true;
    }
    if (den->limb_count == 1U) {
        dsdl_bigint_t tmp  = *num;
        tmp.negative       = false;
        uint32_t rem_small = 0U;
        if (!dsdl_bigint_div_small_inplace(&tmp, den->limbs[0], &rem_small)) {
            return false;
        }
        *quot          = tmp;
        quot->negative = false;
        dsdl_bigint_zero(rem);
        if (rem_small > 0U) {
            rem->limbs[0]   = rem_small;
            rem->limb_count = 1U;
        }
        rem->negative = false;
        return true;
    }

    dsdl_bigint_t        num_scaled = *num;
    dsdl_bigint_t        den_scaled = *den;
    const dsdl_bigint_t* num_div    = num;
    const dsdl_bigint_t* den_div    = den;
    uint32_t             norm       = 1U;
    if (den->limb_count > 1U) {
        const uint32_t den_hi = den->limbs[den->limb_count - 1U];
        if (den_hi < (DSDL_BIGINT_BASE / 2U)) {
            norm       = (uint32_t)(DSDL_BIGINT_BASE / (den_hi + 1U));
            num_scaled = *num;
            den_scaled = *den;
            if (!dsdl_bigint_mul_small_inplace(&num_scaled, norm) ||
                !dsdl_bigint_mul_small_inplace(&den_scaled, norm)) {
                return false;
            }
            num_div = &num_scaled;
            den_div = &den_scaled;
        }
    }

    dsdl_bigint_zero(quot);
    dsdl_bigint_zero(rem);
    quot->limb_count = num_div->limb_count;

    for (uint_least8_t i = num_div->limb_count; i-- > 0U;) {
        if (!dsdl_bigint_shift_base_add(rem, num_div->limbs[i])) {
            return false;
        }
        uint32_t qdigit = 0U;
        if (!dsdl_bigint_is_zero(rem)) {
            qdigit = dsdl_bigint_estimate_quotient(rem, den_div);
            if (qdigit > 0U) {
                dsdl_bigint_t tmp;
                if (!dsdl_bigint_mul_small(den_div, qdigit, &tmp)) {
                    return false;
                }
                while (dsdl_bigint_cmp_abs(&tmp, rem) > 0) {
                    if (qdigit == 0U) {
                        break;
                    }
                    qdigit--;
                    if (!dsdl_bigint_sub_abs_inplace(&tmp, den_div)) {
                        return false;
                    }
                }
                if (!dsdl_bigint_sub_abs_inplace(rem, &tmp)) {
                    return false;
                }
            }
        }
        quot->limbs[i] = qdigit;
    }

    dsdl_bigint_trim(quot);
    dsdl_bigint_trim(rem);
    if (norm > 1U) {
        uint32_t rem_small = 0U;
        if (!dsdl_bigint_div_small_inplace(rem, norm, &rem_small) || (rem_small != 0U)) {
            return false;
        }
    }
    rem->negative = false;
    return true;
}

static bool dsdl_bigint_div_exact(dsdl_bigint_t* const value, const dsdl_bigint_t* const divisor)
{
    dsdl_bigint_t quot;
    dsdl_bigint_t rem;
    if (!dsdl_bigint_div_mod_abs(value, divisor, &quot, &rem)) {
        return false;
    }
    if (!dsdl_bigint_is_zero(&rem)) {
        return false;
    }
    quot.negative = value->negative;
    *value        = quot;
    return true;
}

static bool dsdl_bigint_gcd(dsdl_bigint_t a, dsdl_bigint_t b, dsdl_bigint_t* const out)
{
    a.negative = false;
    b.negative = false;
    if (dsdl_bigint_is_zero(&a)) {
        *out = b;
        return true;
    }
    if (dsdl_bigint_is_zero(&b)) {
        *out = a;
        return true;
    }
    uintmax_t au = 0U;
    uintmax_t bu = 0U;
    if (dsdl_bigint_to_uintmax(&a, &au) && dsdl_bigint_to_uintmax(&b, &bu)) {
        const uintmax_t g = dsdl_gcd_uintmax(au, bu);
        return dsdl_bigint_from_uintmax(out, g);
    }
    while (!dsdl_bigint_is_zero(&b)) {
        dsdl_bigint_t q;
        dsdl_bigint_t r;
        if (!dsdl_bigint_div_mod_abs(&a, &b, &q, &r)) {
            return false;
        }
        a = b;
        b = r;
    }
    *out = a;
    return true;
}

static bool dsdl_bigint_to_uintmax(const dsdl_bigint_t* const value, uintmax_t* const out)
{
    if ((value == NULL) || (out == NULL) || value->negative) {
        return false;
    }
    uintmax_t result = 0U;
    for (uint_least8_t i = value->limb_count; i-- > 0U;) {
        if (result > (UINTMAX_MAX - value->limbs[i]) / (uintmax_t)DSDL_BIGINT_BASE) {
            return false;
        }
        result = result * (uintmax_t)DSDL_BIGINT_BASE + value->limbs[i];
    }
    *out = result;
    return true;
}

static bool dsdl_bigint_to_intmax(const dsdl_bigint_t* const value, intmax_t* const out)
{
    if ((value == NULL) || (out == NULL)) {
        return false;
    }
    dsdl_bigint_t abs_value = *value;
    abs_value.negative      = false;
    uintmax_t abs_u         = 0U;
    if (!dsdl_bigint_to_uintmax(&abs_value, &abs_u)) {
        return false;
    }
    if (value->negative) {
        if (abs_u > ((uintmax_t)INTMAX_MAX + 1U)) {
            return false;
        }
        if (abs_u == ((uintmax_t)INTMAX_MAX + 1U)) {
            *out = INTMAX_MIN;
        } else {
            *out = -(intmax_t)abs_u;
        }
        return true;
    }
    if (abs_u > (uintmax_t)INTMAX_MAX) {
        return false;
    }
    *out = (intmax_t)abs_u;
    return true;
}

static bool dsdl_bigint_mul_pow2_inplace(dsdl_bigint_t* const value, const uint32_t exp)
{
    for (uint32_t i = 0U; i < exp; i++) {
        if (!dsdl_bigint_mul_small_inplace(value, 2U)) {
            return false;
        }
    }
    return true;
}

static bool dsdl_bigint_mul_pow10_inplace(dsdl_bigint_t* const value, const uint32_t exp)
{
    for (uint32_t i = 0U; i < exp; i++) {
        if (!dsdl_bigint_mul_small_inplace(value, 10U)) {
            return false;
        }
    }
    return true;
}

static bool dsdl_bigint_pow10(const uint32_t exp, dsdl_bigint_t* const out)
{
    if (!dsdl_bigint_from_uintmax(out, 1U)) {
        return false;
    }
    return dsdl_bigint_mul_pow10_inplace(out, exp);
}

static bool dsdl_bigint_to_double(const dsdl_bigint_t* const value, double* const out)
{
    if ((value == NULL) || (out == NULL)) {
        return false;
    }
    double result = 0.0;
    for (uint_least8_t i = value->limb_count; i-- > 0U;) {
        result = (result * (double)DSDL_BIGINT_BASE) + (double)value->limbs[i];
    }
    if (value->negative) {
        result = -result;
    }
    *out = result;
    return isfinite(result);
}

static dsdl_rational_t dsdl_rational_nan(void)
{
    dsdl_rational_t r;
    dsdl_bigint_zero(&r.num);
    dsdl_bigint_zero(&r.den);
    return r;
}

static bool dsdl_rational_is_nan(const dsdl_rational_t r) { return dsdl_bigint_is_zero(&r.den); }

/// Create a rational from an integer.
static dsdl_rational_t dsdl_rational_from_int(const intmax_t value)
{
    dsdl_rational_t r;
    if (!dsdl_bigint_from_intmax(&r.num, value) || !dsdl_bigint_from_uintmax(&r.den, 1U)) {
        return dsdl_rational_nan();
    }
    return r;
}

static dsdl_rational_t dsdl_rational_from_uintmax(const uintmax_t value)
{
    dsdl_rational_t r;
    if (!dsdl_bigint_from_uintmax(&r.num, value) || !dsdl_bigint_from_uintmax(&r.den, 1U)) {
        return dsdl_rational_nan();
    }
    r.num.negative = false;
    return r;
}

/// Normalize a rational: ensure den > 0 and GCD(|num|, den) == 1.
static dsdl_rational_t dsdl_rational_normalize(dsdl_rational_t r)
{
    if (dsdl_rational_is_nan(r)) {
        return r;
    }
    if (dsdl_bigint_is_zero(&r.num)) {
        (void)dsdl_bigint_from_uintmax(&r.den, 1U);
        r.num.negative = false;
        return r;
    }
    const bool    negative = r.num.negative;
    dsdl_bigint_t abs_num  = r.num;
    abs_num.negative       = false;

    dsdl_bigint_t g;
    if (!dsdl_bigint_gcd(abs_num, r.den, &g)) {
        return dsdl_rational_nan();
    }
    if (!dsdl_bigint_is_one(&g)) {
        if (!dsdl_bigint_div_exact(&abs_num, &g) || !dsdl_bigint_div_exact(&r.den, &g)) {
            return dsdl_rational_nan();
        }
    }
    abs_num.negative = negative;
    r.num            = abs_num;
    return r;
}

/// Check if rational is an integer (denominator == 1).
static bool dsdl_rational_is_int(const dsdl_rational_t r)
{
    return !dsdl_rational_is_nan(r) && dsdl_bigint_is_one(&r.den);
}

static bool dsdl_rational_to_intmax(const dsdl_rational_t r, intmax_t* const out)
{
    if (!dsdl_rational_is_int(r)) {
        return false;
    }
    return dsdl_bigint_to_intmax(&r.num, out);
}

static bool dsdl_rational_to_uintmax(const dsdl_rational_t r, uintmax_t* const out)
{
    if (!dsdl_rational_is_int(r)) {
        return false;
    }
    if (r.num.negative) {
        return false;
    }
    return dsdl_bigint_to_uintmax(&r.num, out);
}

static bool dsdl_rational_to_double(const dsdl_rational_t r, double* const out)
{
    if (dsdl_rational_is_nan(r) || (out == NULL)) {
        return false;
    }
    double num = 0.0;
    double den = 0.0;
    if (!dsdl_bigint_to_double(&r.num, &num) || !dsdl_bigint_to_double(&r.den, &den)) {
        return false;
    }
    if (!(den > 0.0)) {
        return false;
    }
    *out = num / den;
    return isfinite(*out);
}

/// Add two rationals: a/b + c/d = (ad + bc) / bd
static dsdl_rational_t dsdl_rational_add(dsdl_rational_t a, dsdl_rational_t b)
{
    if (dsdl_rational_is_nan(a) || dsdl_rational_is_nan(b)) {
        return dsdl_rational_nan();
    }

    dsdl_bigint_t a_num = a.num;
    dsdl_bigint_t b_num = b.num;
    a_num.negative      = false;
    b_num.negative      = false;

    dsdl_bigint_t left;
    dsdl_bigint_t right;
    dsdl_bigint_t den;
    if (!dsdl_bigint_mul_abs(&a_num, &b.den, &left) || !dsdl_bigint_mul_abs(&b_num, &a.den, &right) ||
        !dsdl_bigint_mul_abs(&a.den, &b.den, &den)) {
        return dsdl_rational_nan();
    }
    left.negative  = a.num.negative;
    right.negative = b.num.negative;

    dsdl_bigint_t num;
    if (!dsdl_bigint_add_signed(&left, &right, &num)) {
        return dsdl_rational_nan();
    }
    dsdl_rational_t r = { num, den };
    return dsdl_rational_normalize(r);
}

/// Subtract two rationals: a/b - c/d = (ad - bc) / bd
static dsdl_rational_t dsdl_rational_sub(dsdl_rational_t a, dsdl_rational_t b)
{
    b.num.negative = !b.num.negative;
    return dsdl_rational_add(a, b);
}

/// Multiply two rationals: a/b * c/d = ac / bd
static dsdl_rational_t dsdl_rational_mul(dsdl_rational_t a, dsdl_rational_t b)
{
    if (dsdl_rational_is_nan(a) || dsdl_rational_is_nan(b)) {
        return dsdl_rational_nan();
    }

    dsdl_bigint_t a_num = a.num;
    dsdl_bigint_t b_num = b.num;
    a_num.negative      = false;
    b_num.negative      = false;

    dsdl_bigint_t num;
    dsdl_bigint_t den;
    if (!dsdl_bigint_mul_abs(&a_num, &b_num, &num) || !dsdl_bigint_mul_abs(&a.den, &b.den, &den)) {
        return dsdl_rational_nan();
    }
    num.negative      = (a.num.negative != b.num.negative);
    dsdl_rational_t r = { num, den };
    return dsdl_rational_normalize(r);
}

/// Divide two rationals: (a/b) / (c/d) = ad / bc
static dsdl_rational_t dsdl_rational_div(dsdl_rational_t a, dsdl_rational_t b)
{
    if (dsdl_rational_is_nan(a) || dsdl_rational_is_nan(b) || dsdl_bigint_is_zero(&b.num)) {
        return dsdl_rational_nan();
    }

    dsdl_bigint_t a_num = a.num;
    dsdl_bigint_t b_num = b.num;
    a_num.negative      = false;
    b_num.negative      = false;

    dsdl_bigint_t num;
    dsdl_bigint_t den;
    if (!dsdl_bigint_mul_abs(&a_num, &b.den, &num) || !dsdl_bigint_mul_abs(&a.den, &b_num, &den)) {
        return dsdl_rational_nan();
    }
    num.negative      = (a.num.negative != b.num.negative);
    dsdl_rational_t r = { num, den };
    return dsdl_rational_normalize(r);
}

/// Negate a rational.
static dsdl_rational_t dsdl_rational_neg(dsdl_rational_t a)
{
    if (!dsdl_bigint_is_zero(&a.num)) {
        a.num.negative = !a.num.negative;
    }
    return a;
}

/// Compare two rationals: returns <0, 0, >0.
static int dsdl_rational_cmp(dsdl_rational_t a, dsdl_rational_t b)
{
    if (dsdl_rational_is_nan(a) || dsdl_rational_is_nan(b)) {
        return 0;
    }
    if (a.num.negative != b.num.negative) {
        if (dsdl_bigint_is_zero(&a.num) && dsdl_bigint_is_zero(&b.num)) {
            return 0;
        }
        return a.num.negative ? -1 : 1;
    }

    dsdl_bigint_t a_num = a.num;
    dsdl_bigint_t b_num = b.num;
    a_num.negative      = false;
    b_num.negative      = false;

    dsdl_bigint_t left;
    dsdl_bigint_t right;
    if (!dsdl_bigint_mul_abs(&a_num, &b.den, &left) || !dsdl_bigint_mul_abs(&b_num, &a.den, &right)) {
        double a_d = 0.0;
        double b_d = 0.0;
        if (dsdl_rational_to_double(a, &a_d) && dsdl_rational_to_double(b, &b_d)) {
            if (a_d < b_d) {
                return -1;
            }
            if (a_d > b_d) {
                return 1;
            }
        }
        return 0;
    }

    int cmp = dsdl_bigint_cmp_abs(&left, &right);
    if (a.num.negative) {
        cmp = -cmp;
    }
    return cmp;
}

/// Convert a double to an exact rational value.
static dsdl_rational_t dsdl_rational_from_double(double x)
{
    if (!isfinite(x)) {
        return dsdl_rational_nan();
    }
    if (!((x > 0.0) || (x < 0.0))) {
        return dsdl_rational_from_int(0);
    }

    const bool negative = x < 0.0;
    if (negative) {
        x = -x;
    }

    int    exp2 = 0;
    double frac = frexp(x, &exp2);
    if (!(frac > 0.0)) {
        return dsdl_rational_from_int(0);
    }

    const int      mant_bits = DBL_MANT_DIG;
    const double   scaled    = ldexp(frac, mant_bits);
    const uint64_t mant      = (uint64_t)scaled;
    exp2 -= mant_bits;

    dsdl_rational_t r;
    if (!dsdl_bigint_from_uintmax(&r.num, (uintmax_t)mant) || !dsdl_bigint_from_uintmax(&r.den, 1U)) {
        return dsdl_rational_nan();
    }

    if (exp2 >= 0) {
        if (!dsdl_bigint_mul_pow2_inplace(&r.num, (uint32_t)exp2)) {
            return dsdl_rational_nan();
        }
    } else {
        if (!dsdl_bigint_mul_pow2_inplace(&r.den, (uint32_t)(-exp2))) {
            return dsdl_rational_nan();
        }
    }

    r.num.negative = negative;
    return dsdl_rational_normalize(r);
}

// Forward declarations - needed by BLS functions below
static void* dsdl_alloc(dsdl_t* self, size_t size);
static void  dsdl_free(dsdl_t* self, void* ptr);
static void* dsdl_realloc(dsdl_t* self, void* ptr, size_t new_size);

// ============================================================================
// Symbolic Bit Length Set
// ============================================================================
//
// Bit length sets are represented symbolically to avoid combinatorial explosion.
// For example, uint8[<=65536][<=65536] would have 4+ billion entries if expanded
// numerically, but symbolically it's just repeat_range(repeat_range({8}, 65536), 65536).
//
// The key insight is that most operations (min, max, modulo) can be computed
// without full expansion. The modulo operation is particularly important for
// checking alignment: {x % divisor} has at most 'divisor' elements.

/// Operator kind for symbolic bit length set expression tree.
typedef enum
{
    dsdl_bls_nullary,      ///< Leaf: concrete set of integers
    dsdl_bls_concat,       ///< Concatenation: sum of cartesian product (struct fields)
    dsdl_bls_repeat,       ///< Fixed repetition: child * k (fixed array)
    dsdl_bls_repeat_range, ///< Range repetition: child * [0..k_max] (variable array)
    dsdl_bls_union,        ///< Set union of children (union variants)
    dsdl_bls_pad,          ///< Padding: align child to boundary
} dsdl_bls_kind_t;

/// Symbolic bit length set node.
/// Instances are heap-allocated via dsdl_alloc().
struct dsdl_bls_t
{
    dsdl_bls_kind_t    kind;
    struct dsdl_bls_t* next_alloc; ///< Internal: allocation tracking list.
    union
    {
        struct
        { ///< nullary: concrete values (small sets, e.g., primitives)
            size_t    count;
            uint64_t* values; ///< Sorted array of distinct values
        } nullary;
        struct
        { ///< concat: children summed (struct)
            size_t       count;
            dsdl_bls_t** children;
        } concat;
        struct
        { ///< repeat: child * k (fixed array)
            dsdl_bls_t* child;
            uint64_t    k;
        } repeat;
        struct
        { ///< repeat_range: child * [0..k_max] (variable array)
            dsdl_bls_t* child;
            uint64_t    k_max;
        } repeat_range;
        struct
        { ///< union: set union of children
            size_t       count;
            dsdl_bls_t** children;
        } set_union;
        struct
        { ///< pad: align child to boundary
            dsdl_bls_t* child;
            uint64_t    alignment;
        } pad;
    } data;
    /// Cached values (SIZE_MAX = not computed yet)
    uint64_t cached_min;
    uint64_t cached_max;
};

/// Sentinel value indicating "not yet computed" for cached min/max.
#define DSDL_BLS_NOT_COMPUTED UINT64_MAX

// Forward declarations for bls operations
static uint64_t dsdl_bls_min(dsdl_bls_t* bls);
static uint64_t dsdl_bls_max(dsdl_bls_t* bls);

static dsdl_bls_t* dsdl_bls_register(dsdl_t* const dsdl, dsdl_bls_t* const bls)
{
    if ((dsdl != NULL) && (bls != NULL)) {
        bls->next_alloc       = dsdl->bls_allocations;
        dsdl->bls_allocations = bls;
    }
    return bls;
}

/// Create a nullary (leaf) bit length set with a single value.
static dsdl_bls_t* dsdl_bls_new_single(dsdl_t* const dsdl, const uint64_t value)
{
    assert(dsdl != NULL);
    dsdl_bls_t* const bls = (dsdl_bls_t*)dsdl_alloc(dsdl, sizeof(dsdl_bls_t) + sizeof(uint64_t));
    if (bls == NULL) {
        return NULL;
    }
    bls->kind               = dsdl_bls_nullary;
    bls->data.nullary.count = 1;
    // Store value immediately after the struct
    bls->data.nullary.values    = (uint64_t*)(bls + 1);
    bls->data.nullary.values[0] = value;
    bls->cached_min             = value;
    bls->cached_max             = value;
    return dsdl_bls_register(dsdl, bls);
}

/// Create a nullary bit length set from an array of values.
/// Values need not be sorted; will be sorted and deduplicated.
static dsdl_bls_t* dsdl_bls_new_set(dsdl_t* const dsdl, const size_t count, const uint64_t* const values)
{
    assert((dsdl != NULL) && ((values != NULL) || (count == 0)));
    if (count == 0) {
        return NULL; // Empty sets are invalid
    }
    // Allocate space for struct + values
    dsdl_bls_t* const bls = (dsdl_bls_t*)dsdl_alloc(dsdl, sizeof(dsdl_bls_t) + count * sizeof(uint64_t));
    if (bls == NULL) {
        return NULL;
    }
    bls->kind                = dsdl_bls_nullary;
    bls->data.nullary.values = (uint64_t*)(bls + 1);

    // Copy and sort values (simple insertion sort for small sets)
    size_t n = 0;
    for (size_t i = 0; i < count; i++) {
        const uint64_t v = values[i];
        // Find insertion point (maintain sorted order)
        size_t j = n;
        while ((j > 0) && (bls->data.nullary.values[j - 1] > v)) {
            j--;
        }
        // Skip duplicates (check BEFORE shifting)
        if ((j > 0) && (bls->data.nullary.values[j - 1] == v)) {
            continue;
        }
        if ((j < n) && (bls->data.nullary.values[j] == v)) {
            continue;
        }
        // Shift elements to make room for insertion
        for (size_t k = n; k > j; k--) {
            bls->data.nullary.values[k] = bls->data.nullary.values[k - 1];
        }
        bls->data.nullary.values[j] = v;
        n++;
    }
    bls->data.nullary.count = n;
    bls->cached_min         = bls->data.nullary.values[0];
    bls->cached_max         = bls->data.nullary.values[n - 1];
    return dsdl_bls_register(dsdl, bls);
}

/// Create a concatenation (sum of cartesian product) of bit length sets.
/// Represents struct field concatenation: total = f1 + f2 + ... + fn
static dsdl_bls_t* dsdl_bls_new_concat(dsdl_t* const dsdl, const size_t count, dsdl_bls_t** const children)
{
    assert((dsdl != NULL) && ((children != NULL) || (count == 0)));
    if (count == 0) {
        return dsdl_bls_new_single(dsdl, 0); // Empty concat = {0}
    }
    if (count == 1) {
        return children[0]; // Single child optimization
    }
    dsdl_bls_t* const bls = (dsdl_bls_t*)dsdl_alloc(dsdl, sizeof(dsdl_bls_t) + count * sizeof(dsdl_bls_t*));
    if (bls == NULL) {
        return NULL;
    }
    bls->kind                 = dsdl_bls_concat;
    bls->data.concat.count    = count;
    bls->data.concat.children = (dsdl_bls_t**)(bls + 1);
    for (size_t i = 0; i < count; i++) {
        bls->data.concat.children[i] = children[i];
    }
    bls->cached_min = DSDL_BLS_NOT_COMPUTED;
    bls->cached_max = DSDL_BLS_NOT_COMPUTED;
    return dsdl_bls_register(dsdl, bls);
}

/// Create a fixed repetition: child repeated k times.
/// Represents fixed-length array: element[k]
static dsdl_bls_t* dsdl_bls_new_repeat(dsdl_t* const dsdl, dsdl_bls_t* const child, const uint64_t k)
{
    assert(dsdl != NULL);
    if (k == 0) {
        return dsdl_bls_new_single(dsdl, 0); // Empty repetition = {0}
    }
    assert(child != NULL); // Required for k > 0
    if (k == 1) {
        return child; // Single repetition optimization
    }
    dsdl_bls_t* const bls = (dsdl_bls_t*)dsdl_alloc(dsdl, sizeof(dsdl_bls_t));
    if (bls == NULL) {
        return NULL;
    }
    bls->kind              = dsdl_bls_repeat;
    bls->data.repeat.child = child;
    bls->data.repeat.k     = k;
    bls->cached_min        = DSDL_BLS_NOT_COMPUTED;
    bls->cached_max        = DSDL_BLS_NOT_COMPUTED;
    return dsdl_bls_register(dsdl, bls);
}

/// Create a range repetition: child repeated 0 to k_max times.
/// Represents variable-length array: element[<=k_max]
static dsdl_bls_t* dsdl_bls_new_repeat_range(dsdl_t* const dsdl, dsdl_bls_t* const child, const uint64_t k_max)
{
    assert((dsdl != NULL) && (child != NULL));
    dsdl_bls_t* const bls = (dsdl_bls_t*)dsdl_alloc(dsdl, sizeof(dsdl_bls_t));
    if (bls == NULL) {
        return NULL;
    }
    bls->kind                    = dsdl_bls_repeat_range;
    bls->data.repeat_range.child = child;
    bls->data.repeat_range.k_max = k_max;
    bls->cached_min              = 0; // Always includes k=0 case
    bls->cached_max              = DSDL_BLS_NOT_COMPUTED;
    return dsdl_bls_register(dsdl, bls);
}

/// Create a union (set union) of bit length sets.
/// Represents union variants: max of all alternatives
static dsdl_bls_t* dsdl_bls_new_unite(dsdl_t* const dsdl, const size_t count, dsdl_bls_t** const children)
{
    assert((dsdl != NULL) && ((children != NULL) || (count == 0)));
    if (count == 0) {
        return dsdl_bls_new_single(dsdl, 0);
    }
    if (count == 1) {
        return children[0];
    }
    if (count > ((SIZE_MAX - sizeof(dsdl_bls_t)) / sizeof(dsdl_bls_t*))) {
        return NULL;
    }
    dsdl_bls_t* const bls = (dsdl_bls_t*)dsdl_alloc(dsdl, sizeof(dsdl_bls_t) + count * sizeof(dsdl_bls_t*));
    if (bls == NULL) {
        return NULL;
    }
    bls->kind                    = dsdl_bls_union;
    bls->data.set_union.count    = count;
    bls->data.set_union.children = (dsdl_bls_t**)(bls + 1);
    for (size_t i = 0; i < count; i++) {
        bls->data.set_union.children[i] = children[i];
    }
    bls->cached_min = DSDL_BLS_NOT_COMPUTED;
    bls->cached_max = DSDL_BLS_NOT_COMPUTED;
    return dsdl_bls_register(dsdl, bls);
}

/// Create a padding operator: align child to boundary.
/// Adds 0 to (alignment-1) padding bits.
static dsdl_bls_t* dsdl_bls_new_pad(dsdl_t* const dsdl, dsdl_bls_t* const child, const uint64_t alignment)
{
    assert((dsdl != NULL) && (child != NULL));
    if (alignment <= 1) {
        return child; // No-op for alignment 1
    }
    dsdl_bls_t* const bls = (dsdl_bls_t*)dsdl_alloc(dsdl, sizeof(dsdl_bls_t));
    if (bls == NULL) {
        return NULL;
    }
    bls->kind               = dsdl_bls_pad;
    bls->data.pad.child     = child;
    bls->data.pad.alignment = alignment;
    bls->cached_min         = DSDL_BLS_NOT_COMPUTED;
    bls->cached_max         = DSDL_BLS_NOT_COMPUTED;
    return dsdl_bls_register(dsdl, bls);
}

static uint64_t* dsdl_alloc_u64_array(dsdl_t* const dsdl, const uint64_t count)
{
    if ((dsdl == NULL) || (count == 0)) {
        return NULL;
    }
    if (count > (SIZE_MAX / sizeof(uint64_t))) {
        return NULL;
    }
    return (uint64_t*)dsdl_alloc(dsdl, (size_t)count * sizeof(uint64_t));
}

/// Helper: round up x to next multiple of alignment.
static uint64_t dsdl_align_up(const uint64_t x, const uint64_t alignment)
{
    return ((x + alignment - 1) / alignment) * alignment;
}

static uint64_t dsdl_gcd_u64(uint64_t a, uint64_t b)
{
    while (b != 0U) {
        const uint64_t r = a % b;
        a                = b;
        b                = r;
    }
    return a;
}

static uintmax_t dsdl_gcd_uintmax(uintmax_t a, uintmax_t b)
{
    while (b != 0U) {
        const uintmax_t r = a % b;
        a                 = b;
        b                 = r;
    }
    return a;
}

/// Compute minimum value in a bit length set.
static uint64_t dsdl_bls_min(dsdl_bls_t* const bls)
{
    if (bls == NULL) {
        return 0;
    }
    if (bls->cached_min != DSDL_BLS_NOT_COMPUTED) {
        return bls->cached_min;
    }

    uint64_t result = 0;
    switch (bls->kind) {
        case dsdl_bls_nullary:
            result = bls->data.nullary.values[0]; // Already sorted
            break;

        case dsdl_bls_concat: {
            result = 0;
            for (size_t i = 0; i < bls->data.concat.count; i++) {
                result += dsdl_bls_min(bls->data.concat.children[i]);
            }
            break;
        }

        case dsdl_bls_repeat:
            result = dsdl_bls_min(bls->data.repeat.child) * bls->data.repeat.k;
            break;

        case dsdl_bls_repeat_range:
            result = 0; // k=0 case
            break;

        case dsdl_bls_union: {
            result = UINT64_MAX;
            for (size_t i = 0; i < bls->data.set_union.count; i++) {
                const uint64_t child_min = dsdl_bls_min(bls->data.set_union.children[i]);
                if (child_min < result) {
                    result = child_min;
                }
            }
            break;
        }

        case dsdl_bls_pad:
            result = dsdl_align_up(dsdl_bls_min(bls->data.pad.child), bls->data.pad.alignment);
            break;
    }

    bls->cached_min = result;
    return result;
}

/// Compute maximum value in a bit length set.
static uint64_t dsdl_bls_max(dsdl_bls_t* const bls)
{
    if (bls == NULL) {
        return 0;
    }
    if (bls->cached_max != DSDL_BLS_NOT_COMPUTED) {
        return bls->cached_max;
    }

    uint64_t result = 0;
    switch (bls->kind) {
        case dsdl_bls_nullary:
            result = bls->data.nullary.values[bls->data.nullary.count - 1]; // Already sorted
            break;

        case dsdl_bls_concat: {
            result = 0;
            for (size_t i = 0; i < bls->data.concat.count; i++) {
                result += dsdl_bls_max(bls->data.concat.children[i]);
            }
            break;
        }

        case dsdl_bls_repeat:
            result = dsdl_bls_max(bls->data.repeat.child) * bls->data.repeat.k;
            break;

        case dsdl_bls_repeat_range:
            result = dsdl_bls_max(bls->data.repeat_range.child) * bls->data.repeat_range.k_max;
            break;

        case dsdl_bls_union: {
            result = 0;
            for (size_t i = 0; i < bls->data.set_union.count; i++) {
                const uint64_t child_max = dsdl_bls_max(bls->data.set_union.children[i]);
                if (child_max > result) {
                    result = child_max;
                }
            }
            break;
        }

        case dsdl_bls_pad:
            result = dsdl_align_up(dsdl_bls_max(bls->data.pad.child), bls->data.pad.alignment);
            break;
    }

    bls->cached_max = result;
    return result;
}

/// Check if bit length set has fixed length (min == max).
static bool dsdl_bls_is_fixed(dsdl_bls_t* const bls) { return dsdl_bls_min(bls) == dsdl_bls_max(bls); }

/// Compute modulo of all values in a bit length set.
/// Returns the count of unique results, stores results in out_values (must have space for 'divisor' elements).
/// This is the key operation for checking alignment without combinatorial explosion.
static uint64_t dsdl_bls_modulo(dsdl_t* const     dsdl,
                                dsdl_bls_t* const bls,
                                const uint64_t    divisor,
                                uint64_t* const   out_values)
{
    assert(dsdl != NULL);
    if ((bls == NULL) || (divisor == 0)) {
        return 0;
    }

    // Use a bitmap for tracking which remainders we've seen (works for divisor <= 64*8 = 512)
    // For larger divisors, fall back to linear scan.
    uint64_t   seen_bitmap[8] = { 0 };
    const bool use_bitmap     = divisor <= sizeof(seen_bitmap) * 8;

    uint64_t count = 0;

    switch (bls->kind) {
        case dsdl_bls_nullary: {
            for (size_t i = 0; i < bls->data.nullary.count; i++) {
                const uint64_t r = bls->data.nullary.values[i] % divisor;
                if (use_bitmap) {
                    const uint64_t idx = r / 64;
                    const uint64_t bit = ((uint64_t)1) << (r % 64);
                    if ((seen_bitmap[idx] & bit) == 0) {
                        seen_bitmap[idx] |= bit;
                        out_values[count++] = r;
                    }
                } else {
                    // Linear scan for large divisors
                    bool found = false;
                    for (uint64_t j = 0; j < count; j++) {
                        if (out_values[j] == r) {
                            found = true;
                            break;
                        }
                    }
                    if (!found) {
                        out_values[count++] = r;
                    }
                }
            }
            break;
        }

        case dsdl_bls_concat: {
            // For concatenation, we need modular sum of all combinations.
            // Start with {0} and add each child's modulo set.
            out_values[0] = 0;
            count         = 1;

            // Allocate temp array for child modulos
            uint64_t* const child_mods = dsdl_alloc_u64_array(dsdl, divisor);
            if (child_mods == NULL) {
                return 0; // OOM
            }

            for (size_t i = 0; i < bls->data.concat.count; i++) {
                // Get child's modulo values
                const uint64_t child_count = dsdl_bls_modulo(dsdl, bls->data.concat.children[i], divisor, child_mods);

                // Compute new modulo set: {(a + b) % divisor | a in current, b in child}
                uint64_t new_count = 0;
                if (use_bitmap) {
                    memset(seen_bitmap, 0, sizeof(seen_bitmap));
                }
                for (uint64_t j = 0; j < count; j++) {
                    for (uint64_t k = 0; k < child_count; k++) {
                        const uint64_t r = (out_values[j] + child_mods[k]) % divisor;
                        if (use_bitmap) {
                            const uint64_t idx = r / 64;
                            const uint64_t bit = ((uint64_t)1) << (r % 64);
                            if ((seen_bitmap[idx] & bit) == 0) {
                                seen_bitmap[idx] |= bit;
                                out_values[new_count++] = r;
                            }
                        } else {
                            bool found = false;
                            for (uint64_t m = 0; m < new_count; m++) {
                                if (out_values[m] == r) {
                                    found = true;
                                    break;
                                }
                            }
                            if (!found) {
                                out_values[new_count++] = r;
                            }
                        }
                    }
                }
                count = new_count;
            }
            dsdl_free(dsdl, child_mods);
            break;
        }

        case dsdl_bls_repeat: {
            // For repeat(k), we need k-multicombinations of child modulos summed.
            // Optimization: for k >= divisor, pattern repeats, so use equivalent_k.
            const uint64_t k            = bls->data.repeat.k;
            const uint64_t equivalent_k = (k < divisor) ? k : (divisor + k % divisor);

            // Get child's modulo values
            uint64_t* const child_mods = dsdl_alloc_u64_array(dsdl, divisor);
            if (child_mods == NULL) {
                return 0; // OOM
            }
            const uint64_t child_count = dsdl_bls_modulo(dsdl, bls->data.repeat.child, divisor, child_mods);

            // Start with {0} (k=0 gives 0, but k>=1 here since we don't reach this for k=0)
            // Actually for repeat, k is fixed, so we need k iterations of adding child_mods.
            out_values[0] = 0;
            count         = 1;

            for (uint64_t rep = 0; rep < equivalent_k; rep++) {
                uint64_t new_count = 0;
                if (use_bitmap) {
                    memset(seen_bitmap, 0, sizeof(seen_bitmap));
                }
                for (uint64_t j = 0; j < count; j++) {
                    for (uint64_t k2 = 0; k2 < child_count; k2++) {
                        const uint64_t r = (out_values[j] + child_mods[k2]) % divisor;
                        if (use_bitmap) {
                            const uint64_t idx = r / 64;
                            const uint64_t bit = ((uint64_t)1) << (r % 64);
                            if ((seen_bitmap[idx] & bit) == 0) {
                                seen_bitmap[idx] |= bit;
                                out_values[new_count++] = r;
                            }
                        } else {
                            bool found = false;
                            for (uint64_t m = 0; m < new_count; m++) {
                                if (out_values[m] == r) {
                                    found = true;
                                    break;
                                }
                            }
                            if (!found) {
                                out_values[new_count++] = r;
                            }
                        }
                    }
                }
                count = new_count;
            }
            dsdl_free(dsdl, child_mods);
            break;
        }

        case dsdl_bls_repeat_range: {
            // For repeat_range(k_max), we include all k in [0, k_max].
            // Optimization: for k_max >= divisor, pattern repeats.
            const uint64_t k_max            = bls->data.repeat_range.k_max;
            const uint64_t equivalent_k_max = (k_max < divisor) ? k_max : (divisor + k_max % divisor);

            // Get child's modulo values
            uint64_t* const child_mods = dsdl_alloc_u64_array(dsdl, divisor);
            if (child_mods == NULL) {
                return 0; // OOM
            }
            const uint64_t child_count = dsdl_bls_modulo(dsdl, bls->data.repeat_range.child, divisor, child_mods);

            // Include k=0 case: {0}
            out_values[0] = 0;
            count         = 1;
            if (use_bitmap) {
                seen_bitmap[0] |= 1; // Mark 0 as seen
            }

            // Running set of sums for current k
            uint64_t* const running = dsdl_alloc_u64_array(dsdl, divisor);
            if (running == NULL) {
                dsdl_free(dsdl, child_mods);
                return 0; // OOM
            }
            running[0]             = 0;
            uint64_t running_count = 1;

            for (uint64_t k = 1; k <= equivalent_k_max; k++) {
                // Add one more child to running sums
                uint64_t new_running_count = 0;
                uint64_t running_bitmap[8] = { 0 };

                for (uint64_t j = 0; j < running_count; j++) {
                    for (uint64_t m = 0; m < child_count; m++) {
                        const uint64_t r   = (running[j] + child_mods[m]) % divisor;
                        const uint64_t idx = r / 64;
                        const uint64_t bit = ((uint64_t)1) << (r % 64);
                        if ((running_bitmap[idx] & bit) == 0) {
                            running_bitmap[idx] |= bit;
                            running[new_running_count++] = r;
                        }
                    }
                }
                running_count = new_running_count;

                // Add to output (union with existing)
                for (uint64_t j = 0; j < running_count; j++) {
                    const uint64_t r = running[j];
                    if (use_bitmap) {
                        const uint64_t idx = r / 64;
                        const uint64_t bit = ((uint64_t)1) << (r % 64);
                        if ((seen_bitmap[idx] & bit) == 0) {
                            seen_bitmap[idx] |= bit;
                            out_values[count++] = r;
                        }
                    } else {
                        bool found = false;
                        for (uint64_t m = 0; m < count; m++) {
                            if (out_values[m] == r) {
                                found = true;
                                break;
                            }
                        }
                        if (!found) {
                            out_values[count++] = r;
                        }
                    }
                }
            }
            dsdl_free(dsdl, running);
            dsdl_free(dsdl, child_mods);
            break;
        }

        case dsdl_bls_union: {
            // Union: collect all modulos from all children
            uint64_t* const child_mods = dsdl_alloc_u64_array(dsdl, divisor);
            if (child_mods == NULL) {
                return 0; // OOM
            }
            for (size_t i = 0; i < bls->data.set_union.count; i++) {
                const uint64_t child_count =
                  dsdl_bls_modulo(dsdl, bls->data.set_union.children[i], divisor, child_mods);
                for (uint64_t j = 0; j < child_count; j++) {
                    const uint64_t r = child_mods[j];
                    if (use_bitmap) {
                        const uint64_t idx = r / 64;
                        const uint64_t bit = ((uint64_t)1) << (r % 64);
                        if ((seen_bitmap[idx] & bit) == 0) {
                            seen_bitmap[idx] |= bit;
                            out_values[count++] = r;
                        }
                    } else {
                        bool found = false;
                        for (uint64_t m = 0; m < count; m++) {
                            if (out_values[m] == r) {
                                found = true;
                                break;
                            }
                        }
                        if (!found) {
                            out_values[count++] = r;
                        }
                    }
                }
            }
            dsdl_free(dsdl, child_mods);
            break;
        }

        case dsdl_bls_pad: {
            // Padding: round each child value up to alignment, then take modulo
            const uint64_t alignment = bls->data.pad.alignment;
            // We need child % lcm(alignment, divisor), but that's complex.
            // Simpler: get child modulo (lcm), apply padding, then modulo divisor.
            const uint64_t lcm = (alignment * divisor) / dsdl_gcd_u64(alignment, divisor);

            uint64_t* const child_mods = dsdl_alloc_u64_array(dsdl, lcm);
            if (child_mods == NULL) {
                return 0; // OOM
            }
            const uint64_t child_count = dsdl_bls_modulo(dsdl, bls->data.pad.child, lcm, child_mods);

            for (uint64_t i = 0; i < child_count; i++) {
                const uint64_t padded = dsdl_align_up(child_mods[i], alignment);
                const uint64_t r      = padded % divisor;
                if (use_bitmap) {
                    const uint64_t idx = r / 64;
                    const uint64_t bit = ((uint64_t)1) << (r % 64);
                    if ((seen_bitmap[idx] & bit) == 0) {
                        seen_bitmap[idx] |= bit;
                        out_values[count++] = r;
                    }
                } else {
                    bool found = false;
                    for (uint64_t j = 0; j < count; j++) {
                        if (out_values[j] == r) {
                            found = true;
                            break;
                        }
                    }
                    if (!found) {
                        out_values[count++] = r;
                    }
                }
            }
            dsdl_free(dsdl, child_mods);
            break;
        }
    }

    return count;
}

/// Check if all values in a bit length set are aligned at the given boundary.
/// Returns true iff {x % alignment} == {0} for all x in the set.
static bool dsdl_bls_is_aligned(dsdl_t* const dsdl, dsdl_bls_t* const bls, const uint64_t alignment)
{
    assert(dsdl != NULL);
    if ((bls == NULL) || (alignment <= 1)) {
        return true;
    }
    uint64_t* const mods = dsdl_alloc_u64_array(dsdl, alignment);
    if (mods == NULL) {
        return false; // OOM - conservative: assume not aligned
    }
    const uint64_t count  = dsdl_bls_modulo(dsdl, bls, alignment, mods);
    const bool     result = (count == 1) && (mods[0] == 0);
    dsdl_free(dsdl, mods);
    return result;
}

static size_t dsdl_u64_sort_dedup(uint64_t* const values, const size_t count)
{
    if ((values == NULL) || (count < 2)) {
        return count;
    }
    // Insertion sort for small sets.
    for (size_t i = 1; i < count; i++) {
        const uint64_t key = values[i];
        size_t         j   = i;
        while ((j > 0) && (values[j - 1] > key)) {
            values[j] = values[j - 1];
            j--;
        }
        values[j] = key;
    }
    size_t out = 1;
    for (size_t i = 1; i < count; i++) {
        if (values[i] != values[out - 1]) {
            values[out++] = values[i];
        }
    }
    return out;
}

static bool dsdl_bls_expand(dsdl_t* const     dsdl,
                            dsdl_bls_t* const bls,
                            uint64_t** const  out_values,
                            size_t* const     out_count)
{
    if ((dsdl == NULL) || (bls == NULL) || (out_values == NULL) || (out_count == NULL)) {
        return false;
    }

    switch (bls->kind) {
        case dsdl_bls_nullary: {
            const size_t    count  = bls->data.nullary.count;
            uint64_t* const values = (uint64_t*)dsdl_alloc(dsdl, count * sizeof(uint64_t));
            if ((values == NULL) && (count > 0)) {
                return false;
            }
            for (size_t i = 0; i < count; i++) {
                values[i] = bls->data.nullary.values[i];
            }
            *out_values = values;
            *out_count  = count;
            return true;
        }

        case dsdl_bls_union: {
            uint64_t* values = NULL;
            size_t    count  = 0;
            for (size_t i = 0; i < bls->data.set_union.count; i++) {
                uint64_t* child_values = NULL;
                size_t    child_count  = 0;
                if (!dsdl_bls_expand(dsdl, bls->data.set_union.children[i], &child_values, &child_count)) {
                    dsdl_free(dsdl, values);
                    return false;
                }
                if (child_count > 0) {
                    if (count > SIZE_MAX - child_count) {
                        dsdl_free(dsdl, child_values);
                        dsdl_free(dsdl, values);
                        return false;
                    }
                    uint64_t* const new_values =
                      (uint64_t*)dsdl_realloc(dsdl, values, (count + child_count) * sizeof(uint64_t));
                    if (new_values == NULL) {
                        dsdl_free(dsdl, child_values);
                        dsdl_free(dsdl, values);
                        return false;
                    }
                    values = new_values;
                    memcpy(values + count, child_values, child_count * sizeof(uint64_t));
                    count += child_count;
                }
                dsdl_free(dsdl, child_values);
            }
            count       = dsdl_u64_sort_dedup(values, count);
            *out_values = values;
            *out_count  = count;
            return count > 0;
        }

        case dsdl_bls_concat: {
            uint64_t* sums       = (uint64_t*)dsdl_alloc(dsdl, sizeof(uint64_t));
            size_t    sums_count = 1;
            if (sums == NULL) {
                return false;
            }
            sums[0] = 0;
            for (size_t i = 0; i < bls->data.concat.count; i++) {
                uint64_t* child_values = NULL;
                size_t    child_count  = 0;
                if (!dsdl_bls_expand(dsdl, bls->data.concat.children[i], &child_values, &child_count)) {
                    dsdl_free(dsdl, sums);
                    return false;
                }
                if ((child_count > 0) && (sums_count > SIZE_MAX / child_count)) {
                    dsdl_free(dsdl, child_values);
                    dsdl_free(dsdl, sums);
                    return false;
                }
                const size_t    new_count = sums_count * child_count;
                uint64_t* const new_sums =
                  (new_count > 0) ? (uint64_t*)dsdl_alloc(dsdl, new_count * sizeof(uint64_t)) : NULL;
                if ((new_sums == NULL) && (new_count > 0)) {
                    dsdl_free(dsdl, child_values);
                    dsdl_free(dsdl, sums);
                    return false;
                }
                size_t idx = 0;
                for (size_t s = 0; s < sums_count; s++) {
                    for (size_t c = 0; c < child_count; c++) {
                        if (UINT64_MAX - sums[s] < child_values[c]) {
                            dsdl_free(dsdl, child_values);
                            dsdl_free(dsdl, sums);
                            dsdl_free(dsdl, new_sums);
                            return false;
                        }
                        new_sums[idx++] = sums[s] + child_values[c];
                    }
                }
                dsdl_free(dsdl, child_values);
                dsdl_free(dsdl, sums);
                sums       = new_sums;
                sums_count = dsdl_u64_sort_dedup(sums, new_count);
            }
            *out_values = sums;
            *out_count  = sums_count;
            return sums_count > 0;
        }

        case dsdl_bls_repeat: {
            uint64_t* child_values = NULL;
            size_t    child_count  = 0;
            if (!dsdl_bls_expand(dsdl, bls->data.repeat.child, &child_values, &child_count)) {
                return false;
            }
            uint64_t* sums       = (uint64_t*)dsdl_alloc(dsdl, sizeof(uint64_t));
            size_t    sums_count = 1;
            if (sums == NULL) {
                dsdl_free(dsdl, child_values);
                return false;
            }
            sums[0] = 0;
            for (uint64_t rep = 0; rep < bls->data.repeat.k; rep++) {
                if ((child_count > 0) && (sums_count > SIZE_MAX / child_count)) {
                    dsdl_free(dsdl, child_values);
                    dsdl_free(dsdl, sums);
                    return false;
                }
                const size_t    new_count = sums_count * child_count;
                uint64_t* const new_sums =
                  (new_count > 0) ? (uint64_t*)dsdl_alloc(dsdl, new_count * sizeof(uint64_t)) : NULL;
                if ((new_sums == NULL) && (new_count > 0)) {
                    dsdl_free(dsdl, child_values);
                    dsdl_free(dsdl, sums);
                    return false;
                }
                size_t idx = 0;
                for (size_t s = 0; s < sums_count; s++) {
                    for (size_t c = 0; c < child_count; c++) {
                        if (UINT64_MAX - sums[s] < child_values[c]) {
                            dsdl_free(dsdl, child_values);
                            dsdl_free(dsdl, sums);
                            dsdl_free(dsdl, new_sums);
                            return false;
                        }
                        new_sums[idx++] = sums[s] + child_values[c];
                    }
                }
                dsdl_free(dsdl, sums);
                sums       = new_sums;
                sums_count = dsdl_u64_sort_dedup(sums, new_count);
            }
            dsdl_free(dsdl, child_values);
            *out_values = sums;
            *out_count  = sums_count;
            return sums_count > 0;
        }

        case dsdl_bls_repeat_range: {
            uint64_t* child_values = NULL;
            size_t    child_count  = 0;
            if (!dsdl_bls_expand(dsdl, bls->data.repeat_range.child, &child_values, &child_count)) {
                return false;
            }
            uint64_t* total   = (uint64_t*)dsdl_alloc(dsdl, sizeof(uint64_t));
            uint64_t* current = (uint64_t*)dsdl_alloc(dsdl, sizeof(uint64_t));
            if ((total == NULL) || (current == NULL)) {
                dsdl_free(dsdl, child_values);
                dsdl_free(dsdl, total);
                dsdl_free(dsdl, current);
                return false;
            }
            size_t total_count   = 1;
            size_t current_count = 1;
            total[0]             = 0;
            current[0]           = 0;

            for (uint64_t rep = 1; rep <= bls->data.repeat_range.k_max; rep++) {
                if ((child_count > 0) && (current_count > SIZE_MAX / child_count)) {
                    dsdl_free(dsdl, child_values);
                    dsdl_free(dsdl, total);
                    dsdl_free(dsdl, current);
                    return false;
                }
                const size_t    new_count = current_count * child_count;
                uint64_t* const new_values =
                  (new_count > 0) ? (uint64_t*)dsdl_alloc(dsdl, new_count * sizeof(uint64_t)) : NULL;
                if ((new_values == NULL) && (new_count > 0)) {
                    dsdl_free(dsdl, child_values);
                    dsdl_free(dsdl, total);
                    dsdl_free(dsdl, current);
                    return false;
                }
                size_t idx = 0;
                for (size_t s = 0; s < current_count; s++) {
                    for (size_t c = 0; c < child_count; c++) {
                        if (UINT64_MAX - current[s] < child_values[c]) {
                            dsdl_free(dsdl, child_values);
                            dsdl_free(dsdl, total);
                            dsdl_free(dsdl, current);
                            dsdl_free(dsdl, new_values);
                            return false;
                        }
                        new_values[idx++] = current[s] + child_values[c];
                    }
                }
                dsdl_free(dsdl, current);
                current       = new_values;
                current_count = dsdl_u64_sort_dedup(current, new_count);

                if (total_count > SIZE_MAX - current_count) {
                    dsdl_free(dsdl, child_values);
                    dsdl_free(dsdl, total);
                    dsdl_free(dsdl, current);
                    return false;
                }
                uint64_t* const new_total =
                  (uint64_t*)dsdl_realloc(dsdl, total, (total_count + current_count) * sizeof(uint64_t));
                if (new_total == NULL) {
                    dsdl_free(dsdl, child_values);
                    dsdl_free(dsdl, total);
                    dsdl_free(dsdl, current);
                    return false;
                }
                total = new_total;
                memcpy(total + total_count, current, current_count * sizeof(uint64_t));
                total_count += current_count;
            }

            dsdl_free(dsdl, child_values);
            dsdl_free(dsdl, current);
            total_count = dsdl_u64_sort_dedup(total, total_count);
            *out_values = total;
            *out_count  = total_count;
            return total_count > 0;
        }

        case dsdl_bls_pad: {
            uint64_t* child_values = NULL;
            size_t    child_count  = 0;
            if (!dsdl_bls_expand(dsdl, bls->data.pad.child, &child_values, &child_count)) {
                return false;
            }
            uint64_t* const values =
              (child_count > 0) ? (uint64_t*)dsdl_alloc(dsdl, child_count * sizeof(uint64_t)) : NULL;
            if ((values == NULL) && (child_count > 0)) {
                dsdl_free(dsdl, child_values);
                return false;
            }
            for (size_t i = 0; i < child_count; i++) {
                values[i] = dsdl_align_up(child_values[i], bls->data.pad.alignment);
            }
            dsdl_free(dsdl, child_values);
            child_count = dsdl_u64_sort_dedup(values, child_count);
            *out_values = values;
            *out_count  = child_count;
            return child_count > 0;
        }
    }
    return false;
}

static bool dsdl_value_from_bls(dsdl_t* const dsdl, dsdl_bls_t* const bls, dsdl_value_t* const out)
{
    if ((dsdl == NULL) || (out == NULL)) {
        return false;
    }
    uint64_t* values = NULL;
    size_t    count  = 0;
    if (!dsdl_bls_expand(dsdl, bls, &values, &count)) {
        return false;
    }
    dsdl_value_t* const elements = (count > 0) ? (dsdl_value_t*)dsdl_alloc(dsdl, count * sizeof(dsdl_value_t)) : NULL;
    if ((elements == NULL) && (count > 0)) {
        dsdl_free(dsdl, values);
        return false;
    }
    for (size_t i = 0; i < count; i++) {
        elements[i].kind        = dsdl_value_rational;
        elements[i].flags       = 0;
        elements[i].as.rational = dsdl_rational_from_uintmax((uintmax_t)values[i]);
    }
    dsdl_free(dsdl, values);
    out->kind            = dsdl_value_set;
    out->flags           = DSDL_VALUE_FLAG_OWNED;
    out->as.set.count    = count;
    out->as.set.elements = elements;
    return true;
}

// ============================================================================
// Parser state
// ============================================================================

/// Internal parser state.
typedef struct
{
    const char*          input;    ///< Input buffer (not NUL-terminated necessarily)
    size_t               len;      ///< Input length in bytes
    size_t               pos;      ///< Current parse position
    size_t               line;     ///< Current line number (1-based)
    size_t               col;      ///< Current column number (1-based)
    dsdl_t*              dsdl;     ///< Parent state for memory allocation and type cache
    dsdl_eval_context_t* eval_ctx; ///< Expression evaluation context (optional)
} dsdl_parser_t;

// ============================================================================
// Memory management helpers
// ============================================================================

static void* dsdl_alloc(dsdl_t* const self, const size_t size)
{
    assert((self != NULL) && (self->realloc != NULL));
    if (size == 0) {
        return NULL;
    }
    return self->realloc(self, NULL, size);
}

static void dsdl_free(dsdl_t* const self, void* const ptr)
{
    assert((self != NULL) && (self->realloc != NULL));
    if (ptr != NULL) {
        (void)self->realloc(self, ptr, 0);
    }
}

/// Free a const char* string that was allocated via realloc.
/// This helper exists because wkv_str_t.str is const char*, but we need to free
/// strings returned by the read() callback which are owned by us.
static void dsdl_free_str(dsdl_t* const self, const char* const str)
{
    if (str != NULL) {
        union
        {
            const char* c;
            void*       m;
        } u;
        u.c = str;
        dsdl_free(self, u.m);
    }
}

static void* dsdl_realloc(dsdl_t* const self, void* const ptr, const size_t new_size)
{
    assert((self != NULL) && (self->realloc != NULL));
    return self->realloc(self, ptr, new_size);
}

typedef struct dsdl_type_alloc_t
{
    struct dsdl_type_alloc_t* next;
} dsdl_type_alloc_t;

static void* dsdl_type_alloc(dsdl_t* const self, const size_t size)
{
    if (self == NULL) {
        return NULL;
    }
    if (size > (SIZE_MAX - sizeof(dsdl_type_alloc_t))) {
        return NULL;
    }
    dsdl_type_alloc_t* const alloc = (dsdl_type_alloc_t*)dsdl_alloc(self, sizeof(*alloc) + size);
    if (alloc == NULL) {
        return NULL;
    }
    alloc->next            = self->type_allocations;
    self->type_allocations = alloc;
    return (void*)(alloc + 1);
}

static void dsdl_value_dispose(dsdl_t* const dsdl, dsdl_value_t* const value);

static bool dsdl_value_set_string_copy(dsdl_t* const dsdl, const wkv_str_t src, dsdl_value_t* const out)
{
    if ((dsdl == NULL) || (out == NULL)) {
        return false;
    }
    if ((src.len > 0) && (src.str == NULL)) {
        return false;
    }
    char* buf = NULL;
    if (src.len > 0) {
        buf = (char*)dsdl_alloc(dsdl, src.len);
        if (buf == NULL) {
            return false;
        }
        (void)memcpy(buf, src.str, src.len);
    }
    out->kind      = dsdl_value_string;
    out->flags     = DSDL_VALUE_FLAG_OWNED;
    out->as.string = (wkv_str_t){ .len = src.len, .str = buf };
    return true;
}

static bool dsdl_copy_wkv_str(dsdl_t* const dsdl, const wkv_str_t src, wkv_str_t* const out)
{
    if ((dsdl == NULL) || (out == NULL)) {
        return false;
    }
    out->len = src.len;
    out->str = NULL;
    if ((src.len > 0) && (src.str == NULL)) {
        return false;
    }
    if (src.len > 0) {
        char* const buf = (char*)dsdl_alloc(dsdl, src.len);
        if (buf == NULL) {
            return false;
        }
        (void)memcpy(buf, src.str, src.len);
        out->str = buf;
    }
    return true;
}

static bool dsdl_value_clone(dsdl_t* const dsdl, const dsdl_value_t* const src, dsdl_value_t* const out)
{
    if ((dsdl == NULL) || (src == NULL) || (out == NULL)) {
        return false;
    }
    switch (src->kind) {
        case dsdl_value_rational:
            out->kind        = dsdl_value_rational;
            out->flags       = 0;
            out->as.rational = src->as.rational;
            return true;
        case dsdl_value_bool:
            out->kind       = dsdl_value_bool;
            out->flags      = 0;
            out->as.boolean = src->as.boolean;
            return true;
        case dsdl_value_type:
            out->kind        = dsdl_value_type;
            out->flags       = 0;
            out->as.type_ref = src->as.type_ref;
            return true;
        case dsdl_value_string:
            if ((src->as.string.len > 0) && (src->as.string.str == NULL)) {
                return false;
            }
            return dsdl_value_set_string_copy(dsdl, src->as.string, out);
        case dsdl_value_set: {
            const size_t count = src->as.set.count;
            if ((count > 0) && (src->as.set.elements == NULL)) {
                return false;
            }
            dsdl_value_t* elements = (count > 0) ? (dsdl_value_t*)dsdl_alloc(dsdl, count * sizeof(dsdl_value_t)) : NULL;
            if ((elements == NULL) && (count > 0)) {
                return false;
            }
            for (size_t i = 0; i < count; i++) {
                if (!dsdl_value_clone(dsdl, &src->as.set.elements[i], &elements[i])) {
                    for (size_t j = 0; j < i; j++) {
                        dsdl_value_dispose(dsdl, &elements[j]);
                    }
                    dsdl_free(dsdl, elements);
                    return false;
                }
            }
            out->kind            = dsdl_value_set;
            out->flags           = DSDL_VALUE_FLAG_OWNED;
            out->as.set.count    = count;
            out->as.set.elements = elements;
            return true;
        }
        case dsdl_value_deferred: {
            if (src->as.deferred.clone == NULL) {
                return false;
            }
            dsdl_closure_t cloned = { 0 };
            if (!src->as.deferred.clone(&src->as.deferred, &cloned)) {
                return false;
            }
            out->kind        = dsdl_value_deferred;
            out->flags       = DSDL_VALUE_FLAG_OWNED;
            out->as.deferred = cloned;
            return true;
        }
    }
    return false;
}

static void dsdl_value_dispose(dsdl_t* const dsdl, dsdl_value_t* const value)
{
    if ((dsdl == NULL) || (value == NULL)) {
        return;
    }
    switch (value->kind) {
        case dsdl_value_string:
            if ((value->flags & DSDL_VALUE_FLAG_OWNED) != 0U) {
                dsdl_free_str(dsdl, value->as.string.str);
            }
            break;
        case dsdl_value_set:
            if ((value->flags & DSDL_VALUE_FLAG_OWNED) != 0U) {
                for (size_t i = 0; i < value->as.set.count; i++) {
                    dsdl_value_dispose(dsdl, &value->as.set.elements[i]);
                }
                dsdl_free(dsdl, value->as.set.elements);
            }
            break;
        case dsdl_value_deferred:
            if ((value->flags & DSDL_VALUE_FLAG_OWNED) != 0U) {
                if (value->as.deferred.cleanup != NULL) {
                    value->as.deferred.cleanup(&value->as.deferred);
                } else if (value->as.deferred.context != NULL) {
                    dsdl_free(dsdl, value->as.deferred.context);
                }
            }
            break;
        case dsdl_value_rational:
        case dsdl_value_bool:
        case dsdl_value_type:
        default:
            break;
    }
    (void)memset(value, 0, sizeof(*value));
}

// ============================================================================
// Parser foundation
// ============================================================================

/// Initialize parser state.
static void dsdl_parser_init(dsdl_parser_t* const       parser,
                             dsdl_t* const              dsdl,
                             const char* const          input,
                             const size_t               len,
                             dsdl_eval_context_t* const eval_ctx)
{
    assert((parser != NULL) && (dsdl != NULL));
    assert((input != NULL) || (len == 0));
    parser->input    = input;
    parser->len      = len;
    parser->pos      = 0;
    parser->line     = 1;
    parser->col      = 1;
    parser->dsdl     = dsdl;
    parser->eval_ctx = eval_ctx;
}

/// Check if parser has reached end of input.
static bool dsdl_parser_eof(const dsdl_parser_t* const parser)
{
    assert(parser != NULL);
    return parser->pos >= parser->len;
}

/// Peek at character at current position + offset. Returns 0 if out of bounds.
static char dsdl_parser_peek(const dsdl_parser_t* const parser, const size_t offset)
{
    assert((parser != NULL) && (parser->input != NULL || parser->len == 0));
    const size_t idx = parser->pos + offset;
    if (idx >= parser->len) {
        return '\0';
    }
    return parser->input[idx];
}

/// Advance parser by count characters, updating line/col tracking.
static void dsdl_parser_advance(dsdl_parser_t* const parser, const size_t count)
{
    assert((parser != NULL) && (parser->pos <= parser->len));
    for (size_t i = 0; i < count && parser->pos < parser->len; ++i) {
        const char c = parser->input[parser->pos];
        if (c == '\n') {
            parser->line++;
            parser->col = 1;
        } else {
            parser->col++;
        }
        parser->pos++;
    }
}

/// Check if the string at current position matches the given string.
/// Does NOT advance the parser.
static bool dsdl_parser_match(const dsdl_parser_t* const parser, const char* const str, const size_t str_len)
{
    assert((parser != NULL) && ((str != NULL) || (str_len == 0)));
    if ((parser->pos + str_len) > parser->len) {
        return false;
    }
    return memcmp(&parser->input[parser->pos], str, str_len) == 0;
}

/// Check if the string matches and advance if so.
static bool dsdl_parser_accept(dsdl_parser_t* const parser, const char* const str, const size_t str_len)
{
    assert((parser != NULL) && ((str != NULL) || (str_len == 0)));
    if (dsdl_parser_match(parser, str, str_len)) {
        dsdl_parser_advance(parser, str_len);
        return true;
    }
    return false;
}

/// Skip whitespace (space and tab only, not newlines).
static void dsdl_parser_skip_ws(dsdl_parser_t* const parser)
{
    while (!dsdl_parser_eof(parser)) {
        const char c = dsdl_parser_peek(parser, 0);
        if ((c == ' ') || (c == '\t')) {
            dsdl_parser_advance(parser, 1);
        } else {
            break;
        }
    }
}

/// Skip a comment (# to end of line). Returns true if a comment was skipped.
static bool dsdl_parser_skip_comment(dsdl_parser_t* const parser)
{
    if (dsdl_parser_peek(parser, 0) != '#') {
        return false;
    }
    // Consume everything until end of line (but not the newline itself)
    while (!dsdl_parser_eof(parser)) {
        const char c = dsdl_parser_peek(parser, 0);
        if ((c == '\r') || (c == '\n')) {
            break;
        }
        dsdl_parser_advance(parser, 1);
    }
    return true;
}

/// Skip optional whitespace and comment at end of line.
static void dsdl_parser_skip_line_tail(dsdl_parser_t* const parser)
{
    dsdl_parser_skip_ws(parser);
    (void)dsdl_parser_skip_comment(parser);
}

/// Skip end of line sequence (\n or \r\n). Returns true if skipped.
static bool dsdl_parser_skip_eol(dsdl_parser_t* const parser)
{
    if (dsdl_parser_peek(parser, 0) == '\r' && dsdl_parser_peek(parser, 1) == '\n') {
        dsdl_parser_advance(parser, 2);
        return true;
    }
    if (dsdl_parser_peek(parser, 0) == '\n') {
        dsdl_parser_advance(parser, 1);
        return true;
    }
    return false;
}

/// Check if character is a digit (0-9).
static bool dsdl_is_digit(const char c) { return (c >= '0') && (c <= '9'); }

/// Check if character is a hex digit (0-9, a-f, A-F).
static bool dsdl_is_hex_digit(const char c)
{
    return dsdl_is_digit(c) || ((c >= 'a') && (c <= 'f')) || ((c >= 'A') && (c <= 'F'));
}

/// Check if character is an octal digit (0-7).
static bool dsdl_is_octal_digit(const char c) { return (c >= '0') && (c <= '7'); }

/// Check if character is a binary digit (0-1).
static bool dsdl_is_binary_digit(const char c) { return (c == '0') || (c == '1'); }

/// Check if character is an identifier start character.
static bool dsdl_is_ident_start(const char c)
{
    return ((c >= 'a') && (c <= 'z')) || ((c >= 'A') && (c <= 'Z')) || (c == '_');
}

/// Check if character is an identifier continuation character.
static bool dsdl_is_ident_cont(const char c) { return dsdl_is_ident_start(c) || dsdl_is_digit(c); }

/// Convert hex digit to value (0-15).
static int dsdl_hex_value(const char c)
{
    if (dsdl_is_digit(c)) {
        return c - '0';
    }
    if ((c >= 'a') && (c <= 'f')) {
        return 10 + (c - 'a');
    }
    if ((c >= 'A') && (c <= 'F')) {
        return 10 + (c - 'A');
    }
    return -1;
}

// ============================================================================
// Literal parsing
// ============================================================================

/// Parse a binary integer literal: 0b[01_]+
/// Returns rational with den=1 on success, den=0 on failure.
static dsdl_rational_t dsdl_parse_int_binary(dsdl_parser_t* const parser)
{
    dsdl_rational_t result = dsdl_rational_nan(); // NaN = failure

    // Check for 0b or 0B prefix
    if (!((dsdl_parser_peek(parser, 0) == '0') &&
          ((dsdl_parser_peek(parser, 1) == 'b') || (dsdl_parser_peek(parser, 1) == 'B')))) {
        return result;
    }
    dsdl_parser_advance(parser, 2);

    // Must have at least one digit
    bool          has_digit = false;
    dsdl_bigint_t value;
    dsdl_bigint_zero(&value);

    while (!dsdl_parser_eof(parser)) {
        const char c = dsdl_parser_peek(parser, 0);
        if (c == '_') {
            // Skip underscores (digit separator)
            dsdl_parser_advance(parser, 1);
            continue;
        }
        if (dsdl_is_binary_digit(c)) {
            has_digit = true;
            if (!dsdl_bigint_mul_add_small_inplace(&value, 2U, (uint32_t)(c - '0'))) {
                return result;
            }
            dsdl_parser_advance(parser, 1);
        } else {
            break;
        }
    }

    if (has_digit) {
        result.num = value;
        if (!dsdl_bigint_from_uintmax(&result.den, 1U)) {
            return dsdl_rational_nan();
        }
    }
    return result;
}

/// Parse an octal integer literal: 0o[0-7_]+
static dsdl_rational_t dsdl_parse_int_octal(dsdl_parser_t* const parser)
{
    dsdl_rational_t result = dsdl_rational_nan(); // NaN = failure

    // Check for 0o or 0O prefix
    if (!((dsdl_parser_peek(parser, 0) == '0') &&
          ((dsdl_parser_peek(parser, 1) == 'o') || (dsdl_parser_peek(parser, 1) == 'O')))) {
        return result;
    }
    dsdl_parser_advance(parser, 2);

    bool          has_digit = false;
    dsdl_bigint_t value;
    dsdl_bigint_zero(&value);

    while (!dsdl_parser_eof(parser)) {
        const char c = dsdl_parser_peek(parser, 0);
        if (c == '_') {
            dsdl_parser_advance(parser, 1);
            continue;
        }
        if (dsdl_is_octal_digit(c)) {
            has_digit = true;
            if (!dsdl_bigint_mul_add_small_inplace(&value, 8U, (uint32_t)(c - '0'))) {
                return result;
            }
            dsdl_parser_advance(parser, 1);
        } else {
            break;
        }
    }

    if (has_digit) {
        result.num = value;
        if (!dsdl_bigint_from_uintmax(&result.den, 1U)) {
            return dsdl_rational_nan();
        }
    }
    return result;
}

/// Parse a hexadecimal integer literal: 0x[0-9a-fA-F_]+
static dsdl_rational_t dsdl_parse_int_hex(dsdl_parser_t* const parser)
{
    dsdl_rational_t result = dsdl_rational_nan(); // NaN = failure

    // Check for 0x or 0X prefix
    if (!((dsdl_parser_peek(parser, 0) == '0') &&
          ((dsdl_parser_peek(parser, 1) == 'x') || (dsdl_parser_peek(parser, 1) == 'X')))) {
        return result;
    }
    dsdl_parser_advance(parser, 2);

    bool          has_digit = false;
    dsdl_bigint_t value;
    dsdl_bigint_zero(&value);

    while (!dsdl_parser_eof(parser)) {
        const char c = dsdl_parser_peek(parser, 0);
        if (c == '_') {
            dsdl_parser_advance(parser, 1);
            continue;
        }
        if (dsdl_is_hex_digit(c)) {
            has_digit = true;
            if (!dsdl_bigint_mul_add_small_inplace(&value, 16U, (uint32_t)dsdl_hex_value(c))) {
                return result;
            }
            dsdl_parser_advance(parser, 1);
        } else {
            break;
        }
    }

    if (has_digit) {
        result.num = value;
        if (!dsdl_bigint_from_uintmax(&result.den, 1U)) {
            return dsdl_rational_nan();
        }
    }
    return result;
}

/// Parse a decimal integer literal: [0-9_]+
/// Returns rational with den=1 on success, den=0 on failure.
static dsdl_rational_t dsdl_parse_int_decimal(dsdl_parser_t* const parser)
{
    dsdl_rational_t result = dsdl_rational_nan(); // NaN = failure

    // Must start with a digit
    if (!dsdl_is_digit(dsdl_parser_peek(parser, 0))) {
        return result;
    }

    bool          has_digit = false;
    dsdl_bigint_t value;
    dsdl_bigint_zero(&value);

    while (!dsdl_parser_eof(parser)) {
        const char c = dsdl_parser_peek(parser, 0);
        if (c == '_') {
            dsdl_parser_advance(parser, 1);
            continue;
        }
        if (dsdl_is_digit(c)) {
            has_digit = true;
            if (!dsdl_bigint_mul_add_small_inplace(&value, 10U, (uint32_t)(c - '0'))) {
                return result;
            }
            dsdl_parser_advance(parser, 1);
        } else {
            break;
        }
    }

    if (has_digit) {
        result.num = value;
        if (!dsdl_bigint_from_uintmax(&result.den, 1U)) {
            return dsdl_rational_nan();
        }
    }
    return result;
}

/// Parse any integer literal (binary, octal, hex, or decimal).
/// The order matters: we try more specific prefixes first.
static dsdl_rational_t dsdl_parse_integer(dsdl_parser_t* const parser)
{
    const size_t start_pos = parser->pos;

    // Try binary (0b...)
    dsdl_rational_t result = dsdl_parse_int_binary(parser);
    if (!dsdl_rational_is_nan(result)) {
        return result;
    }
    parser->pos = start_pos; // Backtrack

    // Try octal (0o...)
    result = dsdl_parse_int_octal(parser);
    if (!dsdl_rational_is_nan(result)) {
        return result;
    }
    parser->pos = start_pos;

    // Try hex (0x...)
    result = dsdl_parse_int_hex(parser);
    if (!dsdl_rational_is_nan(result)) {
        return result;
    }
    parser->pos = start_pos;

    // Try decimal
    return dsdl_parse_int_decimal(parser);
}

/// Parse digits for real number (returns count of digits parsed, 0 on failure).
static size_t dsdl_parse_real_digits(dsdl_parser_t* const parser, dsdl_bigint_t* const out_value)
{
    size_t count = 0;
    if (out_value != NULL) {
        dsdl_bigint_zero(out_value);
    }

    while (!dsdl_parser_eof(parser)) {
        const char c = dsdl_parser_peek(parser, 0);
        if (c == '_') {
            dsdl_parser_advance(parser, 1);
            continue;
        }
        if (dsdl_is_digit(c)) {
            count++;
            if (out_value != NULL) {
                if (!dsdl_bigint_mul_add_small_inplace(out_value, 10U, (uint32_t)(c - '0'))) {
                    return 0;
                }
            }
            dsdl_parser_advance(parser, 1);
        } else {
            break;
        }
    }

    return count;
}

/// Parse a real literal (point notation or exponent notation).
/// Returns rational on success, with den=0 on failure.
/// Note: For simplicity, we convert to double and then back to rational for fractional parts.
static dsdl_rational_t dsdl_parse_real(dsdl_parser_t* const parser)
{
    dsdl_rational_t result    = dsdl_rational_nan(); // NaN = failure
    const size_t    start_pos = parser->pos;

    // Components of the real number
    dsdl_bigint_t int_part;
    dsdl_bigint_t frac_part;
    size_t        frac_count = 0; // Number of fractional digits
    uint32_t      exp_value  = 0;
    bool          exp_neg    = false;
    bool          has_point  = false;
    bool          has_exp    = false;

    // Parse integer part (optional if there's a point)
    const size_t int_digits = dsdl_parse_real_digits(parser, &int_part);

    // Check for decimal point
    if (dsdl_parser_peek(parser, 0) == '.') {
        // Make sure the next character is a digit or end-of-token (not another identifier)
        const char next = dsdl_parser_peek(parser, 1);
        if (dsdl_is_digit(next)) {
            has_point = true;
            dsdl_parser_advance(parser, 1); // Consume '.'

            // Parse fractional part
            frac_count = dsdl_parse_real_digits(parser, &frac_part);
        } else if (!dsdl_is_ident_start(next)) {
            // Just "123." is valid
            has_point = true;
            dsdl_parser_advance(parser, 1);
        }
    }

    // Check for exponent
    const char exp_char = dsdl_parser_peek(parser, 0);
    if ((exp_char == 'e') || (exp_char == 'E')) {
        has_exp = true;
        dsdl_parser_advance(parser, 1);

        // Optional sign
        const char sign = dsdl_parser_peek(parser, 0);
        if (sign == '+') {
            dsdl_parser_advance(parser, 1);
        } else if (sign == '-') {
            exp_neg = true;
            dsdl_parser_advance(parser, 1);
        }

        // Exponent digits (required)
        bool exp_has_digit = false;
        while (!dsdl_parser_eof(parser)) {
            const char c = dsdl_parser_peek(parser, 0);
            if (c == '_') {
                dsdl_parser_advance(parser, 1);
                continue;
            }
            if (dsdl_is_digit(c)) {
                exp_has_digit        = true;
                const uint32_t digit = (uint32_t)(c - '0');
                if (exp_value > (UINT32_MAX - digit) / 10U) {
                    parser->pos = start_pos;
                    return result;
                }
                exp_value = (exp_value * 10U) + digit;
                dsdl_parser_advance(parser, 1);
            } else {
                break;
            }
        }
        if (!exp_has_digit) {
            // Invalid: e without digits
            parser->pos = start_pos;
            return result;
        }
    }

    // A real literal must have either a decimal point or an exponent
    if (!has_point && !has_exp) {
        parser->pos = start_pos;
        return result;
    }

    // Also need at least some digits
    if ((int_digits == 0) && (frac_count == 0)) {
        parser->pos = start_pos;
        return result;
    }

    // Build the rational.
    // Value = (int_part + frac_part / 10^frac_count) * 10^(±exp_value)
    // To keep it as a rational: num = int_part * 10^frac_count + frac_part
    //                          den = 10^frac_count
    // Then multiply/divide by 10^exp as needed.

    dsdl_bigint_t num = int_part;
    dsdl_bigint_t den;
    if (frac_count > 0U) {
        dsdl_bigint_t scaled_int = int_part;
        if (!dsdl_bigint_mul_pow10_inplace(&scaled_int, (uint32_t)frac_count)) {
            parser->pos = start_pos;
            return result;
        }
        dsdl_bigint_t sum;
        if (!dsdl_bigint_add_abs(&scaled_int, &frac_part, &sum)) {
            parser->pos = start_pos;
            return result;
        }
        num = sum;
        if (!dsdl_bigint_pow10((uint32_t)frac_count, &den)) {
            parser->pos = start_pos;
            return result;
        }
    } else {
        if (!dsdl_bigint_from_uintmax(&den, 1U)) {
            parser->pos = start_pos;
            return result;
        }
    }

    if (has_exp && (exp_value > 0U)) {
        const uint32_t exp_limit = 1000U;
        if (exp_value > exp_limit) {
            parser->pos = start_pos;
            return result;
        }
        dsdl_bigint_t pow10;
        if (!dsdl_bigint_pow10(exp_value, &pow10)) {
            parser->pos = start_pos;
            return result;
        }
        dsdl_bigint_t scaled;
        if (exp_neg) {
            if (!dsdl_bigint_mul_abs(&den, &pow10, &scaled)) {
                parser->pos = start_pos;
                return result;
            }
            den = scaled;
        } else {
            if (!dsdl_bigint_mul_abs(&num, &pow10, &scaled)) {
                parser->pos = start_pos;
                return result;
            }
            num = scaled;
        }
    }

    result.num          = num;
    result.den          = den;
    result.num.negative = false;
    return dsdl_rational_normalize(result);
}

/// Parse a numeric literal (integer or real).
/// Tries real first (more specific), then integer.
static dsdl_rational_t dsdl_parse_number(dsdl_parser_t* const parser)
{
    const size_t start_pos = parser->pos;

    // Try real first (point or exponent notation)
    dsdl_rational_t result = dsdl_parse_real(parser);
    if (!dsdl_rational_is_nan(result)) {
        return result;
    }
    parser->pos = start_pos;

    // Fall back to integer
    return dsdl_parse_integer(parser);
}

/// Parse an identifier and return it as a borrowed string.
/// Returns empty string on failure.
static wkv_str_t dsdl_parse_identifier(dsdl_parser_t* const parser)
{
    wkv_str_t result = { 0, NULL };

    if (!dsdl_is_ident_start(dsdl_parser_peek(parser, 0))) {
        return result;
    }

    const size_t start = parser->pos;
    dsdl_parser_advance(parser, 1);

    while (dsdl_is_ident_cont(dsdl_parser_peek(parser, 0))) {
        dsdl_parser_advance(parser, 1);
    }

    result.len = parser->pos - start;
    result.str = &parser->input[start];
    return result;
}

/// Parse a boolean literal (true/false).
/// Returns value in *out_value, returns true on success.
static bool dsdl_parse_boolean(dsdl_parser_t* const parser, bool* const out_value)
{
    if (dsdl_parser_accept(parser, "true", 4)) {
        // Make sure it's not followed by identifier characters (e.g., "trueX")
        if (!dsdl_is_ident_cont(dsdl_parser_peek(parser, 0))) {
            *out_value = true;
            return true;
        }
        // Backtrack
        parser->pos -= 4;
    } else if (dsdl_parser_accept(parser, "false", 5)) {
        if (!dsdl_is_ident_cont(dsdl_parser_peek(parser, 0))) {
            *out_value = false;
            return true;
        }
        parser->pos -= 5;
    }
    return false;
}

/// Parse a string literal (single or double quoted).
/// Returns the string content (without quotes, escapes NOT processed).
/// Returns empty string on failure.
static wkv_str_t dsdl_parse_string(dsdl_parser_t* const parser)
{
    wkv_str_t  result    = { 0, NULL };
    const char quote     = dsdl_parser_peek(parser, 0);
    const bool is_single = (quote == '\'');
    const bool is_double = (quote == '"');

    if (!is_single && !is_double) {
        return result;
    }

    dsdl_parser_advance(parser, 1); // Consume opening quote
    const size_t start = parser->pos;

    while (!dsdl_parser_eof(parser)) {
        const char c = dsdl_parser_peek(parser, 0);

        if (c == quote) {
            // End of string
            result.len = parser->pos - start;
            result.str = &parser->input[start];
            dsdl_parser_advance(parser, 1); // Consume closing quote
            return result;
        }

        if ((c == '\r') || (c == '\n')) {
            // Unterminated string (newline in string)
            break;
        }

        if (c == '\\') {
            // Escape sequence - skip both backslash and next character
            dsdl_parser_advance(parser, 2);
        } else {
            dsdl_parser_advance(parser, 1);
        }
    }

    // Unterminated string - return failure
    return result;
}

static int dsdl_hex_to_nibble(const char c)
{
    if ((c >= '0') && (c <= '9')) {
        return (int)(c - '0');
    }
    if ((c >= 'a') && (c <= 'f')) {
        return (int)(c - 'a') + 10;
    }
    if ((c >= 'A') && (c <= 'F')) {
        return (int)(c - 'A') + 10;
    }
    return -1;
}

static bool dsdl_utf8_encode(const uint32_t code_point, char* const out, size_t* const out_len)
{
    if ((out == NULL) || (out_len == NULL)) {
        return false;
    }
    if (code_point <= 0x7FU) {
        out[0]   = (char)code_point;
        *out_len = 1;
        return true;
    }
    if (code_point <= 0x7FFU) {
        out[0]   = (char)(0xC0U | (code_point >> 6U));
        out[1]   = (char)(0x80U | (code_point & 0x3FU));
        *out_len = 2;
        return true;
    }
    if (code_point <= 0xFFFFU) {
        if ((code_point >= 0xD800U) && (code_point <= 0xDFFFU)) {
            return false;
        }
        out[0]   = (char)(0xE0U | (code_point >> 12U));
        out[1]   = (char)(0x80U | ((code_point >> 6U) & 0x3FU));
        out[2]   = (char)(0x80U | (code_point & 0x3FU));
        *out_len = 3;
        return true;
    }
    if (code_point <= 0x10FFFFU) {
        out[0]   = (char)(0xF0U | (code_point >> 18U));
        out[1]   = (char)(0x80U | ((code_point >> 12U) & 0x3FU));
        out[2]   = (char)(0x80U | ((code_point >> 6U) & 0x3FU));
        out[3]   = (char)(0x80U | (code_point & 0x3FU));
        *out_len = 4;
        return true;
    }
    return false;
}

static bool dsdl_unescape_string(dsdl_t* const dsdl, const wkv_str_t raw, wkv_str_t* const out)
{
    if ((dsdl == NULL) || (out == NULL)) {
        return false;
    }
    if ((raw.len > 0) && (raw.str == NULL)) {
        return false;
    }

    char* buf = NULL;
    if (raw.len > 0) {
        buf = (char*)dsdl_alloc(dsdl, raw.len);
        if (buf == NULL) {
            return false;
        }
    }

    size_t out_len = 0;
    for (size_t i = 0; i < raw.len; i++) {
        const char c = raw.str[i];
        if (c != '\\') {
            buf[out_len++] = c;
            continue;
        }
        if ((i + 1) >= raw.len) {
            dsdl_free(dsdl, buf);
            return false;
        }
        const char esc = raw.str[i + 1];
        if (esc == '\\') {
            buf[out_len++] = '\\';
            i++;
            continue;
        }
        if (esc == 'r') {
            buf[out_len++] = '\r';
            i++;
            continue;
        }
        if (esc == 'n') {
            buf[out_len++] = '\n';
            i++;
            continue;
        }
        if (esc == 't') {
            buf[out_len++] = '\t';
            i++;
            continue;
        }
        if (esc == '\'') {
            buf[out_len++] = '\'';
            i++;
            continue;
        }
        if (esc == '"') {
            buf[out_len++] = '"';
            i++;
            continue;
        }
        if ((esc == 'u') || (esc == 'U')) {
            const size_t digits = (esc == 'u') ? 4U : 8U;
            if ((i + 1U + digits) >= raw.len) {
                dsdl_free(dsdl, buf);
                return false;
            }
            uint32_t code_point = 0;
            for (size_t d = 0; d < digits; d++) {
                const int nibble = dsdl_hex_to_nibble(raw.str[i + 2 + d]);
                if (nibble < 0) {
                    dsdl_free(dsdl, buf);
                    return false;
                }
                code_point = (code_point << 4U) | (uint32_t)nibble;
            }
            char   encoded[4];
            size_t encoded_len = 0;
            if (!dsdl_utf8_encode(code_point, encoded, &encoded_len)) {
                dsdl_free(dsdl, buf);
                return false;
            }
            if ((out_len + encoded_len) > raw.len) {
                dsdl_free(dsdl, buf);
                return false;
            }
            for (size_t k = 0; k < encoded_len; k++) {
                buf[out_len++] = encoded[k];
            }
            i += 1U + digits;
            continue;
        }
        dsdl_free(dsdl, buf);
        return false;
    }

    out->len = out_len;
    out->str = buf;
    return true;
}

// Forward declaration for recursive expression parsing
static bool dsdl_parse_expression(dsdl_parser_t* parser, dsdl_value_t* out_value);

/// Parse a set literal: { expr, expr, ... }
/// Returns true on success, fills out_value with dsdl_value_set.
/// Uses dynamic allocation for arbitrary-sized sets.
static bool dsdl_parse_set(dsdl_parser_t* const parser, dsdl_value_t* const out_value)
{
    if (dsdl_parser_peek(parser, 0) != '{') {
        return false;
    }
    const size_t start_pos  = parser->pos;
    const size_t start_line = parser->line;
    const size_t start_col  = parser->col;
    dsdl_parser_advance(parser, 1); // Consume '{'
    dsdl_parser_skip_ws(parser);

    // Initialize as empty set
    out_value->kind            = dsdl_value_set;
    out_value->flags           = DSDL_VALUE_FLAG_OWNED;
    out_value->as.set.count    = 0;
    out_value->as.set.elements = NULL;

    // Check for empty set
    if (dsdl_parser_peek(parser, 0) == '}') {
        parser->pos  = start_pos;
        parser->line = start_line;
        parser->col  = start_col;
        return false; // Empty sets are not permitted
    }

    // Dynamic allocation with growth strategy
    size_t        capacity = 8; // Initial capacity
    size_t        count    = 0;
    dsdl_value_t* elements = (dsdl_value_t*)dsdl_alloc(parser->dsdl, capacity * sizeof(dsdl_value_t));
    if (elements == NULL) {
        return false; // OOM
    }

    for (;;) {
        // Parse expression
        dsdl_value_t elem;
        if (!dsdl_parse_expression(parser, &elem)) {
            for (size_t i = 0; i < count; i++) {
                dsdl_value_dispose(parser->dsdl, &elements[i]);
            }
            dsdl_free(parser->dsdl, elements);
            return false; // Parse error
        }

        // Grow buffer if needed
        if (count >= capacity) {
            const size_t        new_capacity = capacity * 2;
            dsdl_value_t* const new_elements =
              (dsdl_value_t*)dsdl_realloc(parser->dsdl, elements, new_capacity * sizeof(dsdl_value_t));
            if (new_elements == NULL) {
                for (size_t i = 0; i < count; i++) {
                    dsdl_value_dispose(parser->dsdl, &elements[i]);
                }
                dsdl_value_dispose(parser->dsdl, &elem);
                dsdl_free(parser->dsdl, elements);
                return false; // OOM
            }
            elements = new_elements;
            capacity = new_capacity;
        }

        elements[count++] = elem;
        dsdl_parser_skip_ws(parser);

        // Check for comma or closing brace
        if (dsdl_parser_peek(parser, 0) == ',') {
            dsdl_parser_advance(parser, 1);
            dsdl_parser_skip_ws(parser);
        } else if (dsdl_parser_peek(parser, 0) == '}') {
            break;
        } else {
            for (size_t i = 0; i < count; i++) {
                dsdl_value_dispose(parser->dsdl, &elements[i]);
            }
            dsdl_free(parser->dsdl, elements);
            return false; // Unexpected character
        }
    }

    // Consume closing brace
    if (dsdl_parser_peek(parser, 0) != '}') {
        for (size_t i = 0; i < count; i++) {
            dsdl_value_dispose(parser->dsdl, &elements[i]);
        }
        dsdl_free(parser->dsdl, elements);
        return false;
    }
    dsdl_parser_advance(parser, 1);

    // Shrink to fit if significantly oversized (optional optimization)
    if ((count > 0) && (count < capacity / 2) && (capacity > 8)) {
        dsdl_value_t* const shrunk = (dsdl_value_t*)dsdl_realloc(parser->dsdl, elements, count * sizeof(dsdl_value_t));
        if (shrunk != NULL) {
            elements = shrunk;
        }
        // If shrink fails, keep the larger buffer - not a critical error
    }

    out_value->as.set.elements = elements;
    out_value->as.set.count    = count;

    return true;
}

/// Parse a literal (number, string, boolean, or set).
static bool dsdl_parse_literal(dsdl_parser_t* const parser, dsdl_value_t* const out_value)
{
    // Try set first (starts with '{')
    if (dsdl_parser_peek(parser, 0) == '{') {
        return dsdl_parse_set(parser, out_value);
    }

    // Try string (starts with quote)
    const char c = dsdl_parser_peek(parser, 0);
    if ((c == '"') || (c == '\'')) {
        wkv_str_t raw = dsdl_parse_string(parser);
        if (raw.str == NULL) {
            return false;
        }
        wkv_str_t unescaped = { 0, NULL };
        if (!dsdl_unescape_string(parser->dsdl, raw, &unescaped)) {
            return false;
        }
        out_value->kind      = dsdl_value_string;
        out_value->flags     = DSDL_VALUE_FLAG_OWNED;
        out_value->as.string = unescaped;
        return true;
    }

    // Try boolean
    bool bool_val;
    if (dsdl_parse_boolean(parser, &bool_val)) {
        out_value->kind       = dsdl_value_bool;
        out_value->flags      = 0;
        out_value->as.boolean = bool_val;
        return true;
    }

    // Try number (integer or real)
    dsdl_rational_t num = dsdl_parse_number(parser);
    if (!dsdl_rational_is_nan(num)) {
        out_value->kind        = dsdl_value_rational;
        out_value->flags       = 0;
        out_value->as.rational = num;
        return true;
    }

    return false;
}

// ============================================================================
// Parsed type representation
// ============================================================================

/// Parsed type representation during parsing.
/// This is an intermediate representation before converting to dsdl_type_t.
typedef struct dsdl_parsed_type_t
{
    dsdl_type_t   kind;                ///< DSDL_xxx type kind constant (DSDL_ARRAY_* for arrays)
    dsdl_type_t   element_kind;        ///< For arrays: the element type kind (primitive or composite marker)
    uint_least8_t bit_width;           ///< Bit width for primitives/void
    bool          is_inclusive;        ///< For variable arrays: inclusive vs exclusive
    uint64_t      array_size;          ///< Array capacity (max size for variable, fixed size for fixed)
    bool          has_array_size_expr; ///< True if array_size_expr holds a deferred expression
    dsdl_value_t  array_size_expr;     ///< Array capacity expression (may be deferred)
    wkv_str_t     type_name;           ///< For composite types: full type name
    uint_least8_t version_major;       ///< For versioned types
    uint_least8_t version_minor;       ///< For versioned types
} dsdl_parsed_type_t;

// ============================================================================
// Expression parsing (Pratt parser / precedence climbing)
// ============================================================================

/// Operator precedence levels (higher = binds tighter)
typedef enum
{
    dsdl_prec_none       = 0,
    dsdl_prec_logical    = 1, // || &&
    dsdl_prec_comparison = 2, // == != < <= > >=
    dsdl_prec_bitwise    = 3, // | ^ &
    dsdl_prec_additive   = 4, // + -
    dsdl_prec_mult       = 5, // * / %
    dsdl_prec_exp        = 6, // **
    dsdl_prec_attribute  = 7, // .
} dsdl_prec_t;

/// Binary operator types
typedef enum
{
    dsdl_op_none,
    // Logical
    dsdl_op_or,
    dsdl_op_and,
    // Comparison
    dsdl_op_eq,
    dsdl_op_ne,
    dsdl_op_lt,
    dsdl_op_le,
    dsdl_op_gt,
    dsdl_op_ge,
    // Bitwise
    dsdl_op_bit_or,
    dsdl_op_bit_xor,
    dsdl_op_bit_and,
    // Additive
    dsdl_op_add,
    dsdl_op_sub,
    // Multiplicative
    dsdl_op_mul,
    dsdl_op_div,
    dsdl_op_mod,
    // Exponential
    dsdl_op_pow,
    // Attribute
    dsdl_op_dot,
} dsdl_op_t;

static bool dsdl_parse_type_versioned(dsdl_parser_t* const parser, dsdl_parsed_type_t* const out_type);

/// Attribute names supported for set-like values.
typedef enum
{
    dsdl_attr_min,
    dsdl_attr_max,
    dsdl_attr_count,
    dsdl_attr_bit_length,
    dsdl_attr_extent,
} dsdl_attr_kind_t;

/// Unary operator kinds.
typedef enum
{
    dsdl_unary_not,
    dsdl_unary_pos,
    dsdl_unary_neg,
} dsdl_unary_op_t;

// Forward declaration for _offset_ evaluation.
static bool                         dsdl_value_from_bls(dsdl_t* dsdl, dsdl_bls_t* bls, dsdl_value_t* out);
static dsdl_bls_t*                  dsdl_type_bls(dsdl_t* self, dsdl_type_t* type_ptr);
static const dsdl_type_composite_t* dsdl_resolve_composite_type(dsdl_t* const       self,
                                                                const wkv_str_t     type_name,
                                                                const uint_least8_t version_major,
                                                                const uint_least8_t version_minor,
                                                                const wkv_str_t     current_namespace);

/// Get precedence for a binary operator at current position.
/// Also returns the operator type and its length.
static dsdl_prec_t dsdl_get_binary_op(const dsdl_parser_t* const parser, dsdl_op_t* const out_op, size_t* const out_len)
{
    *out_op  = dsdl_op_none;
    *out_len = 0;

    const char c0 = dsdl_parser_peek(parser, 0);
    const char c1 = dsdl_parser_peek(parser, 1);

    // Two-character operators first
    if (c0 == '|' && c1 == '|') {
        *out_op  = dsdl_op_or;
        *out_len = 2;
        return dsdl_prec_logical;
    }
    if (c0 == '&' && c1 == '&') {
        *out_op  = dsdl_op_and;
        *out_len = 2;
        return dsdl_prec_logical;
    }
    if (c0 == '=' && c1 == '=') {
        *out_op  = dsdl_op_eq;
        *out_len = 2;
        return dsdl_prec_comparison;
    }
    if (c0 == '!' && c1 == '=') {
        *out_op  = dsdl_op_ne;
        *out_len = 2;
        return dsdl_prec_comparison;
    }
    if (c0 == '<' && c1 == '=') {
        *out_op  = dsdl_op_le;
        *out_len = 2;
        return dsdl_prec_comparison;
    }
    if (c0 == '>' && c1 == '=') {
        *out_op  = dsdl_op_ge;
        *out_len = 2;
        return dsdl_prec_comparison;
    }
    if (c0 == '*' && c1 == '*') {
        *out_op  = dsdl_op_pow;
        *out_len = 2;
        return dsdl_prec_exp;
    }

    // Single-character operators
    if (c0 == '<') {
        *out_op  = dsdl_op_lt;
        *out_len = 1;
        return dsdl_prec_comparison;
    }
    if (c0 == '>') {
        *out_op  = dsdl_op_gt;
        *out_len = 1;
        return dsdl_prec_comparison;
    }
    if (c0 == '|') {
        *out_op  = dsdl_op_bit_or;
        *out_len = 1;
        return dsdl_prec_bitwise;
    }
    if (c0 == '^') {
        *out_op  = dsdl_op_bit_xor;
        *out_len = 1;
        return dsdl_prec_bitwise;
    }
    if (c0 == '&') {
        *out_op  = dsdl_op_bit_and;
        *out_len = 1;
        return dsdl_prec_bitwise;
    }
    if (c0 == '+') {
        *out_op  = dsdl_op_add;
        *out_len = 1;
        return dsdl_prec_additive;
    }
    if (c0 == '-') {
        *out_op  = dsdl_op_sub;
        *out_len = 1;
        return dsdl_prec_additive;
    }
    if (c0 == '*') {
        *out_op  = dsdl_op_mul;
        *out_len = 1;
        return dsdl_prec_mult;
    }
    if (c0 == '/') {
        *out_op  = dsdl_op_div;
        *out_len = 1;
        return dsdl_prec_mult;
    }
    if (c0 == '%') {
        *out_op  = dsdl_op_mod;
        *out_len = 1;
        return dsdl_prec_mult;
    }
    if (c0 == '.') {
        *out_op  = dsdl_op_dot;
        *out_len = 1;
        return dsdl_prec_attribute;
    }

    return dsdl_prec_none;
}

static bool dsdl_attr_from_ident(const wkv_str_t attr, dsdl_attr_kind_t* const out_kind)
{
    if ((out_kind == NULL) || (attr.str == NULL)) {
        return false;
    }
    if ((attr.len == 3U) && (memcmp(attr.str, "min", 3) == 0)) {
        *out_kind = dsdl_attr_min;
        return true;
    }
    if ((attr.len == 3U) && (memcmp(attr.str, "max", 3) == 0)) {
        *out_kind = dsdl_attr_max;
        return true;
    }
    if ((attr.len == 5U) && (memcmp(attr.str, "count", 5) == 0)) {
        *out_kind = dsdl_attr_count;
        return true;
    }
    if ((attr.len == 12U) && (memcmp(attr.str, "_bit_length_", 12) == 0)) {
        *out_kind = dsdl_attr_bit_length;
        return true;
    }
    if ((attr.len == 8U) && (memcmp(attr.str, "_extent_", 8) == 0)) {
        *out_kind = dsdl_attr_extent;
        return true;
    }
    return false;
}

static bool dsdl_str_equal(const wkv_str_t a, const wkv_str_t b)
{
    if (a.len != b.len) {
        return false;
    }
    if (a.len == 0) {
        return true;
    }
    if ((a.str == NULL) || (b.str == NULL)) {
        return false;
    }
    return memcmp(a.str, b.str, a.len) == 0;
}

static bool dsdl_utf8_decode(const char* const str, const size_t len, size_t* const pos, uint32_t* const out)
{
    if ((str == NULL) || (pos == NULL) || (out == NULL) || (*pos >= len)) {
        return false;
    }
    const unsigned char c0 = (unsigned char)str[*pos];
    if (c0 < 0x80U) {
        *out = (uint32_t)c0;
        *pos += 1U;
        return true;
    }
    if ((c0 & 0xE0U) == 0xC0U) {
        if ((*pos + 1U) >= len) {
            return false;
        }
        const unsigned char c1 = (unsigned char)str[*pos + 1U];
        if ((c1 & 0xC0U) != 0x80U) {
            return false;
        }
        const uint32_t cp = ((uint32_t)(c0 & 0x1FU) << 6U) | (uint32_t)(c1 & 0x3FU);
        if (cp < 0x80U) {
            return false;
        }
        *out = cp;
        *pos += 2U;
        return true;
    }
    if ((c0 & 0xF0U) == 0xE0U) {
        if ((*pos + 2U) >= len) {
            return false;
        }
        const unsigned char c1 = (unsigned char)str[*pos + 1U];
        const unsigned char c2 = (unsigned char)str[*pos + 2U];
        if (((c1 & 0xC0U) != 0x80U) || ((c2 & 0xC0U) != 0x80U)) {
            return false;
        }
        const uint32_t cp = ((uint32_t)(c0 & 0x0FU) << 12U) | ((uint32_t)(c1 & 0x3FU) << 6U) | (uint32_t)(c2 & 0x3FU);
        if ((cp < 0x800U) || ((cp >= 0xD800U) && (cp <= 0xDFFFU))) {
            return false;
        }
        *out = cp;
        *pos += 3U;
        return true;
    }
    if ((c0 & 0xF8U) == 0xF0U) {
        if ((*pos + 3U) >= len) {
            return false;
        }
        const unsigned char c1 = (unsigned char)str[*pos + 1U];
        const unsigned char c2 = (unsigned char)str[*pos + 2U];
        const unsigned char c3 = (unsigned char)str[*pos + 3U];
        if (((c1 & 0xC0U) != 0x80U) || ((c2 & 0xC0U) != 0x80U) || ((c3 & 0xC0U) != 0x80U)) {
            return false;
        }
        const uint32_t cp = ((uint32_t)(c0 & 0x07U) << 18U) | ((uint32_t)(c1 & 0x3FU) << 12U) |
                            ((uint32_t)(c2 & 0x3FU) << 6U) | (uint32_t)(c3 & 0x3FU);
        if ((cp < 0x10000U) || (cp > 0x10FFFFU)) {
            return false;
        }
        *out = cp;
        *pos += 4U;
        return true;
    }
    return false;
}

static uint32_t dsdl_compose_acute(const uint32_t base)
{
    switch (base) {
        case 'A':
            return 0x00C1U;
        case 'E':
            return 0x00C9U;
        case 'I':
            return 0x00CDU;
        case 'O':
            return 0x00D3U;
        case 'U':
            return 0x00DAU;
        case 'Y':
            return 0x00DDU;
        case 'a':
            return 0x00E1U;
        case 'e':
            return 0x00E9U;
        case 'i':
            return 0x00EDU;
        case 'o':
            return 0x00F3U;
        case 'u':
            return 0x00FAU;
        case 'y':
            return 0x00FDU;
        default:
            return 0U;
    }
}

static bool dsdl_nfc_next(const char* const str, const size_t len, size_t* const pos, uint32_t* const out)
{
    size_t   cur = *pos;
    uint32_t cp  = 0U;
    if (!dsdl_utf8_decode(str, len, &cur, &cp)) {
        return false;
    }
    if (cp != 0U) {
        size_t   lookahead = cur;
        uint32_t next      = 0U;
        if (dsdl_utf8_decode(str, len, &lookahead, &next) && (next == 0x0301U)) {
            const uint32_t composed = dsdl_compose_acute(cp);
            if (composed != 0U) {
                *pos = lookahead;
                *out = composed;
                return true;
            }
        }
    }
    *pos = cur;
    *out = cp;
    return true;
}

static bool dsdl_string_equal_nfc(const wkv_str_t a, const wkv_str_t b)
{
    if ((a.len == 0U) && (b.len == 0U)) {
        return true;
    }
    if ((a.str == NULL) || (b.str == NULL)) {
        return false;
    }
    size_t pos_a = 0U;
    size_t pos_b = 0U;
    while ((pos_a < a.len) || (pos_b < b.len)) {
        uint32_t cp_a = 0U;
        uint32_t cp_b = 0U;
        if (!dsdl_nfc_next(a.str, a.len, &pos_a, &cp_a) || !dsdl_nfc_next(b.str, b.len, &pos_b, &cp_b)) {
            return false;
        }
        if (cp_a != cp_b) {
            return false;
        }
    }
    return true;
}

static bool dsdl_value_equal(const dsdl_value_t* const left, const dsdl_value_t* const right)
{
    if ((left == NULL) || (right == NULL) || (left->kind != right->kind)) {
        return false;
    }
    switch (left->kind) {
        case dsdl_value_rational:
            return dsdl_rational_cmp(left->as.rational, right->as.rational) == 0;
        case dsdl_value_string:
            return dsdl_string_equal_nfc(left->as.string, right->as.string);
        case dsdl_value_bool:
            return left->as.boolean == right->as.boolean;
        case dsdl_value_type:
            return left->as.type_ref == right->as.type_ref;
        case dsdl_value_set:
        case dsdl_value_deferred:
            return false;
    }
    return false;
}

static bool dsdl_set_is_homogeneous(const dsdl_value_t* const set_val, dsdl_value_kind_t* const out_kind)
{
    if ((set_val == NULL) || (set_val->kind != dsdl_value_set)) {
        return false;
    }
    if (set_val->as.set.count == 0) {
        if (out_kind != NULL) {
            *out_kind = dsdl_value_rational;
        }
        return true;
    }
    const dsdl_value_kind_t kind = set_val->as.set.elements[0].kind;
    for (size_t i = 0; i < set_val->as.set.count; i++) {
        if (set_val->as.set.elements[i].kind != kind) {
            return false;
        }
        if (kind == dsdl_value_deferred) {
            return false;
        }
    }
    if (out_kind != NULL) {
        *out_kind = kind;
    }
    return true;
}

static bool dsdl_set_contains(const dsdl_value_t* const set_val, const dsdl_value_t* const needle)
{
    if ((set_val == NULL) || (needle == NULL) || (set_val->kind != dsdl_value_set)) {
        return false;
    }
    for (size_t i = 0; i < set_val->as.set.count; i++) {
        if (dsdl_value_equal(&set_val->as.set.elements[i], needle)) {
            return true;
        }
    }
    return false;
}

static bool dsdl_set_is_subset(const dsdl_value_t* const left, const dsdl_value_t* const right)
{
    if ((left == NULL) || (right == NULL) || (left->kind != dsdl_value_set) || (right->kind != dsdl_value_set)) {
        return false;
    }
    for (size_t i = 0; i < left->as.set.count; i++) {
        if (!dsdl_set_contains(right, &left->as.set.elements[i])) {
            return false;
        }
    }
    return true;
}

static bool dsdl_set_union(dsdl_t* const             dsdl,
                           const dsdl_value_t* const left,
                           const dsdl_value_t* const right,
                           dsdl_value_t* const       result)
{
    dsdl_value_kind_t left_kind  = dsdl_value_rational;
    dsdl_value_kind_t right_kind = dsdl_value_rational;
    if (!dsdl_set_is_homogeneous(left, &left_kind) || !dsdl_set_is_homogeneous(right, &right_kind) ||
        (left_kind != right_kind)) {
        return false;
    }
    if ((left->as.set.count == 0) && (right->as.set.count == 0)) {
        result->kind            = dsdl_value_set;
        result->flags           = DSDL_VALUE_FLAG_OWNED;
        result->as.set.count    = 0;
        result->as.set.elements = NULL;
        return true;
    }

    const size_t        capacity = left->as.set.count + right->as.set.count;
    dsdl_value_t* const elements =
      (capacity > 0) ? (dsdl_value_t*)dsdl_alloc(dsdl, capacity * sizeof(dsdl_value_t)) : NULL;
    if ((capacity > 0) && (elements == NULL)) {
        return false;
    }

    size_t count = 0;
    for (size_t i = 0; i < left->as.set.count; i++) {
        if (!dsdl_value_clone(dsdl, &left->as.set.elements[i], &elements[count])) {
            for (size_t j = 0; j < count; j++) {
                dsdl_value_dispose(dsdl, &elements[j]);
            }
            dsdl_free(dsdl, elements);
            return false;
        }
        count++;
    }
    for (size_t i = 0; i < right->as.set.count; i++) {
        if (!dsdl_set_contains(left, &right->as.set.elements[i])) {
            if (!dsdl_value_clone(dsdl, &right->as.set.elements[i], &elements[count])) {
                for (size_t j = 0; j < count; j++) {
                    dsdl_value_dispose(dsdl, &elements[j]);
                }
                dsdl_free(dsdl, elements);
                return false;
            }
            count++;
        }
    }

    result->kind            = dsdl_value_set;
    result->flags           = DSDL_VALUE_FLAG_OWNED;
    result->as.set.count    = count;
    result->as.set.elements = elements;
    return true;
}

static bool dsdl_set_intersection(dsdl_t* const             dsdl,
                                  const dsdl_value_t* const left,
                                  const dsdl_value_t* const right,
                                  dsdl_value_t* const       result)
{
    dsdl_value_kind_t left_kind  = dsdl_value_rational;
    dsdl_value_kind_t right_kind = dsdl_value_rational;
    if (!dsdl_set_is_homogeneous(left, &left_kind) || !dsdl_set_is_homogeneous(right, &right_kind) ||
        (left_kind != right_kind)) {
        return false;
    }
    const size_t capacity = (left->as.set.count < right->as.set.count) ? left->as.set.count : right->as.set.count;
    dsdl_value_t* const elements =
      (capacity > 0) ? (dsdl_value_t*)dsdl_alloc(dsdl, capacity * sizeof(dsdl_value_t)) : NULL;
    if ((capacity > 0) && (elements == NULL)) {
        return false;
    }
    size_t count = 0;
    for (size_t i = 0; i < left->as.set.count; i++) {
        if (dsdl_set_contains(right, &left->as.set.elements[i])) {
            if (!dsdl_value_clone(dsdl, &left->as.set.elements[i], &elements[count])) {
                for (size_t j = 0; j < count; j++) {
                    dsdl_value_dispose(dsdl, &elements[j]);
                }
                dsdl_free(dsdl, elements);
                return false;
            }
            count++;
        }
    }
    result->kind            = dsdl_value_set;
    result->flags           = DSDL_VALUE_FLAG_OWNED;
    result->as.set.count    = count;
    result->as.set.elements = elements;
    return true;
}

static bool dsdl_set_symdiff(dsdl_t* const             dsdl,
                             const dsdl_value_t* const left,
                             const dsdl_value_t* const right,
                             dsdl_value_t* const       result)
{
    dsdl_value_kind_t left_kind  = dsdl_value_rational;
    dsdl_value_kind_t right_kind = dsdl_value_rational;
    if (!dsdl_set_is_homogeneous(left, &left_kind) || !dsdl_set_is_homogeneous(right, &right_kind) ||
        (left_kind != right_kind)) {
        return false;
    }
    const size_t        capacity = left->as.set.count + right->as.set.count;
    dsdl_value_t* const elements =
      (capacity > 0) ? (dsdl_value_t*)dsdl_alloc(dsdl, capacity * sizeof(dsdl_value_t)) : NULL;
    if ((capacity > 0) && (elements == NULL)) {
        return false;
    }
    size_t count = 0;
    for (size_t i = 0; i < left->as.set.count; i++) {
        if (!dsdl_set_contains(right, &left->as.set.elements[i])) {
            if (!dsdl_value_clone(dsdl, &left->as.set.elements[i], &elements[count])) {
                for (size_t j = 0; j < count; j++) {
                    dsdl_value_dispose(dsdl, &elements[j]);
                }
                dsdl_free(dsdl, elements);
                return false;
            }
            count++;
        }
    }
    for (size_t i = 0; i < right->as.set.count; i++) {
        if (!dsdl_set_contains(left, &right->as.set.elements[i])) {
            if (!dsdl_value_clone(dsdl, &right->as.set.elements[i], &elements[count])) {
                for (size_t j = 0; j < count; j++) {
                    dsdl_value_dispose(dsdl, &elements[j]);
                }
                dsdl_free(dsdl, elements);
                return false;
            }
            count++;
        }
    }
    result->kind            = dsdl_value_set;
    result->flags           = DSDL_VALUE_FLAG_OWNED;
    result->as.set.count    = count;
    result->as.set.elements = elements;
    return true;
}

static bool dsdl_set_attribute(const dsdl_value_t* const set_val, const dsdl_attr_kind_t attr, dsdl_value_t* const out)
{
    if ((set_val == NULL) || (out == NULL) || (set_val->kind != dsdl_value_set)) {
        return false;
    }
    if (attr == dsdl_attr_count) {
        out->kind        = dsdl_value_rational;
        out->flags       = 0;
        out->as.rational = dsdl_rational_from_uintmax((uintmax_t)set_val->as.set.count);
        return true;
    }
    if ((attr != dsdl_attr_min) && (attr != dsdl_attr_max)) {
        return false;
    }
    if (set_val->as.set.count == 0) {
        return false;
    }

    dsdl_value_kind_t elem_kind = dsdl_value_rational;
    if (!dsdl_set_is_homogeneous(set_val, &elem_kind) || (elem_kind != dsdl_value_rational)) {
        return false;
    }

    dsdl_rational_t best = set_val->as.set.elements[0].as.rational;
    for (size_t i = 1; i < set_val->as.set.count; i++) {
        const dsdl_rational_t cand = set_val->as.set.elements[i].as.rational;
        const int             cmp  = dsdl_rational_cmp(cand, best);
        if ((attr == dsdl_attr_min) && (cmp < 0)) {
            best = cand;
        } else if ((attr == dsdl_attr_max) && (cmp > 0)) {
            best = cand;
        }
    }

    out->kind        = dsdl_value_rational;
    out->flags       = 0;
    out->as.rational = best;
    return true;
}

static bool dsdl_parse_primitive_bit_width(const wkv_str_t name, uint_least8_t* const out_bits)
{
    if ((name.str == NULL) || (out_bits == NULL)) {
        return false;
    }
    if ((name.len == 4U) && (memcmp(name.str, "bool", 4) == 0)) {
        *out_bits = 1U;
        return true;
    }
    if ((name.len == 4U) && (memcmp(name.str, "byte", 4) == 0)) {
        *out_bits = 8U;
        return true;
    }
    if ((name.len == 4U) && (memcmp(name.str, "utf8", 4) == 0)) {
        *out_bits = 8U;
        return true;
    }

    const struct
    {
        const char*   prefix;
        size_t        prefix_len;
        uint_least8_t min_bits;
        uint_least8_t max_bits;
        bool          strict_sizes;
    } prefixes[] = {
        { "uint", 4U, 1U, 64U, false },
        { "int", 3U, 2U, 64U, false },
        { "void", 4U, 1U, 64U, false },
        { "float", 5U, 0U, 0U, true },
    };

    for (size_t i = 0; i < (sizeof(prefixes) / sizeof(prefixes[0])); i++) {
        if (name.len <= prefixes[i].prefix_len) {
            continue;
        }
        if (memcmp(name.str, prefixes[i].prefix, prefixes[i].prefix_len) != 0) {
            continue;
        }
        uint64_t     value = 0;
        const size_t start = prefixes[i].prefix_len;
        for (size_t j = start; j < name.len; j++) {
            const char c = name.str[j];
            if (!dsdl_is_digit(c)) {
                return false;
            }
            value = (value * 10U) + (uint64_t)(c - '0');
            if (value > 64U) {
                return false;
            }
        }
        const uint_least8_t bits = (uint_least8_t)value;
        if (prefixes[i].strict_sizes) {
            if ((bits != 16U) && (bits != 32U) && (bits != 64U)) {
                return false;
            }
        } else {
            if ((bits < prefixes[i].min_bits) || (bits > prefixes[i].max_bits)) {
                return false;
            }
        }
        *out_bits = bits;
        return true;
    }

    return false;
}

static bool dsdl_apply_type_attribute(dsdl_t* const          dsdl,
                                      const wkv_str_t        type_name,
                                      const dsdl_attr_kind_t attr,
                                      dsdl_value_t* const    result)
{
    uint_least8_t bit_width = 0;
    if (!dsdl_parse_primitive_bit_width(type_name, &bit_width)) {
        return false;
    }

    if (attr == dsdl_attr_bit_length) {
        dsdl_value_t* const elements = (dsdl_value_t*)dsdl_alloc(dsdl, sizeof(dsdl_value_t));
        if (elements == NULL) {
            return false;
        }
        elements[0].kind        = dsdl_value_rational;
        elements[0].flags       = 0;
        elements[0].as.rational = dsdl_rational_from_uintmax((uintmax_t)bit_width);
        result->kind            = dsdl_value_set;
        result->flags           = DSDL_VALUE_FLAG_OWNED;
        result->as.set.count    = 1;
        result->as.set.elements = elements;
        return true;
    }
    if (attr == dsdl_attr_extent) {
        result->kind        = dsdl_value_rational;
        result->flags       = 0;
        result->as.rational = dsdl_rational_from_uintmax((uintmax_t)bit_width);
        return true;
    }

    return false;
}

static bool dsdl_apply_type_attribute_composite(dsdl_t* const                      dsdl,
                                                const dsdl_type_composite_t* const type,
                                                const dsdl_attr_kind_t             attr,
                                                dsdl_value_t* const                result)
{
    if ((dsdl == NULL) || (type == NULL) || (result == NULL)) {
        return false;
    }
    if ((type->response != NULL) && (attr == dsdl_attr_bit_length)) {
        return false; // Services do not define _bit_length_
    }
    if (attr == dsdl_attr_bit_length) {
        dsdl_bls_t* const bls = dsdl_type_bls(dsdl, (dsdl_type_t*)(uintptr_t)type);
        if (bls == NULL) {
            return false;
        }
        return dsdl_value_from_bls(dsdl, bls, result);
    }
    if (attr == dsdl_attr_extent) {
        uint64_t extent_bits = 0;
        if (type->extent > 0) {
            extent_bits = type->extent * 8U;
        } else {
            dsdl_bls_t* const bls = dsdl_type_bls(dsdl, (dsdl_type_t*)(uintptr_t)type);
            if (bls == NULL) {
                return false;
            }
            extent_bits = dsdl_bls_max(bls);
        }
        result->kind        = dsdl_value_rational;
        result->flags       = 0;
        result->as.rational = dsdl_rational_from_uintmax((uintmax_t)extent_bits);
        return true;
    }
    return false;
}

static bool dsdl_type_constant_lookup(dsdl_t* const                      dsdl,
                                      const dsdl_type_composite_t* const type,
                                      const wkv_str_t                    name,
                                      dsdl_value_t* const                out)
{
    if ((dsdl == NULL) || (type == NULL) || (out == NULL)) {
        return false;
    }
    if ((type->constant_names == NULL) || (type->constant_values == NULL)) {
        return false;
    }
    for (size_t i = 0; i < type->constant_count; i++) {
        if (dsdl_str_equal(type->constant_names[i], name)) {
            return dsdl_value_clone(dsdl, &type->constant_values[i], out);
        }
    }
    return false;
}

static bool dsdl_apply_binary_op(dsdl_t* const             dsdl,
                                 const dsdl_op_t           op,
                                 const dsdl_value_t* const left,
                                 const dsdl_value_t* const right,
                                 dsdl_value_t* const       result);

static bool dsdl_apply_attribute_kind(dsdl_t* const             dsdl,
                                      const dsdl_attr_kind_t    attr,
                                      const dsdl_value_t* const value,
                                      dsdl_value_t* const       result);

static bool dsdl_apply_attribute_name(dsdl_t* const             dsdl,
                                      const wkv_str_t           attr,
                                      const dsdl_value_t* const value,
                                      dsdl_value_t* const       result);

static bool dsdl_apply_unary_op(dsdl_t* const             dsdl,
                                const dsdl_unary_op_t     op,
                                const dsdl_value_t* const operand,
                                dsdl_value_t* const       result);

static bool dsdl_set_elementwise_op(dsdl_t* const             dsdl,
                                    const dsdl_op_t           op,
                                    const dsdl_value_t* const set_val,
                                    const dsdl_value_t* const scalar,
                                    const bool                scalar_left,
                                    dsdl_value_t* const       out)
{
    if ((set_val == NULL) || (scalar == NULL) || (out == NULL) || (set_val->kind != dsdl_value_set)) {
        return false;
    }
    if (scalar->kind == dsdl_value_deferred || set_val->kind == dsdl_value_deferred) {
        return false;
    }
    if (set_val->as.set.count == 0) {
        out->kind            = dsdl_value_set;
        out->flags           = DSDL_VALUE_FLAG_OWNED;
        out->as.set.count    = 0;
        out->as.set.elements = NULL;
        return true;
    }

    if (!dsdl_set_is_homogeneous(set_val, NULL)) {
        return false;
    }

    const size_t        capacity = set_val->as.set.count;
    dsdl_value_t* const elements = (dsdl_value_t*)dsdl_alloc(dsdl, capacity * sizeof(dsdl_value_t));
    if (elements == NULL) {
        return false;
    }

    size_t            count = 0;
    dsdl_value_kind_t kind  = dsdl_value_rational;
    for (size_t i = 0; i < set_val->as.set.count; i++) {
        const dsdl_value_t* lhs = scalar_left ? scalar : &set_val->as.set.elements[i];
        const dsdl_value_t* rhs = scalar_left ? &set_val->as.set.elements[i] : scalar;

        dsdl_value_t elem_result;
        if (!dsdl_apply_binary_op(dsdl, op, lhs, rhs, &elem_result)) {
            for (size_t j = 0; j < count; j++) {
                dsdl_value_dispose(dsdl, &elements[j]);
            }
            dsdl_free(dsdl, elements);
            return false;
        }
        if (elem_result.kind == dsdl_value_set || elem_result.kind == dsdl_value_deferred) {
            dsdl_value_dispose(dsdl, &elem_result);
            for (size_t j = 0; j < count; j++) {
                dsdl_value_dispose(dsdl, &elements[j]);
            }
            dsdl_free(dsdl, elements);
            return false;
        }
        if (count == 0) {
            kind = elem_result.kind;
        } else if (elem_result.kind != kind) {
            dsdl_value_dispose(dsdl, &elem_result);
            for (size_t j = 0; j < count; j++) {
                dsdl_value_dispose(dsdl, &elements[j]);
            }
            dsdl_free(dsdl, elements);
            return false;
        }
        bool duplicate = false;
        for (size_t j = 0; j < count; j++) {
            if (dsdl_value_equal(&elements[j], &elem_result)) {
                duplicate = true;
                break;
            }
        }
        if (!duplicate) {
            elements[count++] = elem_result;
        } else {
            dsdl_value_dispose(dsdl, &elem_result);
        }
    }

    out->kind            = dsdl_value_set;
    out->flags           = DSDL_VALUE_FLAG_OWNED;
    out->as.set.count    = count;
    out->as.set.elements = elements;
    return true;
}

typedef struct
{
    dsdl_t*      dsdl;
    dsdl_op_t    op;
    dsdl_value_t left;
    dsdl_value_t right;
} dsdl_closure_binary_ctx_t;

typedef struct
{
    dsdl_t*         dsdl;
    dsdl_unary_op_t op;
    dsdl_value_t    operand;
} dsdl_closure_unary_ctx_t;

typedef struct
{
    dsdl_t*      dsdl;
    wkv_str_t    attr;
    dsdl_value_t base;
} dsdl_closure_attr_ctx_t;

typedef struct
{
    dsdl_t*              dsdl;
    dsdl_eval_context_t* eval;
} dsdl_closure_offset_ctx_t;

typedef struct
{
    dsdl_t*              dsdl;
    dsdl_eval_context_t* eval;
    wkv_str_t            name;
} dsdl_closure_symbol_ctx_t;

typedef struct
{
    dsdl_t*              dsdl;
    dsdl_eval_context_t* eval;
    wkv_str_t            type_name;
    uint_least8_t        major;
    uint_least8_t        minor;
} dsdl_closure_type_ref_ctx_t;

static bool dsdl_closure_eval_binary(dsdl_closure_t* const self, dsdl_value_t* const out)
{
    if ((self == NULL) || (out == NULL)) {
        return false;
    }
    dsdl_closure_binary_ctx_t* const ctx = (dsdl_closure_binary_ctx_t*)self->context;
    if ((ctx == NULL) || (ctx->dsdl == NULL)) {
        return false;
    }
    if (!dsdl_resolve_value(&ctx->left) || !dsdl_resolve_value(&ctx->right)) {
        return false;
    }
    return dsdl_apply_binary_op(ctx->dsdl, ctx->op, &ctx->left, &ctx->right, out);
}

static bool dsdl_closure_eval_unary(dsdl_closure_t* const self, dsdl_value_t* const out)
{
    if ((self == NULL) || (out == NULL)) {
        return false;
    }
    dsdl_closure_unary_ctx_t* const ctx = (dsdl_closure_unary_ctx_t*)self->context;
    if ((ctx == NULL) || (ctx->dsdl == NULL)) {
        return false;
    }
    if (!dsdl_resolve_value(&ctx->operand)) {
        return false;
    }
    return dsdl_apply_unary_op(ctx->dsdl, ctx->op, &ctx->operand, out);
}

static bool dsdl_closure_eval_attribute(dsdl_closure_t* const self, dsdl_value_t* const out)
{
    if ((self == NULL) || (out == NULL)) {
        return false;
    }
    dsdl_closure_attr_ctx_t* const ctx = (dsdl_closure_attr_ctx_t*)self->context;
    if ((ctx == NULL) || (ctx->dsdl == NULL)) {
        return false;
    }
    if (!dsdl_resolve_value(&ctx->base)) {
        return false;
    }
    return dsdl_apply_attribute_name(ctx->dsdl, ctx->attr, &ctx->base, out);
}

static bool dsdl_closure_eval_offset(dsdl_closure_t* const self, dsdl_value_t* const out)
{
    if ((self == NULL) || (out == NULL)) {
        return false;
    }
    dsdl_closure_offset_ctx_t* const ctx = (dsdl_closure_offset_ctx_t*)self->context;
    if ((ctx == NULL) || (ctx->eval == NULL) || (ctx->eval->dsdl == NULL) || (ctx->eval->offset == NULL) ||
        !ctx->eval->offset_is_defined) {
        return false;
    }
    return dsdl_value_from_bls(ctx->eval->dsdl, ctx->eval->offset, out);
}

static bool dsdl_closure_eval_symbol(dsdl_closure_t* const self, dsdl_value_t* const out)
{
    if ((self == NULL) || (out == NULL)) {
        return false;
    }
    dsdl_closure_symbol_ctx_t* const ctx = (dsdl_closure_symbol_ctx_t*)self->context;
    if ((ctx == NULL) || (ctx->dsdl == NULL) || (ctx->eval == NULL)) {
        return false;
    }
    if ((ctx->eval->constant_names == NULL) || (ctx->eval->constant_values == NULL)) {
        return false;
    }
    for (size_t i = 0; i < ctx->eval->constant_count; i++) {
        if (dsdl_str_equal(ctx->eval->constant_names[i], ctx->name)) {
            return dsdl_value_clone(ctx->dsdl, &ctx->eval->constant_values[i], out);
        }
    }
    return false;
}

static bool dsdl_closure_eval_type_ref(dsdl_closure_t* const self, dsdl_value_t* const out)
{
    if ((self == NULL) || (out == NULL)) {
        return false;
    }
    dsdl_closure_type_ref_ctx_t* const ctx = (dsdl_closure_type_ref_ctx_t*)self->context;
    if ((ctx == NULL) || (ctx->dsdl == NULL) || (ctx->eval == NULL)) {
        return false;
    }
    const dsdl_type_composite_t* const type =
      dsdl_resolve_composite_type(ctx->dsdl, ctx->type_name, ctx->major, ctx->minor, ctx->eval->current_namespace);
    if (type == NULL) {
        return false;
    }
    out->kind        = dsdl_value_type;
    out->flags       = 0;
    out->as.type_ref = (void*)(uintptr_t)type;
    return true;
}

static void dsdl_closure_cleanup_binary(dsdl_closure_t* const self)
{
    if (self == NULL) {
        return;
    }
    dsdl_closure_binary_ctx_t* const ctx = (dsdl_closure_binary_ctx_t*)self->context;
    if ((ctx == NULL) || (ctx->dsdl == NULL)) {
        return;
    }
    dsdl_value_dispose(ctx->dsdl, &ctx->left);
    dsdl_value_dispose(ctx->dsdl, &ctx->right);
    dsdl_free(ctx->dsdl, ctx);
    self->context = NULL;
}

static void dsdl_closure_cleanup_unary(dsdl_closure_t* const self)
{
    if (self == NULL) {
        return;
    }
    dsdl_closure_unary_ctx_t* const ctx = (dsdl_closure_unary_ctx_t*)self->context;
    if ((ctx == NULL) || (ctx->dsdl == NULL)) {
        return;
    }
    dsdl_value_dispose(ctx->dsdl, &ctx->operand);
    dsdl_free(ctx->dsdl, ctx);
    self->context = NULL;
}

static void dsdl_closure_cleanup_attribute(dsdl_closure_t* const self)
{
    if (self == NULL) {
        return;
    }
    dsdl_closure_attr_ctx_t* const ctx = (dsdl_closure_attr_ctx_t*)self->context;
    if ((ctx == NULL) || (ctx->dsdl == NULL)) {
        return;
    }
    dsdl_value_dispose(ctx->dsdl, &ctx->base);
    if (ctx->attr.str != NULL) {
        dsdl_free_str(ctx->dsdl, ctx->attr.str);
    }
    dsdl_free(ctx->dsdl, ctx);
    self->context = NULL;
}

static void dsdl_closure_cleanup_offset(dsdl_closure_t* const self)
{
    if (self == NULL) {
        return;
    }
    dsdl_closure_offset_ctx_t* const ctx = (dsdl_closure_offset_ctx_t*)self->context;
    if ((ctx == NULL) || (ctx->dsdl == NULL)) {
        return;
    }
    dsdl_free(ctx->dsdl, ctx);
    self->context = NULL;
}

static void dsdl_closure_cleanup_symbol(dsdl_closure_t* const self)
{
    if (self == NULL) {
        return;
    }
    dsdl_closure_symbol_ctx_t* const ctx = (dsdl_closure_symbol_ctx_t*)self->context;
    if ((ctx == NULL) || (ctx->dsdl == NULL)) {
        return;
    }
    if (ctx->name.str != NULL) {
        dsdl_free_str(ctx->dsdl, ctx->name.str);
    }
    dsdl_free(ctx->dsdl, ctx);
    self->context = NULL;
}

static void dsdl_closure_cleanup_type_ref(dsdl_closure_t* const self)
{
    if (self == NULL) {
        return;
    }
    dsdl_closure_type_ref_ctx_t* const ctx = (dsdl_closure_type_ref_ctx_t*)self->context;
    if ((ctx == NULL) || (ctx->dsdl == NULL)) {
        return;
    }
    if (ctx->type_name.str != NULL) {
        dsdl_free_str(ctx->dsdl, ctx->type_name.str);
    }
    dsdl_free(ctx->dsdl, ctx);
    self->context = NULL;
}

static bool dsdl_closure_clone_binary(const dsdl_closure_t* const self, dsdl_closure_t* const out)
{
    if ((self == NULL) || (out == NULL)) {
        return false;
    }
    const dsdl_closure_binary_ctx_t* const ctx = (const dsdl_closure_binary_ctx_t*)self->context;
    if ((ctx == NULL) || (ctx->dsdl == NULL)) {
        return false;
    }
    dsdl_closure_binary_ctx_t* const clone_ctx = (dsdl_closure_binary_ctx_t*)dsdl_alloc(ctx->dsdl, sizeof(*clone_ctx));
    if (clone_ctx == NULL) {
        return false;
    }
    clone_ctx->dsdl = ctx->dsdl;
    clone_ctx->op   = ctx->op;
    if (!dsdl_value_clone(ctx->dsdl, &ctx->left, &clone_ctx->left)) {
        dsdl_free(ctx->dsdl, clone_ctx);
        return false;
    }
    if (!dsdl_value_clone(ctx->dsdl, &ctx->right, &clone_ctx->right)) {
        dsdl_value_dispose(ctx->dsdl, &clone_ctx->left);
        dsdl_free(ctx->dsdl, clone_ctx);
        return false;
    }
    out->context = clone_ctx;
    out->fun     = dsdl_closure_eval_binary;
    out->cleanup = dsdl_closure_cleanup_binary;
    out->clone   = dsdl_closure_clone_binary;
    return true;
}

static bool dsdl_closure_clone_unary(const dsdl_closure_t* const self, dsdl_closure_t* const out)
{
    if ((self == NULL) || (out == NULL)) {
        return false;
    }
    const dsdl_closure_unary_ctx_t* const ctx = (const dsdl_closure_unary_ctx_t*)self->context;
    if ((ctx == NULL) || (ctx->dsdl == NULL)) {
        return false;
    }
    dsdl_closure_unary_ctx_t* const clone_ctx = (dsdl_closure_unary_ctx_t*)dsdl_alloc(ctx->dsdl, sizeof(*clone_ctx));
    if (clone_ctx == NULL) {
        return false;
    }
    clone_ctx->dsdl = ctx->dsdl;
    clone_ctx->op   = ctx->op;
    if (!dsdl_value_clone(ctx->dsdl, &ctx->operand, &clone_ctx->operand)) {
        dsdl_free(ctx->dsdl, clone_ctx);
        return false;
    }
    out->context = clone_ctx;
    out->fun     = dsdl_closure_eval_unary;
    out->cleanup = dsdl_closure_cleanup_unary;
    out->clone   = dsdl_closure_clone_unary;
    return true;
}

static bool dsdl_closure_clone_attribute(const dsdl_closure_t* const self, dsdl_closure_t* const out)
{
    if ((self == NULL) || (out == NULL)) {
        return false;
    }
    const dsdl_closure_attr_ctx_t* const ctx = (const dsdl_closure_attr_ctx_t*)self->context;
    if ((ctx == NULL) || (ctx->dsdl == NULL)) {
        return false;
    }
    dsdl_closure_attr_ctx_t* const clone_ctx = (dsdl_closure_attr_ctx_t*)dsdl_alloc(ctx->dsdl, sizeof(*clone_ctx));
    if (clone_ctx == NULL) {
        return false;
    }
    clone_ctx->dsdl = ctx->dsdl;
    if (!dsdl_copy_wkv_str(ctx->dsdl, ctx->attr, &clone_ctx->attr)) {
        dsdl_free(ctx->dsdl, clone_ctx);
        return false;
    }
    if (!dsdl_value_clone(ctx->dsdl, &ctx->base, &clone_ctx->base)) {
        if (clone_ctx->attr.str != NULL) {
            dsdl_free_str(ctx->dsdl, clone_ctx->attr.str);
        }
        dsdl_free(ctx->dsdl, clone_ctx);
        return false;
    }
    out->context = clone_ctx;
    out->fun     = dsdl_closure_eval_attribute;
    out->cleanup = dsdl_closure_cleanup_attribute;
    out->clone   = dsdl_closure_clone_attribute;
    return true;
}

static bool dsdl_closure_clone_symbol(const dsdl_closure_t* const self, dsdl_closure_t* const out)
{
    if ((self == NULL) || (out == NULL)) {
        return false;
    }
    const dsdl_closure_symbol_ctx_t* const ctx = (const dsdl_closure_symbol_ctx_t*)self->context;
    if ((ctx == NULL) || (ctx->dsdl == NULL)) {
        return false;
    }
    dsdl_closure_symbol_ctx_t* const clone_ctx = (dsdl_closure_symbol_ctx_t*)dsdl_alloc(ctx->dsdl, sizeof(*clone_ctx));
    if (clone_ctx == NULL) {
        return false;
    }
    clone_ctx->dsdl = ctx->dsdl;
    clone_ctx->eval = ctx->eval;
    if (!dsdl_copy_wkv_str(ctx->dsdl, ctx->name, &clone_ctx->name)) {
        dsdl_free(ctx->dsdl, clone_ctx);
        return false;
    }
    out->context = clone_ctx;
    out->fun     = dsdl_closure_eval_symbol;
    out->cleanup = dsdl_closure_cleanup_symbol;
    out->clone   = dsdl_closure_clone_symbol;
    return true;
}

static bool dsdl_closure_clone_type_ref(const dsdl_closure_t* const self, dsdl_closure_t* const out)
{
    if ((self == NULL) || (out == NULL)) {
        return false;
    }
    const dsdl_closure_type_ref_ctx_t* const ctx = (const dsdl_closure_type_ref_ctx_t*)self->context;
    if ((ctx == NULL) || (ctx->dsdl == NULL)) {
        return false;
    }
    dsdl_closure_type_ref_ctx_t* const clone_ctx =
      (dsdl_closure_type_ref_ctx_t*)dsdl_alloc(ctx->dsdl, sizeof(*clone_ctx));
    if (clone_ctx == NULL) {
        return false;
    }
    clone_ctx->dsdl  = ctx->dsdl;
    clone_ctx->eval  = ctx->eval;
    clone_ctx->major = ctx->major;
    clone_ctx->minor = ctx->minor;
    if (!dsdl_copy_wkv_str(ctx->dsdl, ctx->type_name, &clone_ctx->type_name)) {
        dsdl_free(ctx->dsdl, clone_ctx);
        return false;
    }
    out->context = clone_ctx;
    out->fun     = dsdl_closure_eval_type_ref;
    out->cleanup = dsdl_closure_cleanup_type_ref;
    out->clone   = dsdl_closure_clone_type_ref;
    return true;
}

static bool dsdl_closure_clone_offset(const dsdl_closure_t* const self, dsdl_closure_t* const out)
{
    if ((self == NULL) || (out == NULL)) {
        return false;
    }
    const dsdl_closure_offset_ctx_t* const ctx = (const dsdl_closure_offset_ctx_t*)self->context;
    if ((ctx == NULL) || (ctx->dsdl == NULL)) {
        return false;
    }
    dsdl_closure_offset_ctx_t* const clone_ctx = (dsdl_closure_offset_ctx_t*)dsdl_alloc(ctx->dsdl, sizeof(*clone_ctx));
    if (clone_ctx == NULL) {
        return false;
    }
    clone_ctx->dsdl = ctx->dsdl;
    clone_ctx->eval = ctx->eval;
    out->context    = clone_ctx;
    out->fun        = dsdl_closure_eval_offset;
    out->cleanup    = dsdl_closure_cleanup_offset;
    out->clone      = dsdl_closure_clone_offset;
    return true;
}

static bool dsdl_make_binary_closure(dsdl_t* const             dsdl,
                                     const dsdl_op_t           op,
                                     const dsdl_value_t* const left,
                                     const dsdl_value_t* const right,
                                     dsdl_value_t* const       result)
{
    dsdl_closure_binary_ctx_t* const ctx = (dsdl_closure_binary_ctx_t*)dsdl_alloc(dsdl, sizeof(*ctx));
    if (ctx == NULL) {
        return false;
    }
    ctx->dsdl = dsdl;
    ctx->op   = op;
    if (!dsdl_value_clone(dsdl, left, &ctx->left)) {
        dsdl_free(dsdl, ctx);
        return false;
    }
    if (!dsdl_value_clone(dsdl, right, &ctx->right)) {
        dsdl_value_dispose(dsdl, &ctx->left);
        dsdl_free(dsdl, ctx);
        return false;
    }
    result->kind                = dsdl_value_deferred;
    result->flags               = DSDL_VALUE_FLAG_OWNED;
    result->as.deferred.fun     = dsdl_closure_eval_binary;
    result->as.deferred.cleanup = dsdl_closure_cleanup_binary;
    result->as.deferred.clone   = dsdl_closure_clone_binary;
    result->as.deferred.context = ctx;
    return true;
}

static bool dsdl_make_unary_closure(dsdl_t* const             dsdl,
                                    const dsdl_unary_op_t     op,
                                    const dsdl_value_t* const operand,
                                    dsdl_value_t* const       result)
{
    dsdl_closure_unary_ctx_t* const ctx = (dsdl_closure_unary_ctx_t*)dsdl_alloc(dsdl, sizeof(*ctx));
    if (ctx == NULL) {
        return false;
    }
    ctx->dsdl = dsdl;
    ctx->op   = op;
    if (!dsdl_value_clone(dsdl, operand, &ctx->operand)) {
        dsdl_free(dsdl, ctx);
        return false;
    }
    result->kind                = dsdl_value_deferred;
    result->flags               = DSDL_VALUE_FLAG_OWNED;
    result->as.deferred.fun     = dsdl_closure_eval_unary;
    result->as.deferred.cleanup = dsdl_closure_cleanup_unary;
    result->as.deferred.clone   = dsdl_closure_clone_unary;
    result->as.deferred.context = ctx;
    return true;
}

static bool dsdl_make_attribute_closure(dsdl_t* const             dsdl,
                                        const wkv_str_t           attr,
                                        const dsdl_value_t* const base,
                                        dsdl_value_t* const       result)
{
    dsdl_closure_attr_ctx_t* const ctx = (dsdl_closure_attr_ctx_t*)dsdl_alloc(dsdl, sizeof(*ctx));
    if (ctx == NULL) {
        return false;
    }
    ctx->dsdl = dsdl;
    if (!dsdl_copy_wkv_str(dsdl, attr, &ctx->attr)) {
        dsdl_free(dsdl, ctx);
        return false;
    }
    if (!dsdl_value_clone(dsdl, base, &ctx->base)) {
        if (ctx->attr.str != NULL) {
            dsdl_free_str(dsdl, ctx->attr.str);
        }
        dsdl_free(dsdl, ctx);
        return false;
    }
    result->kind                = dsdl_value_deferred;
    result->flags               = DSDL_VALUE_FLAG_OWNED;
    result->as.deferred.fun     = dsdl_closure_eval_attribute;
    result->as.deferred.cleanup = dsdl_closure_cleanup_attribute;
    result->as.deferred.clone   = dsdl_closure_clone_attribute;
    result->as.deferred.context = ctx;
    return true;
}

static bool dsdl_make_offset_closure(dsdl_t* const dsdl, dsdl_eval_context_t* const eval, dsdl_value_t* const result)
{
    if (eval == NULL) {
        return false;
    }
    dsdl_closure_offset_ctx_t* const ctx = (dsdl_closure_offset_ctx_t*)dsdl_alloc(dsdl, sizeof(*ctx));
    if (ctx == NULL) {
        return false;
    }
    ctx->dsdl                   = dsdl;
    ctx->eval                   = eval;
    result->kind                = dsdl_value_deferred;
    result->flags               = DSDL_VALUE_FLAG_OWNED;
    result->as.deferred.fun     = dsdl_closure_eval_offset;
    result->as.deferred.cleanup = dsdl_closure_cleanup_offset;
    result->as.deferred.clone   = dsdl_closure_clone_offset;
    result->as.deferred.context = ctx;
    return true;
}

static bool dsdl_make_symbol_closure(dsdl_t* const              dsdl,
                                     dsdl_eval_context_t* const eval,
                                     const wkv_str_t            name,
                                     dsdl_value_t* const        result)
{
    if ((eval == NULL) || (result == NULL)) {
        return false;
    }
    dsdl_closure_symbol_ctx_t* const ctx = (dsdl_closure_symbol_ctx_t*)dsdl_alloc(dsdl, sizeof(*ctx));
    if (ctx == NULL) {
        return false;
    }
    ctx->dsdl = dsdl;
    ctx->eval = eval;
    if (!dsdl_copy_wkv_str(dsdl, name, &ctx->name)) {
        dsdl_free(dsdl, ctx);
        return false;
    }
    result->kind                = dsdl_value_deferred;
    result->flags               = DSDL_VALUE_FLAG_OWNED;
    result->as.deferred.fun     = dsdl_closure_eval_symbol;
    result->as.deferred.cleanup = dsdl_closure_cleanup_symbol;
    result->as.deferred.clone   = dsdl_closure_clone_symbol;
    result->as.deferred.context = ctx;
    return true;
}

static bool dsdl_make_type_ref_closure(dsdl_t* const              dsdl,
                                       dsdl_eval_context_t* const eval,
                                       const wkv_str_t            type_name,
                                       const uint_least8_t        major,
                                       const uint_least8_t        minor,
                                       dsdl_value_t* const        result)
{
    if ((eval == NULL) || (result == NULL)) {
        return false;
    }
    dsdl_closure_type_ref_ctx_t* const ctx = (dsdl_closure_type_ref_ctx_t*)dsdl_alloc(dsdl, sizeof(*ctx));
    if (ctx == NULL) {
        return false;
    }
    ctx->dsdl  = dsdl;
    ctx->eval  = eval;
    ctx->major = major;
    ctx->minor = minor;
    if (!dsdl_copy_wkv_str(dsdl, type_name, &ctx->type_name)) {
        dsdl_free(dsdl, ctx);
        return false;
    }
    result->kind                = dsdl_value_deferred;
    result->flags               = DSDL_VALUE_FLAG_OWNED;
    result->as.deferred.fun     = dsdl_closure_eval_type_ref;
    result->as.deferred.cleanup = dsdl_closure_cleanup_type_ref;
    result->as.deferred.clone   = dsdl_closure_clone_type_ref;
    result->as.deferred.context = ctx;
    return true;
}

static bool dsdl_apply_attribute_kind(dsdl_t* const             dsdl,
                                      const dsdl_attr_kind_t    attr,
                                      const dsdl_value_t* const value,
                                      dsdl_value_t* const       result)
{
    if ((value == NULL) || (result == NULL)) {
        return false;
    }
    if (value->kind == dsdl_value_set) {
        return dsdl_set_attribute(value, attr, result);
    }
    if (value->kind == dsdl_value_string) {
        return dsdl_apply_type_attribute(dsdl, value->as.string, attr, result);
    }
    if (value->kind == dsdl_value_type) {
        return dsdl_apply_type_attribute_composite(
          dsdl, (const dsdl_type_composite_t*)value->as.type_ref, attr, result);
    }
    return false;
}

static bool dsdl_apply_attribute_name(dsdl_t* const             dsdl,
                                      const wkv_str_t           attr,
                                      const dsdl_value_t* const value,
                                      dsdl_value_t* const       result)
{
    if ((value == NULL) || (result == NULL)) {
        return false;
    }
    if (value->kind == dsdl_value_deferred) {
        return dsdl_make_attribute_closure(dsdl, attr, value, result);
    }
    dsdl_attr_kind_t kind;
    if (dsdl_attr_from_ident(attr, &kind)) {
        return dsdl_apply_attribute_kind(dsdl, kind, value, result);
    }
    if (value->kind != dsdl_value_type) {
        return false;
    }
    return dsdl_type_constant_lookup(dsdl, (const dsdl_type_composite_t*)value->as.type_ref, attr, result);
}

static bool dsdl_apply_unary_op(dsdl_t* const             dsdl,
                                const dsdl_unary_op_t     op,
                                const dsdl_value_t* const operand,
                                dsdl_value_t* const       result)
{
    if ((operand == NULL) || (result == NULL)) {
        return false;
    }
    if (operand->kind == dsdl_value_deferred) {
        return dsdl_make_unary_closure(dsdl, op, operand, result);
    }

    if (op == dsdl_unary_not) {
        if (operand->kind != dsdl_value_bool) {
            return false;
        }
        result->kind       = dsdl_value_bool;
        result->flags      = 0;
        result->as.boolean = !operand->as.boolean;
        return true;
    }
    if (op == dsdl_unary_pos) {
        if (operand->kind != dsdl_value_rational) {
            return false;
        }
        result->kind        = dsdl_value_rational;
        result->flags       = 0;
        result->as.rational = operand->as.rational;
        return true;
    }
    if (op == dsdl_unary_neg) {
        if (operand->kind != dsdl_value_rational) {
            return false;
        }
        result->kind        = dsdl_value_rational;
        result->flags       = 0;
        result->as.rational = dsdl_rational_neg(operand->as.rational);
        return true;
    }
    return false;
}

/// Apply a binary operation to two values.
static bool dsdl_apply_binary_op(dsdl_t* const             dsdl,
                                 const dsdl_op_t           op,
                                 const dsdl_value_t* const left,
                                 const dsdl_value_t* const right,
                                 dsdl_value_t* const       result)
{
    assert((dsdl != NULL) && (left != NULL) && (right != NULL) && (result != NULL));

    if ((left->kind == dsdl_value_deferred) || (right->kind == dsdl_value_deferred)) {
        return dsdl_make_binary_closure(dsdl, op, left, right, result);
    }

    if ((left->kind == dsdl_value_set) || (right->kind == dsdl_value_set)) {
        if ((left->kind == dsdl_value_set) && (right->kind == dsdl_value_set)) {
            dsdl_value_kind_t left_kind  = dsdl_value_rational;
            dsdl_value_kind_t right_kind = dsdl_value_rational;
            if (!dsdl_set_is_homogeneous(left, &left_kind) || !dsdl_set_is_homogeneous(right, &right_kind) ||
                (left_kind != right_kind)) {
                return false;
            }

            const bool subset_lr = dsdl_set_is_subset(left, right);
            const bool subset_rl = dsdl_set_is_subset(right, left);

            if (op == dsdl_op_eq) {
                result->kind       = dsdl_value_bool;
                result->flags      = 0;
                result->as.boolean = subset_lr && subset_rl;
                return true;
            }
            if (op == dsdl_op_ne) {
                result->kind       = dsdl_value_bool;
                result->flags      = 0;
                result->as.boolean = !(subset_lr && subset_rl);
                return true;
            }
            if (op == dsdl_op_le) {
                result->kind       = dsdl_value_bool;
                result->flags      = 0;
                result->as.boolean = subset_lr;
                return true;
            }
            if (op == dsdl_op_ge) {
                result->kind       = dsdl_value_bool;
                result->flags      = 0;
                result->as.boolean = subset_rl;
                return true;
            }
            if (op == dsdl_op_lt) {
                result->kind       = dsdl_value_bool;
                result->flags      = 0;
                result->as.boolean = subset_lr && !subset_rl;
                return true;
            }
            if (op == dsdl_op_gt) {
                result->kind       = dsdl_value_bool;
                result->flags      = 0;
                result->as.boolean = subset_rl && !subset_lr;
                return true;
            }
            if (op == dsdl_op_bit_or) {
                return dsdl_set_union(dsdl, left, right, result);
            }
            if (op == dsdl_op_bit_and) {
                return dsdl_set_intersection(dsdl, left, right, result);
            }
            if (op == dsdl_op_bit_xor) {
                return dsdl_set_symdiff(dsdl, left, right, result);
            }
            return false;
        }

        const dsdl_value_t* const set_val     = (left->kind == dsdl_value_set) ? left : right;
        const dsdl_value_t* const scalar      = (left->kind == dsdl_value_set) ? right : left;
        const bool                scalar_left = (left->kind != dsdl_value_set);

        if ((op == dsdl_op_add) || (op == dsdl_op_sub) || (op == dsdl_op_mul) || (op == dsdl_op_div) ||
            (op == dsdl_op_mod) || (op == dsdl_op_pow)) {
            return dsdl_set_elementwise_op(dsdl, op, set_val, scalar, scalar_left, result);
        }
        return false;
    }

    if ((left->kind == dsdl_value_rational) && (right->kind == dsdl_value_rational)) {
        const dsdl_rational_t a = left->as.rational;
        const dsdl_rational_t b = right->as.rational;

        if (op == dsdl_op_add) {
            result->kind        = dsdl_value_rational;
            result->flags       = 0;
            result->as.rational = dsdl_rational_add(a, b);
            return true;
        }
        if (op == dsdl_op_sub) {
            result->kind        = dsdl_value_rational;
            result->flags       = 0;
            result->as.rational = dsdl_rational_sub(a, b);
            return true;
        }
        if (op == dsdl_op_mul) {
            result->kind        = dsdl_value_rational;
            result->flags       = 0;
            result->as.rational = dsdl_rational_mul(a, b);
            return true;
        }
        if (op == dsdl_op_div) {
            result->kind        = dsdl_value_rational;
            result->flags       = 0;
            result->as.rational = dsdl_rational_div(a, b);
            return true;
        }
        if (op == dsdl_op_mod) {
            intmax_t a_int = 0;
            intmax_t b_int = 0;
            if (dsdl_rational_to_intmax(a, &a_int) && dsdl_rational_to_intmax(b, &b_int) && (b_int != 0)) {
                result->kind        = dsdl_value_rational;
                result->flags       = 0;
                result->as.rational = dsdl_rational_from_int(a_int % b_int);
                return true;
            }
            return false;
        }
        if (op == dsdl_op_pow) {
            result->kind  = dsdl_value_rational;
            result->flags = 0;
            if (dsdl_rational_is_int(b)) {
                intmax_t exp = 0;
                if (!dsdl_rational_to_intmax(b, &exp)) {
                    return false;
                }
                dsdl_rational_t base = a;
                dsdl_rational_t acc  = dsdl_rational_from_int(1);
                uintmax_t       e    = (exp < 0) ? (uintmax_t)(-(exp + 1)) + 1U : (uintmax_t)exp;

                if (exp < 0) {
                    if (dsdl_bigint_is_zero(&base.num)) {
                        return false;
                    }
                    dsdl_rational_t inv;
                    inv.num          = base.den;
                    inv.num.negative = base.num.negative;
                    inv.den          = base.num;
                    inv.den.negative = false;
                    base             = dsdl_rational_normalize(inv);
                }

                while (e > 0U) {
                    if ((e & 1U) != 0U) {
                        acc = dsdl_rational_mul(acc, base);
                    }
                    e >>= 1U;
                    if (e > 0U) {
                        base = dsdl_rational_mul(base, base);
                    }
                }
                result->as.rational = acc;
            } else {
                double base_d = 0.0;
                double exp_d  = 0.0;
                if (!dsdl_rational_to_double(a, &base_d) || !dsdl_rational_to_double(b, &exp_d)) {
                    return false;
                }
                const double result_d = pow(base_d, exp_d);
                result->as.rational   = dsdl_rational_from_double(result_d);
                if (dsdl_rational_is_nan(result->as.rational)) {
                    return false;
                }
            }
            return true;
        }
        if ((op == dsdl_op_bit_or) || (op == dsdl_op_bit_xor) || (op == dsdl_op_bit_and)) {
            if (dsdl_rational_is_int(a) && dsdl_rational_is_int(b)) {
                intmax_t a_int = 0;
                intmax_t b_int = 0;
                if (!dsdl_rational_to_intmax(a, &a_int) || !dsdl_rational_to_intmax(b, &b_int)) {
                    return false;
                }
                intmax_t r = 0;
                if (op == dsdl_op_bit_or) {
                    r = a_int | b_int;
                } else if (op == dsdl_op_bit_xor) {
                    r = a_int ^ b_int;
                } else {
                    r = a_int & b_int;
                }
                result->kind        = dsdl_value_rational;
                result->flags       = 0;
                result->as.rational = dsdl_rational_from_int(r);
                return true;
            }
            return false;
        }

        const int cmp = dsdl_rational_cmp(a, b);
        result->kind  = dsdl_value_bool;
        result->flags = 0;
        if (op == dsdl_op_eq) {
            result->as.boolean = (cmp == 0);
            return true;
        }
        if (op == dsdl_op_ne) {
            result->as.boolean = (cmp != 0);
            return true;
        }
        if (op == dsdl_op_lt) {
            result->as.boolean = (cmp < 0);
            return true;
        }
        if (op == dsdl_op_le) {
            result->as.boolean = (cmp <= 0);
            return true;
        }
        if (op == dsdl_op_gt) {
            result->as.boolean = (cmp > 0);
            return true;
        }
        if (op == dsdl_op_ge) {
            result->as.boolean = (cmp >= 0);
            return true;
        }
    }

    if ((left->kind == dsdl_value_bool) && (right->kind == dsdl_value_bool)) {
        result->kind  = dsdl_value_bool;
        result->flags = 0;
        if (op == dsdl_op_or) {
            result->as.boolean = left->as.boolean || right->as.boolean;
            return true;
        }
        if (op == dsdl_op_and) {
            result->as.boolean = left->as.boolean && right->as.boolean;
            return true;
        }
        if (op == dsdl_op_eq) {
            result->as.boolean = (left->as.boolean == right->as.boolean);
            return true;
        }
        if (op == dsdl_op_ne) {
            result->as.boolean = (left->as.boolean != right->as.boolean);
            return true;
        }
    }

    if ((left->kind == dsdl_value_string) && (right->kind == dsdl_value_string)) {
        if (op == dsdl_op_add) {
            const size_t total = left->as.string.len + right->as.string.len;
            char*        buf   = NULL;
            if (total > 0) {
                buf = (char*)dsdl_alloc(dsdl, total);
                if (buf == NULL) {
                    return false;
                }
                if (left->as.string.len > 0) {
                    (void)memcpy(buf, left->as.string.str, left->as.string.len);
                }
                if (right->as.string.len > 0) {
                    (void)memcpy(buf + left->as.string.len, right->as.string.str, right->as.string.len);
                }
            }
            result->kind      = dsdl_value_string;
            result->flags     = DSDL_VALUE_FLAG_OWNED;
            result->as.string = (wkv_str_t){ .len = total, .str = buf };
            return true;
        }
        if (op == dsdl_op_eq) {
            result->kind       = dsdl_value_bool;
            result->flags      = 0;
            result->as.boolean = dsdl_value_equal(left, right);
            return true;
        }
        if (op == dsdl_op_ne) {
            result->kind       = dsdl_value_bool;
            result->flags      = 0;
            result->as.boolean = !dsdl_value_equal(left, right);
            return true;
        }
    }

    return false;
}

// Forward declaration for expression parsing
static bool dsdl_parse_expr_prec(dsdl_parser_t* parser, dsdl_prec_t min_prec, dsdl_value_t* out_value);

/// Parse an atom (literal, identifier, or parenthesized expression).
static bool dsdl_parse_atom(dsdl_parser_t* const parser, dsdl_value_t* const out_value)
{
    dsdl_parser_skip_ws(parser);

    // Parenthesized expression
    if (dsdl_parser_peek(parser, 0) == '(') {
        dsdl_parser_advance(parser, 1);
        dsdl_parser_skip_ws(parser);

        if (!dsdl_parse_expression(parser, out_value)) {
            return false;
        }

        dsdl_parser_skip_ws(parser);
        if (dsdl_parser_peek(parser, 0) != ')') {
            return false; // Missing closing paren
        }
        dsdl_parser_advance(parser, 1);
        return true;
    }

    // Try literal first
    if (dsdl_parse_literal(parser, out_value)) {
        return true;
    }

    // Try versioned type reference (namespace.Type.Major.Minor)
    const size_t       start_pos = parser->pos;
    dsdl_parsed_type_t type_ref;
    if (dsdl_parse_type_versioned(parser, &type_ref)) {
        if (parser->eval_ctx == NULL) {
            return false;
        }
        return dsdl_make_type_ref_closure(parser->dsdl,
                                          parser->eval_ctx,
                                          type_ref.type_name,
                                          type_ref.version_major,
                                          type_ref.version_minor,
                                          out_value);
    }
    parser->pos = start_pos;

    // Try identifier (constant name, primitive type name, or _offset_)
    wkv_str_t ident = dsdl_parse_identifier(parser);
    if (ident.str != NULL) {
        if (((ident.len == 9U) && (memcmp(ident.str, "truncated", 9) == 0)) ||
            ((ident.len == 9U) && (memcmp(ident.str, "saturated", 9) == 0))) {
            dsdl_parser_skip_ws(parser);
            wkv_str_t     prim      = dsdl_parse_identifier(parser);
            uint_least8_t bit_width = 0;
            if ((prim.str == NULL) || !dsdl_parse_primitive_bit_width(prim, &bit_width)) {
                return false;
            }
            return dsdl_value_set_string_copy(parser->dsdl, prim, out_value);
        }
        // Check for the special _offset_ pseudo-variable
        if ((ident.len == 8) && (memcmp(ident.str, "_offset_", 8) == 0)) {
            return dsdl_make_offset_closure(parser->dsdl, parser->eval_ctx, out_value);
        }
        // Primitive type name (e.g., uint16) to enable type attribute access
        uint_least8_t bit_width = 0;
        if (dsdl_parse_primitive_bit_width(ident, &bit_width)) {
            return dsdl_value_set_string_copy(parser->dsdl, ident, out_value);
        }
        // Otherwise, defer symbol resolution to semantic analysis
        if (parser->eval_ctx == NULL) {
            return false;
        }
        return dsdl_make_symbol_closure(parser->dsdl, parser->eval_ctx, ident, out_value);
    }

    return false;
}

/// Parse unary prefix operators (!, +, -).
static bool dsdl_parse_unary(dsdl_parser_t* const parser, dsdl_value_t* const out_value)
{
    dsdl_parser_skip_ws(parser);

    const char c = dsdl_parser_peek(parser, 0);

    // Logical NOT
    if (c == '!') {
        dsdl_parser_advance(parser, 1);
        dsdl_parser_skip_ws(parser);

        dsdl_value_t operand;
        if (!dsdl_parse_unary(parser, &operand)) {
            return false;
        }
        dsdl_value_t result;
        if (!dsdl_apply_unary_op(parser->dsdl, dsdl_unary_not, &operand, &result)) {
            dsdl_value_dispose(parser->dsdl, &operand);
            return false;
        }
        dsdl_value_dispose(parser->dsdl, &operand);
        *out_value = result;
        return true;
    }

    // Unary plus
    if (c == '+') {
        dsdl_parser_advance(parser, 1);
        dsdl_parser_skip_ws(parser);

        dsdl_value_t operand;
        if (!dsdl_parse_expr_prec(parser, dsdl_prec_exp, &operand)) {
            return false;
        }
        dsdl_value_t result;
        if (!dsdl_apply_unary_op(parser->dsdl, dsdl_unary_pos, &operand, &result)) {
            dsdl_value_dispose(parser->dsdl, &operand);
            return false;
        }
        dsdl_value_dispose(parser->dsdl, &operand);
        *out_value = result;
        return true;
    }

    // Unary minus
    if (c == '-') {
        dsdl_parser_advance(parser, 1);
        dsdl_parser_skip_ws(parser);

        dsdl_value_t operand;
        if (!dsdl_parse_expr_prec(parser, dsdl_prec_exp, &operand)) {
            return false;
        }
        dsdl_value_t result;
        if (!dsdl_apply_unary_op(parser->dsdl, dsdl_unary_neg, &operand, &result)) {
            dsdl_value_dispose(parser->dsdl, &operand);
            return false;
        }
        dsdl_value_dispose(parser->dsdl, &operand);
        *out_value = result;
        return true;
    }

    // No unary operator, parse atom
    return dsdl_parse_atom(parser, out_value);
}

/// Parse expression with precedence climbing.
static bool dsdl_parse_expr_prec(dsdl_parser_t* const parser, const dsdl_prec_t min_prec, dsdl_value_t* const out_value)
{
    assert((parser != NULL) && (out_value != NULL));
    // Parse left-hand side (unary or atom)
    if (!dsdl_parse_unary(parser, out_value)) {
        return false;
    }

    while (true) {
        dsdl_parser_skip_ws(parser);

        // Check for binary operator
        dsdl_op_t         op;
        size_t            op_len;
        const dsdl_prec_t prec = dsdl_get_binary_op(parser, &op, &op_len);

        if (prec == dsdl_prec_none || prec < min_prec) {
            break;
        }

        // Consume operator
        dsdl_parser_advance(parser, op_len);
        dsdl_parser_skip_ws(parser);

        // Handle attribute access (.) specially
        if (op == dsdl_op_dot) {
            wkv_str_t attr = dsdl_parse_identifier(parser);
            if (attr.str == NULL) {
                return false;
            }
            dsdl_value_t attr_value;
            if (!dsdl_apply_attribute_name(parser->dsdl, attr, out_value, &attr_value)) {
                dsdl_value_dispose(parser->dsdl, out_value);
                return false;
            }
            dsdl_value_dispose(parser->dsdl, out_value);
            *out_value = attr_value;
            continue;
        }

        // Parse right-hand side with higher precedence (for left-associativity)
        // For right-associative ** we would use same precedence
        dsdl_prec_t next_prec = (dsdl_prec_t)(prec + 1);
        if (op == dsdl_op_pow) {
            next_prec = prec; // Right-associative
        }

        dsdl_value_t right;
        if (!dsdl_parse_expr_prec(parser, next_prec, &right)) {
            dsdl_value_dispose(parser->dsdl, out_value);
            return false;
        }

        // Apply operator
        dsdl_value_t result;
        if (!dsdl_apply_binary_op(parser->dsdl, op, out_value, &right, &result)) {
            dsdl_value_dispose(parser->dsdl, out_value);
            dsdl_value_dispose(parser->dsdl, &right);
            return false;
        }
        dsdl_value_dispose(parser->dsdl, out_value);
        dsdl_value_dispose(parser->dsdl, &right);
        *out_value = result;
    }

    return true;
}

/// Parse an expression (top-level entry point).
static bool dsdl_parse_expression(dsdl_parser_t* const parser, dsdl_value_t* const out_value)
{
    return dsdl_parse_expr_prec(parser, dsdl_prec_logical, out_value);
}

// ============================================================================
// Type parsing
// ============================================================================

/// Parse a bit length suffix (1-64).
/// Returns 0 on failure, otherwise the bit length.
static uint_least8_t dsdl_parse_bit_length(dsdl_parser_t* const parser)
{
    if (!dsdl_is_digit(dsdl_parser_peek(parser, 0))) {
        return 0;
    }
    // First digit must be 1-9 (no leading zeros allowed for non-zero numbers)
    if (dsdl_parser_peek(parser, 0) == '0') {
        return 0; // Bit length can't start with 0
    }

    uint64_t value = 0;
    while (dsdl_is_digit(dsdl_parser_peek(parser, 0))) {
        value = value * 10 + (uint64_t)(dsdl_parser_peek(parser, 0) - '0');
        dsdl_parser_advance(parser, 1);
        if (value > 64) {
            return 0; // Overflow - bit length too large
        }
    }

    return (uint_least8_t)value;
}

/// Parse a void type: void[1-64]
static bool dsdl_parse_type_void(dsdl_parser_t* const parser, dsdl_parsed_type_t* const out_type)
{
    if (!dsdl_parser_accept(parser, "void", 4)) {
        return false;
    }

    const uint_least8_t bits = dsdl_parse_bit_length(parser);
    if ((bits < 1) || (bits > 64)) {
        return false;
    }

    out_type->kind      = DSDL_VOID(bits);
    out_type->bit_width = bits;
    return true;
}

/// Parse a primitive type name (uint, int, float) with bit width.
static bool dsdl_parse_primitive_name(dsdl_parser_t* const parser, dsdl_parsed_type_t* const out_type)
{
    if (dsdl_parser_accept(parser, "uint", 4)) {
        const uint_least8_t bits = dsdl_parse_bit_length(parser);
        if ((bits < 1) || (bits > 64)) {
            return false;
        }
        out_type->kind      = DSDL_UINT(bits);
        out_type->bit_width = bits;
        return true;
    }

    if (dsdl_parser_accept(parser, "int", 3)) {
        const uint_least8_t bits = dsdl_parse_bit_length(parser);
        if ((bits < 2) || (bits > 64)) {
            return false; // int requires at least 2 bits
        }
        out_type->kind      = DSDL_INT(bits);
        out_type->bit_width = bits;
        return true;
    }

    if (dsdl_parser_accept(parser, "float", 5)) {
        const uint_least8_t bits = dsdl_parse_bit_length(parser);
        if ((bits != 16) && (bits != 32) && (bits != 64)) {
            return false; // Only float16, float32, float64
        }
        out_type->kind      = DSDL_FLOAT(bits);
        out_type->bit_width = bits;
        return true;
    }

    return false;
}

/// Parse a primitive type (bool, byte, utf8, or [saturated/truncated] primitive_name).
static bool dsdl_parse_type_primitive(dsdl_parser_t* const parser, dsdl_parsed_type_t* const out_type)
{
    // Check for "bool"
    if (dsdl_parser_match(parser, "bool", 4) && !dsdl_is_ident_cont(dsdl_parser_peek(parser, 4))) {
        dsdl_parser_advance(parser, 4);
        out_type->kind      = DSDL_BOOL;
        out_type->bit_width = 1;
        return true;
    }

    // Check for "byte"
    if (dsdl_parser_match(parser, "byte", 4) && !dsdl_is_ident_cont(dsdl_parser_peek(parser, 4))) {
        dsdl_parser_advance(parser, 4);
        out_type->kind      = DSDL_BYTE;
        out_type->bit_width = 8;
        return true;
    }

    // Check for "utf8" (alias for uint8)
    if (dsdl_parser_match(parser, "utf8", 4) && !dsdl_is_ident_cont(dsdl_parser_peek(parser, 4))) {
        dsdl_parser_advance(parser, 4);
        out_type->kind      = DSDL_UTF8;
        out_type->bit_width = 8;
        return true;
    }

    // Check for "truncated" modifier
    if (dsdl_parser_match(parser, "truncated", 9) && !dsdl_is_ident_cont(dsdl_parser_peek(parser, 9))) {
        dsdl_parser_advance(parser, 9);
        dsdl_parser_skip_ws(parser);
        if (!dsdl_parse_primitive_name(parser, out_type)) {
            return false;
        }
        if (dsdl_type_is_int(out_type->kind)) {
            return false; // Truncated signed integers are not allowed.
        }
        out_type->kind = (dsdl_type_t)(out_type->kind | DSDL_TYPE_TRUNCATED_FLAG);
        return true;
    }

    // Check for optional "saturated" modifier
    if (dsdl_parser_match(parser, "saturated", 9) && !dsdl_is_ident_cont(dsdl_parser_peek(parser, 9))) {
        dsdl_parser_advance(parser, 9);
        dsdl_parser_skip_ws(parser);
    }

    if (!dsdl_parse_primitive_name(parser, out_type)) {
        return false;
    }
    return true;
}

/// Parse a versioned type reference: namespace.Name.major.minor
static bool dsdl_parse_type_versioned(dsdl_parser_t* const parser, dsdl_parsed_type_t* const out_type)
{
    const size_t start_pos = parser->pos;

    // Must start with identifier
    wkv_str_t first = dsdl_parse_identifier(parser);
    if (first.str == NULL) {
        return false;
    }

    // Continue parsing namespace parts (identifier.identifier...)
    while (dsdl_parser_peek(parser, 0) == '.') {
        dsdl_parser_advance(parser, 1); // Skip '.'

        // Check if next part is an identifier or version number
        if (!dsdl_is_ident_start(dsdl_parser_peek(parser, 0))) {
            // Should be version specifier now
            break;
        }

        wkv_str_t part = dsdl_parse_identifier(parser);
        if (part.str == NULL) {
            parser->pos = start_pos;
            return false;
        }
    }

    // Now we should have type name (without version).
    // The last '.' was consumed, so now parse version: major.minor
    const size_t type_name_end = parser->pos - 1; // Before the last '.'

    // Parse major version
    dsdl_rational_t major_r = dsdl_parse_int_decimal(parser);
    if (dsdl_rational_is_nan(major_r)) {
        parser->pos = start_pos;
        return false;
    }

    // Expect '.'
    if (dsdl_parser_peek(parser, 0) != '.') {
        parser->pos = start_pos;
        return false;
    }
    dsdl_parser_advance(parser, 1);

    // Parse minor version
    dsdl_rational_t minor_r = dsdl_parse_int_decimal(parser);
    if (dsdl_rational_is_nan(minor_r)) {
        parser->pos = start_pos;
        return false;
    }

    intmax_t major = 0;
    intmax_t minor = 0;
    if (!dsdl_rational_to_intmax(major_r, &major) || !dsdl_rational_to_intmax(minor_r, &minor)) {
        parser->pos = start_pos;
        return false;
    }

    // Verify versions are valid (0-255)
    if ((major < 0) || (major > 255) || (minor < 0) || (minor > 255)) {
        parser->pos = start_pos;
        return false;
    }

    // Success - fill out type
    out_type->kind          = DSDL_COMPOSITE_STRUCT; // Placeholder until we resolve
    out_type->type_name.str = first.str;
    out_type->type_name.len = type_name_end - start_pos;
    out_type->version_major = (uint_least8_t)major;
    out_type->version_minor = (uint_least8_t)minor;

    return true;
}

/// Parse a scalar type (primitive, void, or versioned composite).
static bool dsdl_parse_type_scalar(dsdl_parser_t* const parser, dsdl_parsed_type_t* const out_type)
{
    const size_t start_pos = parser->pos;

    // Try void type first
    if (dsdl_parse_type_void(parser, out_type)) {
        return true;
    }
    parser->pos = start_pos;

    // Try primitive type
    if (dsdl_parse_type_primitive(parser, out_type)) {
        return true;
    }
    parser->pos = start_pos;

    // Try versioned composite type
    return dsdl_parse_type_versioned(parser, out_type);
}

/// Parse an array type: scalar_type [ expression ] or scalar_type [ <= expression ] etc.
static bool dsdl_parse_type_array(dsdl_parser_t* const parser, dsdl_parsed_type_t* const out_type)
{
    // First parse the element type (scalar)
    if (!dsdl_parse_type_scalar(parser, out_type)) {
        return false;
    }

    dsdl_parser_skip_ws(parser);

    // Check for array brackets
    if (dsdl_parser_peek(parser, 0) != '[') {
        return true; // Not an array, just a scalar - success
    }
    dsdl_parser_advance(parser, 1); // Skip '['
    dsdl_parser_skip_ws(parser);

    // Check for variable array indicators
    out_type->is_inclusive = false;
    bool is_variable       = false;

    if (dsdl_parser_accept(parser, "<=", 2)) {
        is_variable            = true;
        out_type->is_inclusive = true;
        dsdl_parser_skip_ws(parser);
    } else if (dsdl_parser_accept(parser, "<", 1)) {
        is_variable            = true;
        out_type->is_inclusive = false;
        dsdl_parser_skip_ws(parser);
    }

    // Parse array size expression
    dsdl_value_t size_val;
    if (!dsdl_parse_expression(parser, &size_val)) {
        return false;
    }

    if ((size_val.kind == dsdl_value_rational) && dsdl_rational_is_int(size_val.as.rational)) {
        uintmax_t capacity_um = 0U;
        if (!dsdl_rational_to_uintmax(size_val.as.rational, &capacity_um) || (capacity_um == 0U)) {
            dsdl_value_dispose(parser->dsdl, &size_val);
            return false;
        }
        if (capacity_um > UINT64_MAX) {
            dsdl_value_dispose(parser->dsdl, &size_val);
            return false;
        }
        uint64_t capacity = (uint64_t)capacity_um;
        if (is_variable && !out_type->is_inclusive) {
            if (capacity <= 1U) {
                dsdl_value_dispose(parser->dsdl, &size_val);
                return false;
            }
            capacity -= 1U;
        }
        out_type->array_size          = capacity;
        out_type->has_array_size_expr = false;
        dsdl_value_dispose(parser->dsdl, &size_val);
    } else if (size_val.kind == dsdl_value_deferred) {
        out_type->has_array_size_expr = true;
        out_type->array_size_expr     = size_val;
    } else {
        dsdl_value_dispose(parser->dsdl, &size_val);
        return false;
    }

    dsdl_parser_skip_ws(parser);

    // Expect closing bracket
    if (dsdl_parser_peek(parser, 0) != ']') {
        return false;
    }
    dsdl_parser_advance(parser, 1);

    // Save element type and update kind to array marker
    out_type->element_kind = out_type->kind;
    if (is_variable) {
        out_type->kind = DSDL_ARRAY_VARIABLE;
    } else {
        out_type->kind = DSDL_ARRAY_FIXED;
    }
    if (dsdl_type_is_void(out_type->element_kind)) {
        if (out_type->has_array_size_expr) {
            dsdl_value_dispose(parser->dsdl, &out_type->array_size_expr);
            out_type->has_array_size_expr = false;
        }
        return false;
    }

    return true;
}

/// Parse any type (the main entry point for type parsing).
static bool dsdl_parse_type(dsdl_parser_t* const parser, dsdl_parsed_type_t* const out_type)
{
    (void)memset(out_type, 0, sizeof(*out_type));
    if (!dsdl_parse_type_array(parser, out_type)) {
        return false;
    }
    DSDL_TRACE(parser->dsdl,
               "Parsed type: kind=0x%04x array_size=%" PRIu64 " variable=%d",
               out_type->kind,
               out_type->array_size,
               (out_type->kind == DSDL_ARRAY_VARIABLE));
    return true;
}

// ============================================================================
// Statement parsing
// ============================================================================

/// Statement kind enumeration.
typedef enum
{
    dsdl_stmt_none,
    dsdl_stmt_constant,       ///< type name = expression
    dsdl_stmt_field,          ///< type name
    dsdl_stmt_padding,        ///< void type (no name)
    dsdl_stmt_directive,      ///< @directive [expression]
    dsdl_stmt_service_marker, ///< ---
} dsdl_stmt_kind_t;

/// Parsed statement representation.
typedef struct
{
    dsdl_stmt_kind_t   kind;
    dsdl_parsed_type_t type;      ///< Type for CONSTANT, FIELD, PADDING
    wkv_str_t          name;      ///< Name for CONSTANT, FIELD, DIRECTIVE
    dsdl_value_t       value;     ///< Value for CONSTANT, optional for DIRECTIVE
    bool               has_value; ///< True if value is set (for DIRECTIVE)
} dsdl_parsed_stmt_t;

/// Parse a directive: @name [expression]
static bool dsdl_parse_directive(dsdl_parser_t* const parser, dsdl_parsed_stmt_t* const out_stmt)
{
    if (dsdl_parser_peek(parser, 0) != '@') {
        return false;
    }
    dsdl_parser_advance(parser, 1); // Skip '@'

    wkv_str_t name = dsdl_parse_identifier(parser);
    if (name.str == NULL) {
        return false;
    }

    out_stmt->kind      = dsdl_stmt_directive;
    out_stmt->name      = name;
    out_stmt->has_value = false;

    // Check for optional expression (requires whitespace separator)
    dsdl_parser_skip_ws(parser);

    // Try to parse expression (may fail if no expression follows)
    const size_t start_pos = parser->pos;
    if (dsdl_parse_expression(parser, &out_stmt->value)) {
        out_stmt->has_value = true;
    } else {
        parser->pos = start_pos; // Backtrack
    }

    return true;
}

/// Parse service response marker: ---+
static bool dsdl_parse_service_marker(dsdl_parser_t* const parser, dsdl_parsed_stmt_t* const out_stmt)
{
    // Must have at least three dashes
    if (!(dsdl_parser_peek(parser, 0) == '-' && dsdl_parser_peek(parser, 1) == '-' &&
          dsdl_parser_peek(parser, 2) == '-')) {
        return false;
    }

    // Consume all consecutive dashes
    while (dsdl_parser_peek(parser, 0) == '-') {
        dsdl_parser_advance(parser, 1);
    }

    out_stmt->kind = dsdl_stmt_service_marker;
    return true;
}

/// Parse a statement (attribute statement: constant, field, or padding).
static bool dsdl_parse_attribute_stmt(dsdl_parser_t* const parser, dsdl_parsed_stmt_t* const out_stmt)
{
    // Parse type
    if (!dsdl_parse_type(parser, &out_stmt->type)) {
        return false;
    }

    dsdl_parser_skip_ws(parser);

    // Check if this is a void (padding) field - no name allowed
    if (dsdl_type_is_void(out_stmt->type.kind) && !dsdl_type_is_array(out_stmt->type.kind)) {
        out_stmt->kind = dsdl_stmt_padding;
        return true;
    }

    // Parse name (required for non-padding fields)
    wkv_str_t name = dsdl_parse_identifier(parser);
    if (name.str == NULL) {
        return false;
    }

    out_stmt->name = name;
    dsdl_parser_skip_ws(parser);

    // Check for constant assignment
    if (dsdl_parser_peek(parser, 0) == '=') {
        dsdl_parser_advance(parser, 1); // Skip '='
        dsdl_parser_skip_ws(parser);

        if (!dsdl_parse_expression(parser, &out_stmt->value)) {
            return false;
        }

        out_stmt->kind      = dsdl_stmt_constant;
        out_stmt->has_value = true;
    } else {
        out_stmt->kind = dsdl_stmt_field;
    }

    return true;
}

/// Parse a statement (any kind).
static bool dsdl_parse_statement(dsdl_parser_t* const parser, dsdl_parsed_stmt_t* const out_stmt)
{
    assert((parser != NULL) && (out_stmt != NULL));
    (void)memset(out_stmt, 0, sizeof(*out_stmt));

    dsdl_parser_skip_ws(parser);

    // Empty line or comment-only line
    const char c = dsdl_parser_peek(parser, 0);
    if ((c == '\0') || (c == '\r') || (c == '\n') || (c == '#')) {
        out_stmt->kind = dsdl_stmt_none;
        return true;
    }

    // Try directive (@...)
    if (c == '@') {
        return dsdl_parse_directive(parser, out_stmt);
    }

    // Try service marker (---)
    if (c == '-') {
        return dsdl_parse_service_marker(parser, out_stmt);
    }

    // Otherwise it's an attribute statement (type name / type name = expr)
    return dsdl_parse_attribute_stmt(parser, out_stmt);
}

/// Parse a single line (statement + optional comment).
static bool dsdl_parse_line(dsdl_parser_t* const parser, dsdl_parsed_stmt_t* const out_stmt)
{
    assert((parser != NULL) && (out_stmt != NULL));
    // Parse statement
    if (!dsdl_parse_statement(parser, out_stmt)) {
        return false;
    }

    // Skip trailing whitespace and comment
    dsdl_parser_skip_line_tail(parser);

    return true;
}

// ============================================================================
// Definition parsing
// ============================================================================

/// Parsed definition intermediate representation.
/// Arrays are heap-allocated and grown as needed during parsing.
typedef struct dsdl_parsed_def_t dsdl_parsed_def_t;

typedef struct
{
    bool         has_extent;
    uint64_t     extent_bits;
    bool         has_extent_expr;
    dsdl_value_t extent_expr;
    bool         is_sealed;
    bool         is_union;
} dsdl_parsed_section_t;

struct dsdl_parsed_def_t
{
    dsdl_t* dsdl; ///< Parser context for memory allocation

    bool                is_service;  ///< True if service type (has request/response)
    size_t              field_count; ///< Number of fields in request (or struct/union)
    size_t              field_capacity;
    dsdl_parsed_type_t* field_types;
    wkv_str_t*          field_names;

    size_t              const_count; ///< Number of constants
    size_t              const_capacity;
    dsdl_value_t*       const_values;
    wkv_str_t*          const_names;
    dsdl_parsed_type_t* const_types;
    size_t*             const_field_idx;

    // For service types: response fields
    size_t              response_field_count;
    size_t              response_field_capacity;
    dsdl_parsed_type_t* response_field_types;
    wkv_str_t*          response_field_names;

    // For service types: response constants
    size_t              response_const_count;
    size_t              response_const_capacity;
    dsdl_value_t*       response_const_values;
    wkv_str_t*          response_const_names;
    dsdl_parsed_type_t* response_const_types;
    size_t*             response_const_field_idx;

    // Directives
    dsdl_parsed_section_t request;
    dsdl_parsed_section_t response;
    bool                  is_deprecated;
    bool                  has_fixed_port_id;
    bool                  has_fixed_port_id_expr;
    dsdl_value_t          fixed_port_id_expr;
    uint_least16_t        fixed_port_id;

    // Assertions (@assert directives)
    // Each assertion is stored with the field index at which it appeared,
    // so we can compute _offset_ at that point during semantic analysis.
    size_t        assert_count;
    size_t        assert_capacity;
    dsdl_value_t* assert_exprs;       ///< The assertion expressions
    size_t*       assert_field_idx;   ///< Field index when assertion appeared (for _offset_)
    bool*         assert_in_response; ///< True if assertion is in response section

    // Print expressions (@print directives with expression)
    size_t        print_count;
    size_t        print_capacity;
    dsdl_value_t* print_exprs;       ///< The print expressions
    size_t*       print_field_idx;   ///< Field index when print appeared (for _offset_)
    bool*         print_in_response; ///< True if print is in response section
};

// Forward declarations
static void dsdl_parsed_def_deinit(dsdl_parsed_def_t* const def);

/// Initialize parsed definition with heap-allocated arrays.
static bool dsdl_parsed_def_init(dsdl_parsed_def_t* const def, dsdl_t* const dsdl)
{
    (void)memset(def, 0, sizeof(*def));
    def->dsdl          = dsdl;
    def->fixed_port_id = DSDL_FIXED_PORT_ID_NONE;

    // Start with reasonable initial capacity
    const size_t initial_capacity = 16;

    def->field_capacity = initial_capacity;
    def->field_types    = (dsdl_parsed_type_t*)dsdl_alloc(dsdl, initial_capacity * sizeof(dsdl_parsed_type_t));
    def->field_names    = (wkv_str_t*)dsdl_alloc(dsdl, initial_capacity * sizeof(wkv_str_t));

    def->const_capacity  = initial_capacity;
    def->const_values    = (dsdl_value_t*)dsdl_alloc(dsdl, initial_capacity * sizeof(dsdl_value_t));
    def->const_names     = (wkv_str_t*)dsdl_alloc(dsdl, initial_capacity * sizeof(wkv_str_t));
    def->const_types     = (dsdl_parsed_type_t*)dsdl_alloc(dsdl, initial_capacity * sizeof(dsdl_parsed_type_t));
    def->const_field_idx = (size_t*)dsdl_alloc(dsdl, initial_capacity * sizeof(size_t));

    def->response_field_capacity = initial_capacity;
    def->response_field_types    = (dsdl_parsed_type_t*)dsdl_alloc(dsdl, initial_capacity * sizeof(dsdl_parsed_type_t));
    def->response_field_names    = (wkv_str_t*)dsdl_alloc(dsdl, initial_capacity * sizeof(wkv_str_t));

    def->response_const_capacity = initial_capacity;
    def->response_const_values   = (dsdl_value_t*)dsdl_alloc(dsdl, initial_capacity * sizeof(dsdl_value_t));
    def->response_const_names    = (wkv_str_t*)dsdl_alloc(dsdl, initial_capacity * sizeof(wkv_str_t));
    def->response_const_types    = (dsdl_parsed_type_t*)dsdl_alloc(dsdl, initial_capacity * sizeof(dsdl_parsed_type_t));
    def->response_const_field_idx = (size_t*)dsdl_alloc(dsdl, initial_capacity * sizeof(size_t));

    def->assert_capacity    = initial_capacity;
    def->assert_exprs       = (dsdl_value_t*)dsdl_alloc(dsdl, initial_capacity * sizeof(dsdl_value_t));
    def->assert_field_idx   = (size_t*)dsdl_alloc(dsdl, initial_capacity * sizeof(size_t));
    def->assert_in_response = (bool*)dsdl_alloc(dsdl, initial_capacity * sizeof(bool));

    def->print_capacity    = initial_capacity;
    def->print_exprs       = (dsdl_value_t*)dsdl_alloc(dsdl, initial_capacity * sizeof(dsdl_value_t));
    def->print_field_idx   = (size_t*)dsdl_alloc(dsdl, initial_capacity * sizeof(size_t));
    def->print_in_response = (bool*)dsdl_alloc(dsdl, initial_capacity * sizeof(bool));

    // Check if any allocation failed
    if ((def->field_types == NULL) || (def->field_names == NULL) || (def->const_values == NULL) ||
        (def->const_names == NULL) || (def->const_types == NULL) || (def->const_field_idx == NULL) ||
        (def->response_field_types == NULL) || (def->response_field_names == NULL) ||
        (def->response_const_values == NULL) || (def->response_const_names == NULL) ||
        (def->response_const_types == NULL) || (def->response_const_field_idx == NULL) || (def->assert_exprs == NULL) ||
        (def->assert_field_idx == NULL) || (def->assert_in_response == NULL) || (def->print_exprs == NULL) ||
        (def->print_field_idx == NULL) || (def->print_in_response == NULL)) {
        dsdl_parsed_def_deinit(def);
        return false;
    }

    return true;
}

/// Free all heap-allocated arrays in parsed definition.
static void dsdl_parsed_def_deinit(dsdl_parsed_def_t* const def)
{
    if (def->dsdl != NULL) {
        if (def->field_types != NULL) {
            for (size_t i = 0; i < def->field_count; i++) {
                if (def->field_types[i].has_array_size_expr) {
                    dsdl_value_dispose(def->dsdl, &def->field_types[i].array_size_expr);
                }
            }
        }
        if (def->response_field_types != NULL) {
            for (size_t i = 0; i < def->response_field_count; i++) {
                if (def->response_field_types[i].has_array_size_expr) {
                    dsdl_value_dispose(def->dsdl, &def->response_field_types[i].array_size_expr);
                }
            }
        }
        if (def->const_types != NULL) {
            for (size_t i = 0; i < def->const_count; i++) {
                if (def->const_types[i].has_array_size_expr) {
                    dsdl_value_dispose(def->dsdl, &def->const_types[i].array_size_expr);
                }
            }
        }
        if (def->response_const_types != NULL) {
            for (size_t i = 0; i < def->response_const_count; i++) {
                if (def->response_const_types[i].has_array_size_expr) {
                    dsdl_value_dispose(def->dsdl, &def->response_const_types[i].array_size_expr);
                }
            }
        }
        if (def->const_values != NULL) {
            for (size_t i = 0; i < def->const_count; i++) {
                dsdl_value_dispose(def->dsdl, &def->const_values[i]);
            }
        }
        if (def->response_const_values != NULL) {
            for (size_t i = 0; i < def->response_const_count; i++) {
                dsdl_value_dispose(def->dsdl, &def->response_const_values[i]);
            }
        }
        if (def->assert_exprs != NULL) {
            for (size_t i = 0; i < def->assert_count; i++) {
                dsdl_value_dispose(def->dsdl, &def->assert_exprs[i]);
            }
        }
        if (def->print_exprs != NULL) {
            for (size_t i = 0; i < def->print_count; i++) {
                dsdl_value_dispose(def->dsdl, &def->print_exprs[i]);
            }
        }
        if (def->request.has_extent_expr) {
            dsdl_value_dispose(def->dsdl, &def->request.extent_expr);
        }
        if (def->response.has_extent_expr) {
            dsdl_value_dispose(def->dsdl, &def->response.extent_expr);
        }
        if (def->has_fixed_port_id_expr) {
            dsdl_value_dispose(def->dsdl, &def->fixed_port_id_expr);
        }
        dsdl_free(def->dsdl, def->field_types);
        dsdl_free(def->dsdl, def->field_names);
        dsdl_free(def->dsdl, def->const_values);
        dsdl_free(def->dsdl, def->const_names);
        dsdl_free(def->dsdl, def->const_types);
        dsdl_free(def->dsdl, def->const_field_idx);
        dsdl_free(def->dsdl, def->response_field_types);
        dsdl_free(def->dsdl, def->response_field_names);
        dsdl_free(def->dsdl, def->response_const_values);
        dsdl_free(def->dsdl, def->response_const_names);
        dsdl_free(def->dsdl, def->response_const_types);
        dsdl_free(def->dsdl, def->response_const_field_idx);
        dsdl_free(def->dsdl, def->assert_exprs);
        dsdl_free(def->dsdl, def->assert_field_idx);
        dsdl_free(def->dsdl, def->assert_in_response);
        dsdl_free(def->dsdl, def->print_exprs);
        dsdl_free(def->dsdl, def->print_field_idx);
        dsdl_free(def->dsdl, def->print_in_response);
    }
    (void)memset(def, 0, sizeof(*def));
}

/// Ensure field array has capacity for at least one more element.
static bool dsdl_parsed_def_ensure_field_capacity(dsdl_parsed_def_t* const def)
{
    if (def->field_count >= def->field_capacity) {
        const size_t        new_capacity = def->field_capacity * 2;
        dsdl_parsed_type_t* new_types =
          (dsdl_parsed_type_t*)dsdl_realloc(def->dsdl, def->field_types, new_capacity * sizeof(dsdl_parsed_type_t));
        wkv_str_t* new_names = (wkv_str_t*)dsdl_realloc(def->dsdl, def->field_names, new_capacity * sizeof(wkv_str_t));

        if ((new_types == NULL) || (new_names == NULL)) {
            return false;
        }

        def->field_types    = new_types;
        def->field_names    = new_names;
        def->field_capacity = new_capacity;
    }
    return true;
}

/// Ensure const array has capacity for at least one more element.
static bool dsdl_parsed_def_ensure_const_capacity(dsdl_parsed_def_t* const def)
{
    if (def->const_count >= def->const_capacity) {
        const size_t  new_capacity = def->const_capacity * 2;
        dsdl_value_t* new_values =
          (dsdl_value_t*)dsdl_realloc(def->dsdl, def->const_values, new_capacity * sizeof(dsdl_value_t));
        wkv_str_t* new_names = (wkv_str_t*)dsdl_realloc(def->dsdl, def->const_names, new_capacity * sizeof(wkv_str_t));
        dsdl_parsed_type_t* new_types =
          (dsdl_parsed_type_t*)dsdl_realloc(def->dsdl, def->const_types, new_capacity * sizeof(dsdl_parsed_type_t));
        size_t* new_field_idx = (size_t*)dsdl_realloc(def->dsdl, def->const_field_idx, new_capacity * sizeof(size_t));

        if ((new_values == NULL) || (new_names == NULL) || (new_types == NULL) || (new_field_idx == NULL)) {
            return false;
        }

        def->const_values    = new_values;
        def->const_names     = new_names;
        def->const_types     = new_types;
        def->const_field_idx = new_field_idx;
        def->const_capacity  = new_capacity;
    }
    return true;
}

/// Ensure response const array has capacity for at least one more element.
static bool dsdl_parsed_def_ensure_response_const_capacity(dsdl_parsed_def_t* const def)
{
    if (def->response_const_count >= def->response_const_capacity) {
        const size_t  new_capacity = def->response_const_capacity * 2;
        dsdl_value_t* new_values =
          (dsdl_value_t*)dsdl_realloc(def->dsdl, def->response_const_values, new_capacity * sizeof(dsdl_value_t));
        wkv_str_t* new_names =
          (wkv_str_t*)dsdl_realloc(def->dsdl, def->response_const_names, new_capacity * sizeof(wkv_str_t));
        dsdl_parsed_type_t* new_types = (dsdl_parsed_type_t*)dsdl_realloc(
          def->dsdl, def->response_const_types, new_capacity * sizeof(dsdl_parsed_type_t));
        size_t* new_field_idx =
          (size_t*)dsdl_realloc(def->dsdl, def->response_const_field_idx, new_capacity * sizeof(size_t));

        if ((new_values == NULL) || (new_names == NULL) || (new_types == NULL) || (new_field_idx == NULL)) {
            return false;
        }

        def->response_const_values    = new_values;
        def->response_const_names     = new_names;
        def->response_const_types     = new_types;
        def->response_const_field_idx = new_field_idx;
        def->response_const_capacity  = new_capacity;
    }
    return true;
}

/// Ensure response field array has capacity for at least one more element.
static bool dsdl_parsed_def_ensure_response_capacity(dsdl_parsed_def_t* const def)
{
    if (def->response_field_count >= def->response_field_capacity) {
        const size_t        new_capacity = def->response_field_capacity * 2;
        dsdl_parsed_type_t* new_types    = (dsdl_parsed_type_t*)dsdl_realloc(
          def->dsdl, def->response_field_types, new_capacity * sizeof(dsdl_parsed_type_t));
        wkv_str_t* new_names =
          (wkv_str_t*)dsdl_realloc(def->dsdl, def->response_field_names, new_capacity * sizeof(wkv_str_t));

        if ((new_types == NULL) || (new_names == NULL)) {
            return false;
        }

        def->response_field_types    = new_types;
        def->response_field_names    = new_names;
        def->response_field_capacity = new_capacity;
    }
    return true;
}

/// Ensure assertion array has capacity for at least one more element.
static bool dsdl_parsed_def_ensure_assert_capacity(dsdl_parsed_def_t* const def)
{
    if (def->assert_count >= def->assert_capacity) {
        const size_t  new_capacity = def->assert_capacity * 2;
        dsdl_value_t* new_exprs =
          (dsdl_value_t*)dsdl_realloc(def->dsdl, def->assert_exprs, new_capacity * sizeof(dsdl_value_t));
        size_t* new_field_idx = (size_t*)dsdl_realloc(def->dsdl, def->assert_field_idx, new_capacity * sizeof(size_t));
        bool*   new_in_response = (bool*)dsdl_realloc(def->dsdl, def->assert_in_response, new_capacity * sizeof(bool));

        if ((new_exprs == NULL) || (new_field_idx == NULL) || (new_in_response == NULL)) {
            return false;
        }

        def->assert_exprs       = new_exprs;
        def->assert_field_idx   = new_field_idx;
        def->assert_in_response = new_in_response;
        def->assert_capacity    = new_capacity;
    }
    return true;
}

/// Ensure print array has capacity for at least one more element.
static bool dsdl_parsed_def_ensure_print_capacity(dsdl_parsed_def_t* const def)
{
    if (def->print_count >= def->print_capacity) {
        const size_t  new_capacity = def->print_capacity * 2;
        dsdl_value_t* new_exprs =
          (dsdl_value_t*)dsdl_realloc(def->dsdl, def->print_exprs, new_capacity * sizeof(dsdl_value_t));
        size_t* new_field_idx   = (size_t*)dsdl_realloc(def->dsdl, def->print_field_idx, new_capacity * sizeof(size_t));
        bool*   new_in_response = (bool*)dsdl_realloc(def->dsdl, def->print_in_response, new_capacity * sizeof(bool));

        if ((new_exprs == NULL) || (new_field_idx == NULL) || (new_in_response == NULL)) {
            return false;
        }

        def->print_exprs       = new_exprs;
        def->print_field_idx   = new_field_idx;
        def->print_in_response = new_in_response;
        def->print_capacity    = new_capacity;
    }
    return true;
}

static bool dsdl_name_collision(const wkv_str_t        name,
                                const wkv_str_t* const field_names,
                                const size_t           field_count,
                                const wkv_str_t* const const_names,
                                const size_t           const_count)
{
    if ((name.str == NULL) || (name.len == 0U)) {
        return false;
    }
    for (size_t i = 0; i < field_count; i++) {
        if ((field_names[i].len > 0U) && dsdl_str_equal(field_names[i], name)) {
            return true;
        }
    }
    for (size_t i = 0; i < const_count; i++) {
        if ((const_names[i].len > 0U) && dsdl_str_equal(const_names[i], name)) {
            return true;
        }
    }
    return false;
}

/// Parse a complete DSDL definition.
/// Expects out_def to be initialized with dsdl_parsed_def_init().
static bool dsdl_parse_definition(dsdl_parser_t* const parser, dsdl_parsed_def_t* const out_def)
{
    assert((parser != NULL) && (out_def != NULL) && (out_def->dsdl != NULL));
    DSDL_TRACE(parser->dsdl, "Starting definition parse at line %zu", parser->line);
    bool parsing_response = false;

    while (!dsdl_parser_eof(parser)) {
        dsdl_parsed_stmt_t stmt;
        if (!dsdl_parse_line(parser, &stmt)) {
            DSDL_TRACE(parser->dsdl, "Parse error at line %zu col %zu", parser->line, parser->col);
            return false; // Parse error
        }

        // Process statement based on kind
        switch (stmt.kind) {
            case dsdl_stmt_none:
                // Empty line or comment only - skip
                break;

            case dsdl_stmt_field:
            case dsdl_stmt_padding: {
                DSDL_TRACE(parser->dsdl,
                           "Field '%.*s' type=0x%04x at line %zu",
                           (int)stmt.name.len,
                           stmt.name.str ? stmt.name.str : "",
                           stmt.type.kind,
                           parser->line);
                dsdl_parsed_section_t* const section = parsing_response ? &out_def->response : &out_def->request;
                if (section->has_extent) {
                    return false; // Attributes cannot appear after @extent
                }
                if (stmt.kind == dsdl_stmt_field) {
                    const wkv_str_t* const field_names =
                      parsing_response ? out_def->response_field_names : out_def->field_names;
                    const size_t field_count = parsing_response ? out_def->response_field_count : out_def->field_count;
                    const wkv_str_t* const const_names =
                      parsing_response ? out_def->response_const_names : out_def->const_names;
                    const size_t const_count = parsing_response ? out_def->response_const_count : out_def->const_count;
                    if (dsdl_name_collision(stmt.name, field_names, field_count, const_names, const_count)) {
                        return false; // Duplicate attribute name
                    }
                    if (!dsdl_type_is_array(stmt.type.kind) &&
                        ((stmt.type.kind == DSDL_BYTE) || (stmt.type.kind == DSDL_UTF8))) {
                        return false; // byte/utf8 cannot be standalone
                    }
                    if (dsdl_type_is_array(stmt.type.kind) && (stmt.type.element_kind == DSDL_UTF8) &&
                        (stmt.type.kind != DSDL_ARRAY_VARIABLE)) {
                        return false; // utf8 must be in variable-length arrays
                    }
                }
                if (parsing_response) {
                    if (!dsdl_parsed_def_ensure_response_capacity(out_def)) {
                        return false; // OOM
                    }
                    const size_t idx                   = out_def->response_field_count++;
                    out_def->response_field_types[idx] = stmt.type;
                    out_def->response_field_names[idx] = stmt.name;
                } else {
                    if (!dsdl_parsed_def_ensure_field_capacity(out_def)) {
                        return false; // OOM
                    }
                    const size_t idx          = out_def->field_count++;
                    out_def->field_types[idx] = stmt.type;
                    out_def->field_names[idx] = stmt.name;
                }
                break;
            }

            case dsdl_stmt_constant: {
                DSDL_TRACE(
                  parser->dsdl, "Constant '%.*s' at line %zu", (int)stmt.name.len, stmt.name.str, parser->line);
                dsdl_parsed_section_t* const section = parsing_response ? &out_def->response : &out_def->request;
                if (section->has_extent) {
                    if (stmt.has_value) {
                        dsdl_value_dispose(parser->dsdl, &stmt.value);
                    }
                    return false; // Attributes cannot appear after @extent
                }
                const wkv_str_t* const field_names =
                  parsing_response ? out_def->response_field_names : out_def->field_names;
                const size_t field_count = parsing_response ? out_def->response_field_count : out_def->field_count;
                const wkv_str_t* const const_names =
                  parsing_response ? out_def->response_const_names : out_def->const_names;
                const size_t const_count = parsing_response ? out_def->response_const_count : out_def->const_count;
                if (dsdl_name_collision(stmt.name, field_names, field_count, const_names, const_count)) {
                    if (stmt.has_value) {
                        dsdl_value_dispose(parser->dsdl, &stmt.value);
                    }
                    return false; // Duplicate attribute name
                }
                if (!dsdl_type_is_array(stmt.type.kind) &&
                    ((stmt.type.kind == DSDL_BYTE) || (stmt.type.kind == DSDL_UTF8))) {
                    if (stmt.has_value) {
                        dsdl_value_dispose(parser->dsdl, &stmt.value);
                    }
                    return false; // byte/utf8 cannot be standalone
                }
                if (parsing_response) {
                    if (!dsdl_parsed_def_ensure_response_const_capacity(out_def)) {
                        if (stmt.has_value) {
                            dsdl_value_dispose(parser->dsdl, &stmt.value);
                        }
                        return false; // OOM
                    }
                    const size_t idx                       = out_def->response_const_count++;
                    out_def->response_const_types[idx]     = stmt.type;
                    out_def->response_const_names[idx]     = stmt.name;
                    out_def->response_const_values[idx]    = stmt.value;
                    out_def->response_const_field_idx[idx] = out_def->response_field_count;
                } else {
                    if (!dsdl_parsed_def_ensure_const_capacity(out_def)) {
                        if (stmt.has_value) {
                            dsdl_value_dispose(parser->dsdl, &stmt.value);
                        }
                        return false; // OOM
                    }
                    const size_t idx              = out_def->const_count++;
                    out_def->const_types[idx]     = stmt.type;
                    out_def->const_names[idx]     = stmt.name;
                    out_def->const_values[idx]    = stmt.value;
                    out_def->const_field_idx[idx] = out_def->field_count;
                }
                break;
            }

            case dsdl_stmt_service_marker:
                DSDL_TRACE(parser->dsdl, "Service marker at line %zu", parser->line);
                if (parsing_response) {
                    return false; // Duplicate service marker
                }
                out_def->is_service = true;
                parsing_response    = true;
                break;

            case dsdl_stmt_directive:
                DSDL_TRACE(
                  parser->dsdl, "Directive @%.*s at line %zu", (int)stmt.name.len, stmt.name.str, parser->line);
                dsdl_parsed_section_t* const section = parsing_response ? &out_def->response : &out_def->request;
                // Process known directives
                if ((stmt.name.len == 6) && (memcmp(stmt.name.str, "sealed", 6) == 0)) {
                    if (stmt.has_value) {
                        dsdl_value_dispose(parser->dsdl, &stmt.value);
                        return false; // @sealed does not accept expressions
                    }
                    if (section->is_sealed) {
                        return false; // Duplicate @sealed
                    }
                    section->is_sealed = true;
                } else if ((stmt.name.len == 6) && (memcmp(stmt.name.str, "extent", 6) == 0)) {
                    if (section->has_extent) {
                        if (stmt.has_value) {
                            dsdl_value_dispose(parser->dsdl, &stmt.value);
                        }
                        return false; // Duplicate @extent
                    }
                    if (!stmt.has_value) {
                        return false; // Invalid extent
                    }
                    if ((stmt.value.kind == dsdl_value_rational) && dsdl_rational_is_int(stmt.value.as.rational)) {
                        uintmax_t extent_um = 0U;
                        if (!dsdl_rational_to_uintmax(stmt.value.as.rational, &extent_um) || (extent_um > UINT64_MAX)) {
                            dsdl_value_dispose(parser->dsdl, &stmt.value);
                            return false;
                        }
                        section->has_extent  = true;
                        section->extent_bits = (uint64_t)extent_um;
                        dsdl_value_dispose(parser->dsdl, &stmt.value);
                    } else if (stmt.value.kind == dsdl_value_deferred) {
                        section->has_extent_expr = true;
                        section->has_extent      = true;
                        section->extent_expr     = stmt.value;
                    } else {
                        if (stmt.has_value) {
                            dsdl_value_dispose(parser->dsdl, &stmt.value);
                        }
                        return false; // Invalid extent
                    }
                } else if ((stmt.name.len == 10) && (memcmp(stmt.name.str, "deprecated", 10) == 0)) {
                    if (parsing_response || out_def->is_deprecated ||
                        (!parsing_response && ((out_def->field_count > 0U) || (out_def->const_count > 0U)))) {
                        if (stmt.has_value) {
                            dsdl_value_dispose(parser->dsdl, &stmt.value);
                        }
                        return false; // @deprecated must be first and only in request
                    }
                    if (stmt.has_value) {
                        dsdl_value_dispose(parser->dsdl, &stmt.value);
                        return false; // @deprecated does not accept expressions
                    }
                    out_def->is_deprecated = true;
                } else if ((stmt.name.len == 5) && (memcmp(stmt.name.str, "union", 5) == 0)) {
                    if (stmt.has_value) {
                        dsdl_value_dispose(parser->dsdl, &stmt.value);
                        return false; // @union does not accept expressions
                    }
                    if (section->is_union) {
                        return false; // Duplicate @union
                    }
                    const size_t field_count = parsing_response ? out_def->response_field_count : out_def->field_count;
                    const size_t const_count = parsing_response ? out_def->response_const_count : out_def->const_count;
                    if ((field_count > 0U) || (const_count > 0U)) {
                        return false; // @union must appear before fields/constants
                    }
                    section->is_union = true;
                } else if ((stmt.name.len == 13) && (memcmp(stmt.name.str, "fixed-port-id", 13) == 0)) {
                    if (!stmt.has_value) {
                        return false;
                    }
                    if (parsing_response) {
                        dsdl_value_dispose(parser->dsdl, &stmt.value);
                        return false;
                    }
                    if (out_def->has_fixed_port_id) {
                        dsdl_value_dispose(parser->dsdl, &stmt.value);
                        return false;
                    }
                    out_def->has_fixed_port_id = true;
                    if ((stmt.value.kind == dsdl_value_rational) && dsdl_rational_is_int(stmt.value.as.rational)) {
                        uintmax_t port_id = 0U;
                        if (!dsdl_rational_to_uintmax(stmt.value.as.rational, &port_id) ||
                            (port_id >= (uintmax_t)DSDL_FIXED_PORT_ID_NONE)) {
                            dsdl_value_dispose(parser->dsdl, &stmt.value);
                            return false;
                        }
                        out_def->fixed_port_id = (uint_least16_t)port_id;
                        dsdl_value_dispose(parser->dsdl, &stmt.value);
                    } else if (stmt.value.kind == dsdl_value_deferred) {
                        out_def->has_fixed_port_id_expr = true;
                        out_def->fixed_port_id_expr     = stmt.value;
                    } else {
                        dsdl_value_dispose(parser->dsdl, &stmt.value);
                        return false;
                    }
                } else if ((stmt.name.len == 6) && (memcmp(stmt.name.str, "assert", 6) == 0)) {
                    // Store assertion for later validation during semantic analysis
                    if (!stmt.has_value) {
                        return false; // @assert requires an expression
                    }
                    if (!dsdl_parsed_def_ensure_assert_capacity(out_def)) {
                        dsdl_value_dispose(parser->dsdl, &stmt.value);
                        return false; // OOM
                    }
                    const size_t idx           = out_def->assert_count++;
                    out_def->assert_exprs[idx] = stmt.value;
                    // Store current field index so we can compute _offset_ at this point
                    out_def->assert_field_idx[idx] =
                      parsing_response ? out_def->response_field_count : out_def->field_count;
                    out_def->assert_in_response[idx] = parsing_response;
                } else if ((stmt.name.len == 5) && (memcmp(stmt.name.str, "print", 5) == 0)) {
                    if (stmt.has_value) {
                        if (!dsdl_parsed_def_ensure_print_capacity(out_def)) {
                            dsdl_value_dispose(parser->dsdl, &stmt.value);
                            return false; // OOM
                        }
                        const size_t idx          = out_def->print_count++;
                        out_def->print_exprs[idx] = stmt.value;
                        out_def->print_field_idx[idx] =
                          parsing_response ? out_def->response_field_count : out_def->field_count;
                        out_def->print_in_response[idx] = parsing_response;
                    }
                } else {
                    if (stmt.has_value) {
                        dsdl_value_dispose(parser->dsdl, &stmt.value);
                    }
                    return false; // Unknown directive
                }
                break;

            default:
                break;
        }

        // Skip end-of-line
        (void)dsdl_parser_skip_eol(parser);
    }

    return true;
}

// ============================================================================
// Public API implementation
// ============================================================================

static void* wkv_realloc_adapter(wkv_t* const self, void* const ptr, const size_t new_size)
{
    dsdl_t* const owner = self->context;
    return owner->realloc(owner, ptr, new_size);
}

void dsdl_new(dsdl_t* const self, void* (*const realloc_func)(dsdl_t*, void*, size_t))
{
    assert((self != NULL) && (realloc_func != NULL));
    (void)memset(self, 0, sizeof(*self));
    self->realloc = realloc_func;

    wkv_init(&self->types, wkv_realloc_adapter);
    self->types.sep     = '.';
    self->types.context = self;

    self->namespace_count = 0;
    self->namespaces      = NULL;
}

static void dsdl_composite_cleanup(dsdl_t* const self, dsdl_type_composite_t* const composite)
{
    if ((self == NULL) || (composite == NULL)) {
        return;
    }
    if (composite->response != NULL) {
        dsdl_composite_cleanup(self, composite->response);
        dsdl_free(self, composite->response);
        composite->response = NULL;
    }
    if (composite->constant_values != NULL) {
        for (size_t i = 0; i < composite->constant_count; i++) {
            dsdl_value_dispose(self, &composite->constant_values[i]);
        }
    }
}

void dsdl_destroy(dsdl_t* const self)
{
    if (self == NULL) {
        return;
    }

    // Free all type definitions stored in the types WKV
    // TODO: iterate and free each composite type

    // Free WKV internal allocations
    while (!wkv_is_empty(&self->types)) {
        wkv_node_t* const node = wkv_at(&self->types, 0);
        if (node != NULL) {
            // Free the type data if present
            if (node->value != NULL) {
                dsdl_composite_cleanup(self, (dsdl_type_composite_t*)node->value);
                dsdl_free(self, node->value);
            }
            wkv_del(&self->types, node);
        }
    }

    // Free namespace strings and array
    for (size_t i = 0; i < self->namespace_count; i++) {
        // Each namespace string was allocated separately (cast away const - we own this memory)
        dsdl_free(self, (void*)(uintptr_t)self->namespaces[i].str);
    }
    dsdl_free(self, self->namespaces);
    self->namespaces      = NULL;
    self->namespace_count = 0;

    // Free tracked type descriptor allocations
    dsdl_type_alloc_t* alloc = self->type_allocations;
    while (alloc != NULL) {
        dsdl_type_alloc_t* const next = alloc->next;
        dsdl_free(self, alloc);
        alloc = next;
    }
    self->type_allocations = NULL;

    // Free tracked BLS allocations
    dsdl_bls_t* bls = self->bls_allocations;
    while (bls != NULL) {
        dsdl_bls_t* const next = bls->next_alloc;
        dsdl_free(self, bls);
        bls = next;
    }
    self->bls_allocations = NULL;
}

// ============================================================================
// Type Resolution Helpers
// ============================================================================

/// Parsed type name components
typedef struct
{
    wkv_str_t     full_name;      ///< Full name including namespace (e.g., "uavcan.node.Heartbeat")
    wkv_str_t     namespace_part; ///< Namespace portion (e.g., "uavcan.node")
    wkv_str_t     type_name;      ///< Just the type name (e.g., "Heartbeat")
    uint_least8_t major;          ///< Major version
    uint_least8_t minor;          ///< Minor version
    bool          has_major;      ///< True if major version was specified
    bool          has_minor;      ///< True if minor version was specified
} dsdl_type_ref_t;

/// Check if a string consists entirely of decimal digits.
static bool dsdl_is_all_digits(const wkv_str_t str)
{
    if ((str.str == NULL) || (str.len == 0)) {
        return false;
    }

    for (size_t i = 0; i < str.len; i++) {
        if (!dsdl_is_digit(str.str[i])) {
            return false;
        }
    }

    return true;
}

static wkv_str_t dsdl_short_name_from_full(const wkv_str_t full_name)
{
    wkv_str_t short_name = full_name;
    if ((full_name.str == NULL) || (full_name.len == 0)) {
        return short_name;
    }
    size_t start = 0;
    for (size_t i = 0; i < full_name.len; i++) {
        if (full_name.str[i] == '.') {
            start = i + 1U;
        }
    }
    short_name.str = full_name.str + start;
    short_name.len = full_name.len - start;
    return short_name;
}

static bool dsdl_is_identifier(const wkv_str_t str)
{
    if ((str.str == NULL) || (str.len == 0U)) {
        return false;
    }
    if (!dsdl_is_ident_start(str.str[0])) {
        return false;
    }
    for (size_t i = 1U; i < str.len; i++) {
        if (!dsdl_is_ident_cont(str.str[i])) {
            return false;
        }
    }
    return true;
}

/// Parse a decimal integer from a string (simple non-negative version).
/// Returns -1 on error.
static int64_t dsdl_parse_int(const wkv_str_t str)
{
    if (!dsdl_is_all_digits(str)) {
        return -1;
    }

    int64_t result = 0;
    for (size_t i = 0; i < str.len; i++) {
        const int64_t digit = str.str[i] - '0';
        // Check for overflow
        if (result > (INT64_MAX - digit) / 10) {
            return -1;
        }
        result = result * 10 + digit;
    }

    return result;
}

/// Parse a type name string into components.
///
/// Handles forms:
///   - "TypeName" → no namespace, no version
///   - "TypeName.1" → no namespace, major only
///   - "TypeName.1.0" → no namespace, full version
///   - "namespace.TypeName" → namespace, no version
///   - "namespace.TypeName.1" → namespace, major only
///   - "namespace.TypeName.1.0" → namespace, full version
///
/// @param name  Type name string
/// @param out   Output structure to populate
/// @return true on success, false if malformed
static bool dsdl_parse_typename(const wkv_str_t name, dsdl_type_ref_t* const out)
{
    if ((name.str == NULL) || (name.len == 0) || (out == NULL)) {
        return false;
    }

    memset(out, 0, sizeof(*out));

    // Find all '.' separators
    size_t dot_positions[16];
    size_t dot_count = 0;

    for (size_t i = 0; i < name.len && dot_count < 16; i++) {
        if (name.str[i] == '.') {
            dot_positions[dot_count++] = i;
        }
    }

    if (dot_count == 0) {
        // Just "TypeName"
        out->type_name      = name;
        out->namespace_part = (wkv_str_t){ 0, NULL };
        return true;
    }

    // Try to parse version numbers from the end
    size_t version_dots = 0; // How many trailing dots are version separators

    // Check for major.minor format (two trailing numbers)
    if (dot_count >= 2) {
        const size_t    last_dot       = dot_positions[dot_count - 1];
        const wkv_str_t last_component = { name.len - last_dot - 1, name.str + last_dot + 1 };

        if (dsdl_is_all_digits(last_component)) {
            const size_t    prev_dot       = dot_positions[dot_count - 2];
            const wkv_str_t prev_component = { last_dot - prev_dot - 1, name.str + prev_dot + 1 };

            if (dsdl_is_all_digits(prev_component)) {
                // Both are numbers, so: major.minor
                const int64_t major_val = dsdl_parse_int(prev_component);
                const int64_t minor_val = dsdl_parse_int(last_component);

                if ((major_val >= 0) && (major_val <= 255) && (minor_val >= 0) && (minor_val <= 255)) {
                    out->major     = (uint_least8_t)major_val;
                    out->minor     = (uint_least8_t)minor_val;
                    out->has_major = true;
                    out->has_minor = true;
                    version_dots   = 2;
                }
            }
        }
    }

    // Check for major-only version (single trailing number)
    if ((version_dots == 0) && (dot_count >= 1)) {
        const size_t    last_dot       = dot_positions[dot_count - 1];
        const wkv_str_t last_component = { name.len - last_dot - 1, name.str + last_dot + 1 };

        if (dsdl_is_all_digits(last_component)) {
            const int64_t major_val = dsdl_parse_int(last_component);
            if ((major_val >= 0) && (major_val <= 255)) {
                out->major     = (uint_least8_t)major_val;
                out->has_major = true;
                version_dots   = 1;
            }
        }
    }

    // Extract type name and namespace
    if (dot_count > version_dots) {
        // There's a namespace part
        const size_t type_name_dot = dot_positions[dot_count - version_dots - 1];
        out->namespace_part        = (wkv_str_t){ type_name_dot, name.str };
        out->type_name = (wkv_str_t){ (version_dots > 0) ? (dot_positions[dot_count - version_dots] - type_name_dot - 1)
                                                         : (name.len - type_name_dot - 1),
                                      name.str + type_name_dot + 1 };
    } else {
        // No namespace, just type name (and maybe version)
        out->namespace_part = (wkv_str_t){ 0, NULL };
        out->type_name = (version_dots > 0) ? ((wkv_str_t){ dot_positions[dot_count - version_dots], name.str }) : name;
    }

    // Construct full_name (without version)
    if (out->namespace_part.len > 0) {
        out->full_name = (wkv_str_t){ out->namespace_part.len + 1 + out->type_name.len, name.str };
    } else {
        out->full_name = out->type_name;
    }

    return true;
}

/// Parsed DSDL filename components.
/// Format: [port_id.]TypeName.major.minor.dsdl
typedef struct
{
    wkv_str_t      type_name;     ///< e.g., "Heartbeat"
    uint_least8_t  major;         ///< Major version
    uint_least8_t  minor;         ///< Minor version
    uint_least16_t fixed_port_id; ///< DSDL_FIXED_PORT_ID_NONE if not present
    bool           valid;         ///< True if parsing succeeded
} dsdl_parsed_filename_t;

/// Parse a DSDL filename into its components.
/// Handles format: [port_id.]TypeName.major.minor.dsdl
///
/// Examples:
///   - "Heartbeat.1.0.dsdl" → type_name="Heartbeat", major=1, minor=0, fixed_port_id=NONE
///   - "7000.FixedPortMessage.1.0.dsdl" → type_name="FixedPortMessage", major=1, minor=0, fixed_port_id=7000
///
/// @param filename  Filename to parse (without directory path)
/// @return Parsed components with valid=true on success, valid=false on failure
static dsdl_parsed_filename_t dsdl_parse_filename(const wkv_str_t filename)
{
    dsdl_parsed_filename_t result = { { 0, NULL }, 0, 0, DSDL_FIXED_PORT_ID_NONE, false };

    // Minimum valid filename: "T.0.0.dsdl" = 10 chars
    if ((filename.str == NULL) || (filename.len < 10)) {
        return result;
    }

    // Check .dsdl suffix
    if ((filename.len < 5) || (filename.str[filename.len - 5] != '.') || (filename.str[filename.len - 4] != 'd') ||
        (filename.str[filename.len - 3] != 's') || (filename.str[filename.len - 2] != 'd') ||
        (filename.str[filename.len - 1] != 'l')) {
        return result;
    }

    // Work with the part before ".dsdl"
    const size_t base_len = filename.len - 5;

    // Find all '.' positions
    size_t dot_positions[16];
    size_t dot_count = 0;

    for (size_t i = 0; (i < base_len) && (dot_count < 16); i++) {
        if (filename.str[i] == '.') {
            dot_positions[dot_count++] = i;
        }
    }

    // Need at least 2 dots for TypeName.major.minor
    if (dot_count < 2) {
        return result;
    }

    // Parse minor version (last component before .dsdl)
    const size_t    minor_start = dot_positions[dot_count - 1] + 1;
    const wkv_str_t minor_str   = { base_len - minor_start, filename.str + minor_start };
    if ((minor_str.len == 0) || !dsdl_is_all_digits(minor_str)) {
        return result;
    }
    const int64_t minor_val = dsdl_parse_int(minor_str);
    if ((minor_val < 0) || (minor_val > 255)) {
        return result;
    }

    // Parse major version (second-to-last component)
    const size_t major_start = dot_positions[dot_count - 2] + 1;
    const size_t major_end   = dot_positions[dot_count - 1];
    if (major_start >= major_end) {
        return result;
    }
    const wkv_str_t major_str = { major_end - major_start, filename.str + major_start };
    if ((major_str.len == 0) || !dsdl_is_all_digits(major_str)) {
        return result;
    }
    const int64_t major_val = dsdl_parse_int(major_str);
    if ((major_val < 0) || (major_val > 255)) {
        return result;
    }

    result.major = (uint_least8_t)major_val;
    result.minor = (uint_least8_t)minor_val;

    // Now handle the part before major.minor
    // It could be: "TypeName" or "port_id.TypeName"
    if (dot_count == 2) {
        // Format: TypeName.major.minor.dsdl
        result.type_name = (wkv_str_t){ dot_positions[0], filename.str };
        if (result.type_name.len == 0) {
            return result;
        }
    } else {
        // At least 3 dots: could be port_id.TypeName.major.minor.dsdl
        // Or namespace like: sub.Type.major.minor.dsdl (but we don't expect namespaces in filename)
        // Check if first component is all digits (port ID)
        const wkv_str_t first_component = { dot_positions[0], filename.str };

        if (dsdl_is_all_digits(first_component) && (first_component.len > 0)) {
            // First component is port ID
            const int64_t port_val = dsdl_parse_int(first_component);
            if ((port_val >= 0) && (port_val <= 65534)) {
                result.fixed_port_id = (uint_least16_t)port_val;

                // Type name is between first dot and major dot
                const size_t name_start = dot_positions[0] + 1;
                const size_t name_end   = dot_positions[dot_count - 2];
                if (name_start >= name_end) {
                    return result;
                }
                result.type_name = (wkv_str_t){ name_end - name_start, filename.str + name_start };
            } else {
                return result; // Invalid port ID
            }
        } else {
            // First component is not a port ID - type name may contain dots (unusual but handle it)
            // Type name is everything before major.minor
            result.type_name = (wkv_str_t){ dot_positions[dot_count - 2], filename.str };
        }
    }

    if (!dsdl_is_identifier(result.type_name)) {
        return result;
    }

    result.valid = true;
    return result;
}

/// Construct the directory path for a type's namespace.
/// Returns the length of the constructed path, or 0 on error.
static size_t dsdl_build_dir_path(const char*     namespace_root,
                                  const size_t    namespace_root_len,
                                  const wkv_str_t namespace_part,
                                  char*           out_path,
                                  const size_t    path_capacity)
{
    size_t pos = 0;

    // Add namespace root
    if ((pos + namespace_root_len) >= path_capacity) {
        return 0;
    }
    memcpy(out_path + pos, namespace_root, namespace_root_len);
    pos += namespace_root_len;

    // Add separator if needed
    if ((namespace_root_len > 0) && (namespace_root[namespace_root_len - 1] != DSDL_PATH_SEP)) {
        if ((pos + 1) >= path_capacity) {
            return 0;
        }
        out_path[pos++] = DSDL_PATH_SEP;
    }

    // Add namespace part (with dots replaced by path separators)
    if (namespace_part.len > 0) {
        if ((pos + namespace_part.len) >= path_capacity) {
            return 0;
        }
        for (size_t i = 0; i < namespace_part.len; i++) {
            out_path[pos++] = (namespace_part.str[i] == '.') ? DSDL_PATH_SEP : namespace_part.str[i];
        }

        // Add trailing separator
        if ((pos + 1) >= path_capacity) {
            return 0;
        }
        out_path[pos++] = DSDL_PATH_SEP;
    }

    out_path[pos] = '\0';
    return pos;
}

/// Locate a DSDL file in registered namespace roots using directory listing.
///
/// Constructs the filesystem path from the type name and searches all registered
/// namespace directories. Handles version resolution by listing directory contents.
///
/// @param self              Parser state with registered namespaces
/// @param type_ref          Parsed type reference with namespace, name, and version
/// @param out_path          Output buffer for the full file path (caller-allocated)
/// @param path_capacity     Size of out_path buffer
/// @param out_fixed_port_id Output for fixed port-ID from filename (can be NULL)
/// @param out_major         Output for resolved major version (can be NULL)
/// @param out_minor         Output for resolved minor version (can be NULL)
/// @return true if file found, false otherwise
static bool dsdl_locate_file(dsdl_t* const          self,
                             const dsdl_type_ref_t* type_ref,
                             char*                  out_path,
                             const size_t           path_capacity,
                             uint_least16_t*        out_fixed_port_id,
                             uint_least8_t*         out_major,
                             uint_least8_t*         out_minor)
{
    if ((self == NULL) || (type_ref == NULL) || (out_path == NULL) || (path_capacity == 0)) {
        return false;
    }

    if (out_fixed_port_id != NULL) {
        *out_fixed_port_id = DSDL_FIXED_PORT_ID_NONE;
    }
    if (out_major != NULL) {
        *out_major = 0;
    }
    if (out_minor != NULL) {
        *out_minor = 0;
    }

    DSDL_TRACE(self,
               "Locating file for '%.*s.%.*s' version=%d.%d (has_major=%d, has_minor=%d)",
               (int)type_ref->namespace_part.len,
               type_ref->namespace_part.str,
               (int)type_ref->type_name.len,
               type_ref->type_name.str,
               type_ref->major,
               type_ref->minor,
               type_ref->has_major,
               type_ref->has_minor);

    // Iterate through registered namespace roots in order (first match wins)
    for (size_t ns_idx = 0; ns_idx < self->namespace_count; ns_idx++) {
        const wkv_str_t*  ns                 = &self->namespaces[ns_idx];
        const char* const namespace_root     = ns->str;
        const size_t      namespace_root_len = ns->len;

        // Build directory path for this namespace
        char   dir_path[DSDL_PATH_MAX];
        size_t dir_len =
          dsdl_build_dir_path(namespace_root, namespace_root_len, type_ref->namespace_part, dir_path, sizeof(dir_path));
        if (dir_len == 0) {
            continue;
        }

        // Use list() for version resolution and port-ID discovery
        wkv_str_t* entries = self->list(self, (wkv_str_t){ dir_len, dir_path });
        if (entries != NULL) {
            // Track best matching version
            bool          found      = false;
            uint_least8_t best_major = 0;
            uint_least8_t best_minor = 0;
            size_t        best_idx   = 0;

            // Iterate through directory entries
            for (size_t i = 0; entries[i].str != NULL; i++) {
                const dsdl_parsed_filename_t parsed = dsdl_parse_filename(entries[i]);
                if (!parsed.valid) {
                    continue; // Skip invalid filenames
                }

                // Check if type name matches
                if ((parsed.type_name.len != type_ref->type_name.len) ||
                    (memcmp(parsed.type_name.str, type_ref->type_name.str, parsed.type_name.len) != 0)) {
                    continue; // Type name doesn't match
                }

                // Version matching logic
                bool version_match = false;

                if (type_ref->has_major && type_ref->has_minor) {
                    // Exact version required
                    version_match = (parsed.major == type_ref->major) && (parsed.minor == type_ref->minor);
                } else if (type_ref->has_major) {
                    // Major must match, pick highest minor
                    if (parsed.major == type_ref->major) {
                        if (!found || (parsed.minor > best_minor)) {
                            version_match = true;
                        }
                    }
                } else {
                    // No version constraint, pick highest major.minor
                    if (!found || (parsed.major > best_major) ||
                        ((parsed.major == best_major) && (parsed.minor > best_minor))) {
                        version_match = true;
                    }
                }

                if (version_match) {
                    found      = true;
                    best_major = parsed.major;
                    best_minor = parsed.minor;
                    best_idx   = i;
                }
            }

            if (found) {
                // Construct full path
                const wkv_str_t* best_filename = &entries[best_idx];
                if ((dir_len + best_filename->len + 1) <= path_capacity) {
                    memcpy(out_path, dir_path, dir_len);
                    memcpy(out_path + dir_len, best_filename->str, best_filename->len);
                    out_path[dir_len + best_filename->len] = '\0';

                    // Extract fixed port-ID and set resolved version
                    if (out_fixed_port_id != NULL) {
                        const dsdl_parsed_filename_t best_parsed = dsdl_parse_filename(*best_filename);
                        *out_fixed_port_id                       = best_parsed.fixed_port_id;
                    }
                    if (out_major != NULL) {
                        *out_major = best_major;
                    }
                    if (out_minor != NULL) {
                        *out_minor = best_minor;
                    }

                    // Free all entries
                    for (size_t i = 0; entries[i].str != NULL; i++) {
                        dsdl_free_str(self, entries[i].str);
                    }
                    dsdl_free(self, entries);

                    DSDL_TRACE(self,
                               "Found: '%s' (port_id=%u, v%u.%u)",
                               out_path,
                               out_fixed_port_id ? *out_fixed_port_id : DSDL_FIXED_PORT_ID_NONE,
                               best_major,
                               best_minor);
                    return true;
                }
            }

            // Free all entries
            for (size_t i = 0; entries[i].str != NULL; i++) {
                dsdl_free_str(self, entries[i].str);
            }
            dsdl_free(self, entries);
        }
    }

    return false; // Not found in any namespace root
}

/// Resolve a composite type reference, loading it if necessary.
/// Returns NULL if the type cannot be found or loaded.
static const dsdl_type_composite_t* dsdl_resolve_composite_type(dsdl_t* const       self,
                                                                const wkv_str_t     type_name,
                                                                const uint_least8_t version_major,
                                                                const uint_least8_t version_minor,
                                                                const wkv_str_t     current_namespace)
{
    assert((self != NULL) && (type_name.str != NULL));
    DSDL_TRACE(self,
               "Resolving composite type '%.*s.%d.%d' (namespace='%.*s')",
               (int)type_name.len,
               type_name.str,
               version_major,
               version_minor,
               (int)current_namespace.len,
               current_namespace.str);

    // Build fully qualified type name with version
    char   full_name[DSDL_PATH_MAX];
    size_t pos = 0;

    // If type_name doesn't contain a dot, it's relative to current_namespace
    bool has_namespace = false;
    for (size_t i = 0; i < type_name.len; i++) {
        if (type_name.str[i] == '.') {
            has_namespace = true;
            break;
        }
    }

    if (!has_namespace && (current_namespace.len > 0)) {
        // Prepend current namespace
        if ((pos + current_namespace.len + 1) >= sizeof(full_name)) {
            return NULL;
        }
        memcpy(full_name + pos, current_namespace.str, current_namespace.len);
        pos += current_namespace.len;
        full_name[pos++] = '.';
    }

    // Add type name
    if ((pos + type_name.len) >= sizeof(full_name)) {
        return NULL;
    }
    memcpy(full_name + pos, type_name.str, type_name.len);
    pos += type_name.len;

    // Add version
    if ((pos + 10) >= sizeof(full_name)) {
        return NULL;
    }
    full_name[pos++] = '.';
    if (version_major >= 100)
        full_name[pos++] = (char)('0' + (version_major / 100));
    if (version_major >= 10)
        full_name[pos++] = (char)('0' + ((version_major / 10) % 10));
    full_name[pos++] = (char)('0' + (version_major % 10));
    full_name[pos++] = '.';
    if (version_minor >= 100)
        full_name[pos++] = (char)('0' + (version_minor / 100));
    if (version_minor >= 10)
        full_name[pos++] = (char)('0' + ((version_minor / 10) % 10));
    full_name[pos++] = (char)('0' + (version_minor % 10));
    full_name[pos]   = '\0';

    // Try to load the type
    return dsdl_read(self, (wkv_str_t){ pos, full_name });
}

/// Create a type descriptor from a parsed type, resolving composite references.
/// Returns a pointer to the allocated type descriptor, or NULL on error.
/// The returned pointer can be cast to dsdl_type_t* to read the type discriminator,
/// then cast to the appropriate concrete type (dsdl_type_array_t*, dsdl_type_composite_t*, etc.).
static dsdl_type_t* dsdl_create_type_descriptor(dsdl_t* const             self,
                                                const dsdl_parsed_type_t* parsed_type,
                                                const wkv_str_t           current_namespace,
                                                const bool                allow_deprecated)
{
    DSDL_TRACE(self, "Creating type descriptor for kind=0x%04x", parsed_type->kind);

    // For primitives, void, and aliases: allocate a single dsdl_type_t
    if (!dsdl_type_is_array(parsed_type->kind) && !dsdl_type_is_composite(parsed_type->kind)) {
        DSDL_TRACE(self, "  -> primitive type, width=%d bits", dsdl_type_bit_width(parsed_type->kind));
        dsdl_type_t* type_ptr = (dsdl_type_t*)dsdl_type_alloc(self, sizeof(dsdl_type_t));
        if (type_ptr == NULL) {
            return NULL;
        }
        *type_ptr = parsed_type->kind;
        return type_ptr;
    }

    // For arrays: allocate dsdl_type_array_t and recursively create element type
    if (dsdl_type_is_array(parsed_type->kind)) {
        DSDL_TRACE(self,
                   "  -> array type, capacity=%" PRIu64 ", variable=%d",
                   parsed_type->array_size,
                   (parsed_type->kind == DSDL_ARRAY_VARIABLE));
        dsdl_type_array_t* arr = (dsdl_type_array_t*)dsdl_type_alloc(self, sizeof(dsdl_type_array_t));
        if (arr == NULL) {
            return NULL;
        }
        (void)memset(arr, 0, sizeof(*arr));
        arr->type     = parsed_type->kind;
        arr->capacity = parsed_type->array_size;

        // Create a temporary parsed_type for the element (strip array info)
        dsdl_parsed_type_t element_type = *parsed_type;
        element_type.kind               = parsed_type->element_kind;
        element_type.array_size         = 0;
        element_type.is_inclusive       = false;

        arr->member_type = dsdl_create_type_descriptor(self, &element_type, current_namespace, allow_deprecated);
        if (arr->member_type == NULL) {
            return NULL;
        }
        return &arr->type;
    }

    // For composite types: resolve and return pointer to the cached type
    if (dsdl_type_is_composite(parsed_type->kind)) {
        DSDL_TRACE(self,
                   "  -> composite type '%.*s.%d.%d'",
                   (int)parsed_type->type_name.len,
                   parsed_type->type_name.str,
                   parsed_type->version_major,
                   parsed_type->version_minor);
        const dsdl_type_composite_t* composite = dsdl_resolve_composite_type(
          self, parsed_type->type_name, parsed_type->version_major, parsed_type->version_minor, current_namespace);
        if (composite == NULL) {
            DSDL_TRACE(self, "  -> FAILED to resolve composite type");
            return NULL; // Failed to resolve
        }
        if (composite->deprecated && !allow_deprecated) {
            return NULL; // Non-deprecated type cannot use deprecated types
        }
        // Return pointer to the composite's type field (first field of the struct).
        // The const cast is safe because all type data is owned by dsdl_t and
        // the field_types array is for reading, not modification.
        return (dsdl_type_t*)(uintptr_t)composite;
    }

    return NULL;
}

bool dsdl_add_namespace(dsdl_t* const self, const wkv_str_t root_directory)
{
    if ((self == NULL) || (root_directory.str == NULL) || (root_directory.len == 0)) {
        return false;
    }

    // Allocate a copy of the directory string
    char* const str_copy = (char*)dsdl_alloc(self, root_directory.len + 1);
    if (str_copy == NULL) {
        return false; // OOM
    }
    (void)memcpy(str_copy, root_directory.str, root_directory.len);
    str_copy[root_directory.len] = '\0';

    // Grow the namespaces array
    const size_t     new_count = self->namespace_count + 1;
    wkv_str_t* const new_array = (wkv_str_t*)dsdl_realloc(self, self->namespaces, new_count * sizeof(wkv_str_t));
    if (new_array == NULL) {
        dsdl_free(self, str_copy);
        return false; // OOM
    }

    // Add the new namespace at the end
    new_array[self->namespace_count].len = root_directory.len;
    new_array[self->namespace_count].str = str_copy;

    self->namespaces      = new_array;
    self->namespace_count = new_count;
    return true;
}

// Forward declarations for functions used in dsdl_read's semantic analysis
static uint_least8_t dsdl_ceil_log2(uint64_t n);
static uint_least8_t dsdl_union_tag_bits(size_t field_count);
static uint_least8_t dsdl_type_alignment_bits(const dsdl_type_t* type_ptr);
static void          dsdl_extend_bls_to_extent(dsdl_t* self, dsdl_type_composite_t* composite);
static dsdl_bls_t*   dsdl_type_bls(dsdl_t* self, dsdl_type_t* type_ptr);
static dsdl_bls_t*   dsdl_type_bls_field(dsdl_t* self, dsdl_type_t* type_ptr);

/// Evaluate an assertion expression with the given offset BLS.
/// Returns true if the assertion passes, false if it fails.
static bool dsdl_eval_assertion(dsdl_t* const              self,
                                dsdl_eval_context_t* const eval,
                                dsdl_value_t* const        expr,
                                dsdl_bls_t* const          offset)
{
    assert((self != NULL) && (eval != NULL) && (expr != NULL) && (offset != NULL));
    eval->offset = offset;

    if (!dsdl_resolve_value(expr)) {
        return false;
    }
    if (expr->kind != dsdl_value_bool) {
        DSDL_TRACE(self, "Non-boolean expression in assertion: %d (FAIL)", (int)expr->kind);
        return false;
    }
    return expr->as.boolean;
}

static bool dsdl_string_to_ascii(const wkv_str_t str, uint_least8_t* const out)
{
    if ((out == NULL) || (str.len != 1) || (str.str == NULL)) {
        return false;
    }
    const unsigned char ch = (unsigned char)str.str[0];
    if (ch > 0x7FU) {
        return false;
    }
    *out = (uint_least8_t)ch;
    return true;
}

static size_t dsdl_u8_dec_len(const uint_least8_t value)
{
    if (value >= 100U) {
        return 3U;
    }
    if (value >= 10U) {
        return 2U;
    }
    return 1U;
}

static size_t dsdl_write_u8_dec(char* const out, const uint_least8_t value)
{
    size_t pos = 0;
    if (value >= 100U) {
        out[pos++] = (char)('0' + (value / 100U));
    }
    if (value >= 10U) {
        out[pos++] = (char)('0' + ((value / 10U) % 10U));
    }
    out[pos++] = (char)('0' + (value % 10U));
    return pos;
}

static bool dsdl_uintmax_fits_bits(const uintmax_t value, const uint_least8_t bits)
{
    if (bits == 0U) {
        return false;
    }
    const uint_least8_t width = (uint_least8_t)(sizeof(uintmax_t) * 8U);
    if (bits >= width) {
        return true;
    }
    const uintmax_t max = (((uintmax_t)1U) << bits) - 1U;
    return value <= max;
}

static bool dsdl_intmax_fits_bits(const intmax_t value, const uint_least8_t bits)
{
    if (bits == 0U) {
        return false;
    }
    const uint_least8_t width = (uint_least8_t)(sizeof(intmax_t) * 8U);
    if (bits >= width) {
        return true;
    }
    const intmax_t max = ((intmax_t)1 << (bits - 1U)) - 1;
    const intmax_t min = -((intmax_t)1 << (bits - 1U));
    return (value >= min) && (value <= max);
}

static bool dsdl_eval_constant_value(dsdl_t* const              dsdl,
                                     dsdl_eval_context_t* const eval,
                                     const dsdl_parsed_type_t*  type,
                                     dsdl_value_t* const        value,
                                     const bool                 allow_defer)
{
    if ((dsdl == NULL) || (type == NULL) || (value == NULL)) {
        return false;
    }
    (void)eval;
    if (!dsdl_resolve_value(value)) {
        if (allow_defer && (value->kind == dsdl_value_deferred)) {
            return true;
        }
        return false;
    }
    if (value->kind == dsdl_value_deferred) {
        return allow_defer;
    }

    if (type->kind == DSDL_BOOL) {
        return value->kind == dsdl_value_bool;
    }

    dsdl_type_t kind = type->kind;
    if (dsdl_type_is_alias(kind)) {
        kind = (dsdl_type_t)(kind & ~DSDL_TYPE_ALIAS_MASK);
    }
    if (dsdl_type_is_void(kind) || dsdl_type_is_array(kind) || dsdl_type_is_composite(kind)) {
        return false;
    }

    if (value->kind == dsdl_value_string) {
        uint_least8_t ascii = 0;
        if (kind != DSDL_UINT(8)) {
            return false;
        }
        if (!dsdl_string_to_ascii(value->as.string, &ascii)) {
            return false;
        }
        dsdl_value_dispose(dsdl, value);
        value->kind        = dsdl_value_rational;
        value->flags       = 0;
        value->as.rational = dsdl_rational_from_int((intmax_t)ascii);
    }

    if (dsdl_type_is_float(kind)) {
        return value->kind == dsdl_value_rational;
    }
    if ((value->kind != dsdl_value_rational) || !dsdl_rational_is_int(value->as.rational)) {
        return false;
    }

    if (dsdl_type_is_uint(kind)) {
        uintmax_t uval = 0U;
        if (!dsdl_rational_to_uintmax(value->as.rational, &uval)) {
            return false;
        }
        return dsdl_uintmax_fits_bits(uval, dsdl_type_bit_width(kind));
    }
    if (dsdl_type_is_int(kind)) {
        intmax_t sval = 0;
        if (!dsdl_rational_to_intmax(value->as.rational, &sval)) {
            return false;
        }
        return dsdl_intmax_fits_bits(sval, dsdl_type_bit_width(kind));
    }
    return false;
}

static bool dsdl_eval_array_size_expr(dsdl_t* const              dsdl,
                                      dsdl_eval_context_t* const eval,
                                      dsdl_parsed_type_t* const  type)
{
    if ((dsdl == NULL) || (eval == NULL) || (type == NULL)) {
        return false;
    }
    (void)eval;
    if (!dsdl_type_is_array(type->kind)) {
        return true;
    }
    if (!type->has_array_size_expr) {
        return true;
    }
    if (!dsdl_resolve_value(&type->array_size_expr)) {
        return false;
    }
    if ((type->array_size_expr.kind != dsdl_value_rational) ||
        !dsdl_rational_is_int(type->array_size_expr.as.rational)) {
        return false;
    }
    uintmax_t capacity_um = 0U;
    if (!dsdl_rational_to_uintmax(type->array_size_expr.as.rational, &capacity_um) || (capacity_um == 0U)) {
        return false;
    }
    if (capacity_um > UINT64_MAX) {
        return false;
    }
    uint64_t capacity = (uint64_t)capacity_um;
    if ((type->kind == DSDL_ARRAY_VARIABLE) && !type->is_inclusive) {
        if (capacity <= 1U) {
            return false;
        }
        capacity -= 1U;
    }
    type->array_size          = capacity;
    type->has_array_size_expr = false;
    dsdl_value_dispose(dsdl, &type->array_size_expr);
    return true;
}

const dsdl_type_composite_t* dsdl_read(dsdl_t* const self, const wkv_str_t type_name)
{
    DSDL_TRACE(self, "type_name='%.*s' (len=%zu)", (int)type_name.len, type_name.str, type_name.len);
    if ((self == NULL) || (type_name.str == NULL) || (type_name.len == 0)) {
        DSDL_TRACE(self, "NULL input");
        return NULL;
    }

    // Parse type name
    dsdl_type_ref_t type_ref;
    if (!dsdl_parse_typename(type_name, &type_ref)) {
        DSDL_TRACE(self, "Failed to parse type name");
        return NULL; // Malformed type name
    }
    DSDL_TRACE(self,
               "Parsed: namespace='%.*s' type='%.*s' version=%d.%d",
               (int)type_ref.namespace_part.len,
               type_ref.namespace_part.str,
               (int)type_ref.type_name.len,
               type_ref.type_name.str,
               type_ref.major,
               type_ref.minor);

    // Use the original type_name as cache key (includes version)
    // E.g., "mymsgs.Inner.1.0" -> cache key is "mymsgs.Inner.1.0"
    wkv_node_t* const cached = wkv_get(&self->types, type_name);
    if (cached != NULL) {
        return (const dsdl_type_composite_t*)cached->value;
    }

    // Locate the DSDL file
    char           file_path[DSDL_PATH_MAX];
    uint_least16_t fixed_port_id  = DSDL_FIXED_PORT_ID_NONE;
    uint_least8_t  resolved_major = 0;
    uint_least8_t  resolved_minor = 0;
    if (!dsdl_locate_file(
          self, &type_ref, file_path, sizeof(file_path), &fixed_port_id, &resolved_major, &resolved_minor)) {
        DSDL_TRACE(self, "Failed to locate file");
        return NULL; // File not found
    }
    DSDL_TRACE(
      self, "Located file: '%s' (fixed_port_id=%u, v%u.%u)", file_path, fixed_port_id, resolved_major, resolved_minor);
    if ((resolved_major == 0U) && (resolved_minor == 0U)) {
        return NULL; // Version 0.0 is invalid
    }

    // Read file contents
    if (self->read == NULL) {
        DSDL_TRACE(self, "No read callback");
        return NULL; // No read callback
    }

    wkv_str_t file_content = self->read(self, wkv_key(file_path));
    if (file_content.str == NULL) {
        DSDL_TRACE(self, "Failed to read file");
        return NULL; // Failed to read file
    }
    DSDL_TRACE(self, "Read %zu bytes", file_content.len);

    // Parse the file
    dsdl_parser_t       parser;
    dsdl_parsed_def_t   def;
    dsdl_eval_context_t eval_ctx = { 0 };
    eval_ctx.dsdl                = self;
    eval_ctx.offset_is_defined   = true;

    dsdl_parser_init(&parser, self, file_content.str, file_content.len, &eval_ctx);

    if (!dsdl_parsed_def_init(&def, self)) {
        dsdl_free_str(self, file_content.str);
        DSDL_TRACE(self, "def init failed");
        return NULL; // OOM
    }

    if (!dsdl_parse_definition(&parser, &def)) {
        dsdl_parsed_def_deinit(&def);
        dsdl_free_str(self, file_content.str);
        DSDL_TRACE(self, "parse failed");
        return NULL; // Parse error
    }
    DSDL_TRACE(self, "parsed OK, field_count=%zu, sealed=%d", def.field_count, def.request.is_sealed);

    if (!def.request.is_sealed && !def.request.has_extent) {
        dsdl_parsed_def_deinit(&def);
        dsdl_free_str(self, file_content.str);
        return NULL; // Must be sealed or have extent
    }
    if (def.request.is_sealed && def.request.has_extent) {
        dsdl_parsed_def_deinit(&def);
        dsdl_free_str(self, file_content.str);
        return NULL; // Cannot be both sealed and have extent
    }
    if (def.is_service && !def.response.is_sealed && !def.response.has_extent) {
        dsdl_parsed_def_deinit(&def);
        dsdl_free_str(self, file_content.str);
        return NULL; // Response must be sealed or have extent
    }
    if (def.is_service && def.response.is_sealed && def.response.has_extent) {
        dsdl_parsed_def_deinit(&def);
        dsdl_free_str(self, file_content.str);
        return NULL; // Response cannot be both sealed and have extent
    }
    if (def.request.is_union && (def.field_count < 2U)) {
        dsdl_parsed_def_deinit(&def);
        dsdl_free_str(self, file_content.str);
        return NULL; // Union must have at least 2 fields
    }
    if (def.is_service && def.response.is_union && (def.response_field_count < 2U)) {
        dsdl_parsed_def_deinit(&def);
        dsdl_free_str(self, file_content.str);
        return NULL; // Union must have at least 2 fields
    }

    // Prepare evaluation context for constants and type references.
    eval_ctx.current_namespace = type_ref.namespace_part;
    eval_ctx.constant_names    = def.const_names;
    eval_ctx.constant_values   = def.const_values;
    eval_ctx.constant_count    = 0;

    for (size_t i = 0; i < def.const_count; i++) {
        if (!dsdl_eval_constant_value(self, &eval_ctx, &def.const_types[i], &def.const_values[i], true)) {
            dsdl_parsed_def_deinit(&def);
            dsdl_free_str(self, file_content.str);
            return NULL;
        }
        eval_ctx.constant_count = i + 1U;
    }

    if (def.has_fixed_port_id_expr) {
        if (!dsdl_resolve_value(&def.fixed_port_id_expr) || (def.fixed_port_id_expr.kind != dsdl_value_rational) ||
            !dsdl_rational_is_int(def.fixed_port_id_expr.as.rational)) {
            dsdl_parsed_def_deinit(&def);
            dsdl_free_str(self, file_content.str);
            return NULL;
        }
        uintmax_t port_id = 0U;
        if (!dsdl_rational_to_uintmax(def.fixed_port_id_expr.as.rational, &port_id) ||
            (port_id >= (uintmax_t)DSDL_FIXED_PORT_ID_NONE)) {
            dsdl_parsed_def_deinit(&def);
            dsdl_free_str(self, file_content.str);
            return NULL;
        }
        def.fixed_port_id = (uint_least16_t)port_id;
        dsdl_value_dispose(self, &def.fixed_port_id_expr);
        def.has_fixed_port_id_expr = false;
    }
    if (def.is_service && def.has_fixed_port_id) {
        dsdl_parsed_def_deinit(&def);
        dsdl_free_str(self, file_content.str);
        return NULL;
    }
    if (def.has_fixed_port_id) {
        if ((fixed_port_id != DSDL_FIXED_PORT_ID_NONE) && (fixed_port_id != def.fixed_port_id)) {
            dsdl_parsed_def_deinit(&def);
            dsdl_free_str(self, file_content.str);
            return NULL;
        }
        fixed_port_id = def.fixed_port_id;
    }

    eval_ctx.constant_names  = def.response_const_names;
    eval_ctx.constant_values = def.response_const_values;
    eval_ctx.constant_count  = 0;

    for (size_t i = 0; i < def.response_const_count; i++) {
        if (!dsdl_eval_constant_value(
              self, &eval_ctx, &def.response_const_types[i], &def.response_const_values[i], true)) {
            dsdl_parsed_def_deinit(&def);
            dsdl_free_str(self, file_content.str);
            return NULL;
        }
        eval_ctx.constant_count = i + 1U;
    }

    // Array size expressions are resolved later during semantic analysis once _offset_ is known.

    // Convert parsed definition to dsdl_type_composite_t
    // Calculate total size needed for single allocation
    const size_t major_len          = dsdl_u8_dec_len(resolved_major);
    const size_t minor_len          = dsdl_u8_dec_len(resolved_minor);
    const size_t name_len           = type_ref.full_name.len;
    const size_t name_versioned_len = name_len + 1U + major_len + 1U + minor_len;
    size_t       total_size         = sizeof(dsdl_type_composite_t);
    total_size += name_len;                               // Space for unversioned name string
    total_size += name_versioned_len;                     // Space for versioned name string
    total_size += def.field_count * sizeof(wkv_str_t);    // field_names array
    total_size += def.field_count * sizeof(dsdl_type_t*); // field_types array (pointers)
    total_size += def.const_count * sizeof(wkv_str_t);    // constant_names array
    total_size += def.const_count * sizeof(dsdl_type_t*); // constant_types array
    total_size += def.const_count * sizeof(dsdl_value_t); // constant_values array

    // Sum up field name string lengths
    size_t total_name_len = 0;
    for (size_t i = 0; i < def.field_count; i++) {
        total_name_len += def.field_names[i].len;
    }
    for (size_t i = 0; i < def.const_count; i++) {
        total_name_len += def.const_names[i].len;
    }
    total_size += total_name_len; // Space for all field and constant name strings

    // Allocate single block
    void* block = dsdl_alloc(self, total_size);
    if (block == NULL) {
        dsdl_parsed_def_deinit(&def);
        dsdl_free_str(self, file_content.str);
        DSDL_TRACE(self, "alloc failed, size=%zu", total_size);
        return NULL;
    }
    DSDL_TRACE(self, "allocated %zu bytes", total_size);

    // Layout the block
    dsdl_type_composite_t* composite = (dsdl_type_composite_t*)block;
    char*                  str_ptr   = (char*)(composite + 1);

    // Copy unversioned type name
    composite->name.len = name_len;
    composite->name.str = str_ptr;
    if (name_len > 0U) {
        memcpy(str_ptr, type_ref.full_name.str, name_len);
    }
    str_ptr += name_len;

    // Copy versioned type name
    composite->name_versioned.len = name_versioned_len;
    composite->name_versioned.str = str_ptr;
    size_t name_pos               = 0U;
    if (name_len > 0U) {
        memcpy(str_ptr + name_pos, type_ref.full_name.str, name_len);
        name_pos += name_len;
    }
    str_ptr[name_pos++] = '.';
    name_pos += dsdl_write_u8_dec(str_ptr + name_pos, resolved_major);
    str_ptr[name_pos++] = '.';
    name_pos += dsdl_write_u8_dec(str_ptr + name_pos, resolved_minor);
    str_ptr += composite->name_versioned.len;
    composite->short_name = dsdl_short_name_from_full(composite->name);

    // Set up field_names array
    composite->field_names = (wkv_str_t*)str_ptr;
    str_ptr += def.field_count * sizeof(wkv_str_t);

    // Set up field_types array (array of pointers)
    composite->field_types = (dsdl_type_t**)str_ptr;
    str_ptr += def.field_count * sizeof(dsdl_type_t*);

    // Set up constant names/types/values arrays
    composite->constant_names = (wkv_str_t*)str_ptr;
    str_ptr += def.const_count * sizeof(wkv_str_t);
    composite->constant_types = (dsdl_type_t**)str_ptr;
    str_ptr += def.const_count * sizeof(dsdl_type_t*);
    composite->constant_values = (dsdl_value_t*)str_ptr;
    str_ptr += def.const_count * sizeof(dsdl_value_t);

    composite->field_count    = def.field_count;
    composite->constant_count = def.const_count;
    composite->response       = NULL;
    composite->bls            = NULL;
    composite->fixed_port_id  = fixed_port_id;

    if (def.field_count > 0U) {
        memset(composite->field_types, 0, def.field_count * sizeof(dsdl_type_t*));
    }
    if (def.const_count > 0U) {
        memset(composite->constant_types, 0, def.const_count * sizeof(dsdl_type_t*));
        memset(composite->constant_values, 0, def.const_count * sizeof(dsdl_value_t));
    }

    // Copy field names and create type descriptors
    for (size_t i = 0; i < def.field_count; i++) {
        composite->field_names[i].len = def.field_names[i].len;
        composite->field_names[i].str = str_ptr;
        memcpy(str_ptr, def.field_names[i].str, def.field_names[i].len);
        str_ptr += def.field_names[i].len;
    }

    for (size_t i = 0; i < def.const_count; i++) {
        composite->constant_names[i].len = def.const_names[i].len;
        composite->constant_names[i].str = str_ptr;
        memcpy(str_ptr, def.const_names[i].str, def.const_names[i].len);
        str_ptr += def.const_names[i].len;

        composite->constant_types[i] =
          dsdl_create_type_descriptor(self, &def.const_types[i], type_ref.namespace_part, def.is_deprecated);
        if (composite->constant_types[i] == NULL) {
            dsdl_parsed_def_deinit(&def);
            dsdl_free_str(self, file_content.str);
            dsdl_composite_cleanup(self, composite);
            dsdl_free(self, block);
            return NULL;
        }
    }

    // Set basic properties
    composite->sealed     = def.request.is_sealed;
    composite->deprecated = def.is_deprecated;
    composite->type       = def.request.is_union ? DSDL_COMPOSITE_UNION : DSDL_COMPOSITE_STRUCT;

    // Use version from file resolution (resolved_major/minor come from the actual file found)
    composite->version[0] = resolved_major;
    composite->version[1] = resolved_minor;

    // Extent is assigned after extent expressions are evaluated.
    composite->extent = 0;

    // Initialize optional fields
    composite->fixed_port_id = fixed_port_id;

    dsdl_type_composite_t* response = NULL;
    if (def.is_service) {
        const size_t response_suffix_len         = sizeof("Response") - 1U;
        const size_t response_name_len           = type_ref.full_name.len + 1U + response_suffix_len;
        const size_t response_name_versioned_len = response_name_len + 1U + major_len + 1U + minor_len;

        size_t response_total_size = sizeof(dsdl_type_composite_t);
        response_total_size += response_name_len;
        response_total_size += response_name_versioned_len;
        response_total_size += def.response_field_count * sizeof(wkv_str_t);
        response_total_size += def.response_field_count * sizeof(dsdl_type_t*);
        response_total_size += def.response_const_count * sizeof(wkv_str_t);
        response_total_size += def.response_const_count * sizeof(dsdl_type_t*);
        response_total_size += def.response_const_count * sizeof(dsdl_value_t);

        size_t response_field_name_len = 0;
        for (size_t i = 0; i < def.response_field_count; i++) {
            response_field_name_len += def.response_field_names[i].len;
        }
        size_t response_const_name_len = 0;
        for (size_t i = 0; i < def.response_const_count; i++) {
            response_const_name_len += def.response_const_names[i].len;
        }
        response_total_size += response_field_name_len;
        response_total_size += response_const_name_len;

        void* response_block = dsdl_alloc(self, response_total_size);
        if (response_block == NULL) {
            dsdl_parsed_def_deinit(&def);
            dsdl_free_str(self, file_content.str);
            dsdl_composite_cleanup(self, composite);
            dsdl_free(self, block);
            return NULL;
        }

        response                     = (dsdl_type_composite_t*)response_block;
        char* resp_str_ptr           = (char*)(response + 1);
        response->name.len           = response_name_len;
        response->name.str           = resp_str_ptr;
        response->name_versioned.len = response_name_versioned_len;

        size_t response_pos = 0U;
        if (type_ref.full_name.len > 0) {
            memcpy(resp_str_ptr + response_pos, type_ref.full_name.str, type_ref.full_name.len);
            response_pos += type_ref.full_name.len;
        }
        resp_str_ptr[response_pos++] = '.';
        memcpy(resp_str_ptr + response_pos, "Response", response_suffix_len);
        response_pos += response_suffix_len;
        resp_str_ptr += response->name.len;

        response->name_versioned.str = resp_str_ptr;
        response_pos                 = 0U;
        if (response->name.len > 0U) {
            memcpy(resp_str_ptr + response_pos, response->name.str, response->name.len);
            response_pos += response->name.len;
        }
        resp_str_ptr[response_pos++] = '.';
        response_pos += dsdl_write_u8_dec(resp_str_ptr + response_pos, resolved_major);
        resp_str_ptr[response_pos++] = '.';
        response_pos += dsdl_write_u8_dec(resp_str_ptr + response_pos, resolved_minor);
        resp_str_ptr += response->name_versioned.len;
        response->short_name = dsdl_short_name_from_full(response->name);

        response->field_names = (wkv_str_t*)resp_str_ptr;
        resp_str_ptr += def.response_field_count * sizeof(wkv_str_t);
        response->field_types = (dsdl_type_t**)resp_str_ptr;
        resp_str_ptr += def.response_field_count * sizeof(dsdl_type_t*);
        response->constant_names = (wkv_str_t*)resp_str_ptr;
        resp_str_ptr += def.response_const_count * sizeof(wkv_str_t);
        response->constant_types = (dsdl_type_t**)resp_str_ptr;
        resp_str_ptr += def.response_const_count * sizeof(dsdl_type_t*);
        response->constant_values = (dsdl_value_t*)resp_str_ptr;
        resp_str_ptr += def.response_const_count * sizeof(dsdl_value_t);

        response->field_count    = def.response_field_count;
        response->constant_count = def.response_const_count;
        response->response       = NULL;
        response->bls            = NULL;
        response->fixed_port_id  = DSDL_FIXED_PORT_ID_NONE;

        if (def.response_field_count > 0U) {
            memset(response->field_types, 0, def.response_field_count * sizeof(dsdl_type_t*));
        }
        if (def.response_const_count > 0U) {
            memset(response->constant_types, 0, def.response_const_count * sizeof(dsdl_type_t*));
            memset(response->constant_values, 0, def.response_const_count * sizeof(dsdl_value_t));
        }

        for (size_t i = 0; i < def.response_field_count; i++) {
            response->field_names[i].len = def.response_field_names[i].len;
            response->field_names[i].str = resp_str_ptr;
            memcpy(resp_str_ptr, def.response_field_names[i].str, def.response_field_names[i].len);
            resp_str_ptr += def.response_field_names[i].len;
        }

        for (size_t i = 0; i < def.response_const_count; i++) {
            response->constant_names[i].len = def.response_const_names[i].len;
            response->constant_names[i].str = resp_str_ptr;
            memcpy(resp_str_ptr, def.response_const_names[i].str, def.response_const_names[i].len);
            resp_str_ptr += def.response_const_names[i].len;

            response->constant_types[i] = dsdl_create_type_descriptor(
              self, &def.response_const_types[i], type_ref.namespace_part, def.is_deprecated);
            if (response->constant_types[i] == NULL) {
                dsdl_parsed_def_deinit(&def);
                dsdl_free_str(self, file_content.str);
                dsdl_composite_cleanup(self, response);
                dsdl_composite_cleanup(self, composite);
                dsdl_free(self, response_block);
                dsdl_free(self, block);
                return NULL;
            }
        }

        response->sealed     = def.response.is_sealed;
        response->deprecated = def.is_deprecated;
        response->type       = def.response.is_union ? DSDL_COMPOSITE_UNION : DSDL_COMPOSITE_STRUCT;
        response->version[0] = resolved_major;
        response->version[1] = resolved_minor;
        response->extent     = 0;

        composite->response = response;
    }

    // Semantic analysis: compute _offset_ and validate assertions (request)
    // _offset_ is the bit offset before each field. For structs, it accumulates.
    // For unions, each variant starts at tag_bits offset.
    if ((def.assert_count > 0) || (def.print_count > 0) || def.request.has_extent_expr || (def.field_count > 0) ||
        (def.const_count > 0)) {
        dsdl_bls_t* offset       = dsdl_bls_new_single(self, 0); // Start at 0
        dsdl_bls_t* final_offset = NULL;
        size_t      next_const   = 0U;

        eval_ctx.constant_names  = def.const_names;
        eval_ctx.constant_values = def.const_values;
        eval_ctx.constant_count  = 0U;

        if (def.request.is_union && (def.field_count > 0)) {
            const uint_least8_t tag_bits    = dsdl_union_tag_bits(def.field_count);
            dsdl_bls_t* const   tag_bls     = dsdl_bls_new_single(self, tag_bits);
            dsdl_bls_t*         children[2] = { offset, tag_bls };
            offset                          = dsdl_bls_new_concat(self, 2, children);
        }

        for (size_t i = 0; i < def.field_count; i++) {
            eval_ctx.offset_is_defined = (!def.request.is_union) || (i > 0U);
            while ((next_const < def.const_count) && (def.const_field_idx[next_const] == i)) {
                eval_ctx.offset = offset;
                if (!dsdl_eval_constant_value(
                      self, &eval_ctx, &def.const_types[next_const], &def.const_values[next_const], false)) {
                    dsdl_parsed_def_deinit(&def);
                    dsdl_free_str(self, file_content.str);
                    dsdl_composite_cleanup(self, composite);
                    dsdl_free(self, block);
                    return NULL;
                }
                eval_ctx.constant_count = next_const + 1U;
                next_const++;
            }
            for (size_t a = 0; a < def.assert_count; a++) {
                if (!def.assert_in_response[a] && (def.assert_field_idx[a] == i)) {
                    DSDL_TRACE(self,
                               "Assertion %zu at field %zu, offset min=%" PRIu64 " max=%" PRIu64,
                               a,
                               i,
                               dsdl_bls_min(offset),
                               dsdl_bls_max(offset));
                    if (!dsdl_eval_assertion(self, &eval_ctx, &def.assert_exprs[a], offset)) {
                        DSDL_TRACE(self, "Assertion %zu FAILED at field %zu", a, i);
                        dsdl_parsed_def_deinit(&def);
                        dsdl_free_str(self, file_content.str);
                        dsdl_composite_cleanup(self, composite);
                        dsdl_free(self, block);
                        return NULL;
                    }
                }
            }
            for (size_t p = 0; p < def.print_count; p++) {
                if (!def.print_in_response[p] && (def.print_field_idx[p] == i)) {
                    eval_ctx.offset = offset;
                    if (!dsdl_resolve_value(&def.print_exprs[p])) {
                        dsdl_parsed_def_deinit(&def);
                        dsdl_free_str(self, file_content.str);
                        dsdl_composite_cleanup(self, composite);
                        dsdl_free(self, block);
                        return NULL;
                    }
                }
            }

            eval_ctx.offset            = offset;
            eval_ctx.offset_is_defined = true;
            if (!dsdl_eval_array_size_expr(self, &eval_ctx, &def.field_types[i])) {
                dsdl_parsed_def_deinit(&def);
                dsdl_free_str(self, file_content.str);
                dsdl_composite_cleanup(self, composite);
                dsdl_free(self, block);
                return NULL;
            }
            if (composite->field_types[i] == NULL) {
                composite->field_types[i] =
                  dsdl_create_type_descriptor(self, &def.field_types[i], type_ref.namespace_part, def.is_deprecated);
                if (composite->field_types[i] == NULL) {
                    DSDL_TRACE(self, "type descriptor creation failed for field %zu", i);
                    dsdl_parsed_def_deinit(&def);
                    dsdl_free_str(self, file_content.str);
                    dsdl_composite_cleanup(self, composite);
                    dsdl_free(self, block);
                    return NULL;
                }
            }

            if (!def.request.is_union) {
                const uint_least8_t alignment = dsdl_type_alignment_bits(composite->field_types[i]);
                if (alignment > 1U) {
                    offset = dsdl_bls_new_pad(self, offset, alignment);
                }
                dsdl_bls_t* const field_bls   = dsdl_type_bls_field(self, composite->field_types[i]);
                dsdl_bls_t*       children[2] = { offset, field_bls };
                offset                        = dsdl_bls_new_concat(self, 2, children);
            }
        }

        if (!def.request.is_union) {
            final_offset = offset;
        } else {
            if (def.field_count > 0U) {
                dsdl_bls_t** variants = (dsdl_bls_t**)dsdl_alloc(self, def.field_count * sizeof(dsdl_bls_t*));
                if (variants == NULL) {
                    dsdl_parsed_def_deinit(&def);
                    dsdl_free_str(self, file_content.str);
                    dsdl_composite_cleanup(self, composite);
                    dsdl_free(self, block);
                    return NULL;
                }
                for (size_t i = 0; i < def.field_count; i++) {
                    variants[i] = dsdl_type_bls_field(self, composite->field_types[i]);
                }
                dsdl_bls_t* const variants_bls = dsdl_bls_new_unite(self, def.field_count, variants);
                dsdl_free(self, variants);

                dsdl_bls_t* children[2] = { offset, variants_bls };
                final_offset            = dsdl_bls_new_concat(self, 2, children);
            } else {
                final_offset = offset;
            }
        }

        eval_ctx.offset            = final_offset;
        eval_ctx.offset_is_defined = true;
        while ((next_const < def.const_count) && (def.const_field_idx[next_const] == def.field_count)) {
            if (!dsdl_eval_constant_value(
                  self, &eval_ctx, &def.const_types[next_const], &def.const_values[next_const], false)) {
                dsdl_parsed_def_deinit(&def);
                dsdl_free_str(self, file_content.str);
                dsdl_composite_cleanup(self, composite);
                dsdl_free(self, block);
                return NULL;
            }
            eval_ctx.constant_count = next_const + 1U;
            next_const++;
        }

        for (size_t a = 0; a < def.assert_count; a++) {
            if (!def.assert_in_response[a] && (def.assert_field_idx[a] == def.field_count)) {
                DSDL_TRACE(self,
                           "Final assertion %zu, offset min=%" PRIu64 " max=%" PRIu64,
                           a,
                           dsdl_bls_min(final_offset),
                           dsdl_bls_max(final_offset));
                if (!dsdl_eval_assertion(self, &eval_ctx, &def.assert_exprs[a], final_offset)) {
                    DSDL_TRACE(self, "Final assertion %zu FAILED", a);
                    dsdl_parsed_def_deinit(&def);
                    dsdl_free_str(self, file_content.str);
                    dsdl_composite_cleanup(self, composite);
                    dsdl_free(self, block);
                    return NULL;
                }
            }
        }
        for (size_t p = 0; p < def.print_count; p++) {
            if (!def.print_in_response[p] && (def.print_field_idx[p] == def.field_count)) {
                eval_ctx.offset = final_offset;
                if (!dsdl_resolve_value(&def.print_exprs[p])) {
                    dsdl_parsed_def_deinit(&def);
                    dsdl_free_str(self, file_content.str);
                    dsdl_composite_cleanup(self, composite);
                    dsdl_free(self, block);
                    return NULL;
                }
            }
        }

        if (def.request.has_extent_expr) {
            eval_ctx.offset = final_offset;
            if (!dsdl_resolve_value(&def.request.extent_expr) ||
                (def.request.extent_expr.kind != dsdl_value_rational) ||
                !dsdl_rational_is_int(def.request.extent_expr.as.rational)) {
                DSDL_TRACE(self, "Extent expression evaluation FAILED");
                dsdl_parsed_def_deinit(&def);
                dsdl_free_str(self, file_content.str);
                dsdl_composite_cleanup(self, composite);
                dsdl_free(self, block);
                return NULL;
            }
            uintmax_t extent_um = 0U;
            if (!dsdl_rational_to_uintmax(def.request.extent_expr.as.rational, &extent_um) ||
                (extent_um > UINT64_MAX)) {
                DSDL_TRACE(self, "Extent expression value out of range");
                dsdl_parsed_def_deinit(&def);
                dsdl_free_str(self, file_content.str);
                dsdl_composite_cleanup(self, composite);
                dsdl_free(self, block);
                return NULL;
            }
            def.request.extent_bits = (uint64_t)extent_um;
        }
    }

    // Semantic analysis: compute _offset_ and validate assertions (response)
    if ((response != NULL) && ((def.assert_count > 0) || (def.print_count > 0) || def.response.has_extent_expr ||
                               (response->field_count > 0) || (def.response_const_count > 0))) {
        dsdl_bls_t* offset       = dsdl_bls_new_single(self, 0);
        dsdl_bls_t* final_offset = NULL;
        size_t      next_const   = 0U;

        eval_ctx.constant_names  = def.response_const_names;
        eval_ctx.constant_values = def.response_const_values;
        eval_ctx.constant_count  = 0U;

        if (def.response.is_union && (response->field_count > 0)) {
            const uint_least8_t tag_bits    = dsdl_union_tag_bits(response->field_count);
            dsdl_bls_t* const   tag_bls     = dsdl_bls_new_single(self, tag_bits);
            dsdl_bls_t*         children[2] = { offset, tag_bls };
            offset                          = dsdl_bls_new_concat(self, 2, children);
        }

        for (size_t i = 0; i < response->field_count; i++) {
            eval_ctx.offset_is_defined = (!def.response.is_union) || (i > 0U);
            while ((next_const < def.response_const_count) && (def.response_const_field_idx[next_const] == i)) {
                eval_ctx.offset = offset;
                if (!dsdl_eval_constant_value(self,
                                              &eval_ctx,
                                              &def.response_const_types[next_const],
                                              &def.response_const_values[next_const],
                                              false)) {
                    dsdl_parsed_def_deinit(&def);
                    dsdl_free_str(self, file_content.str);
                    dsdl_composite_cleanup(self, composite);
                    dsdl_free(self, block);
                    return NULL;
                }
                eval_ctx.constant_count = next_const + 1U;
                next_const++;
            }

            for (size_t a = 0; a < def.assert_count; a++) {
                if (def.assert_in_response[a] && (def.assert_field_idx[a] == i)) {
                    DSDL_TRACE(self,
                               "Response assertion %zu at field %zu, offset min=%" PRIu64 " max=%" PRIu64,
                               a,
                               i,
                               dsdl_bls_min(offset),
                               dsdl_bls_max(offset));
                    if (!dsdl_eval_assertion(self, &eval_ctx, &def.assert_exprs[a], offset)) {
                        DSDL_TRACE(self, "Response assertion %zu FAILED at field %zu", a, i);
                        dsdl_parsed_def_deinit(&def);
                        dsdl_free_str(self, file_content.str);
                        dsdl_composite_cleanup(self, composite);
                        dsdl_free(self, block);
                        return NULL;
                    }
                }
            }
            for (size_t p = 0; p < def.print_count; p++) {
                if (def.print_in_response[p] && (def.print_field_idx[p] == i)) {
                    eval_ctx.offset = offset;
                    if (!dsdl_resolve_value(&def.print_exprs[p])) {
                        dsdl_parsed_def_deinit(&def);
                        dsdl_free_str(self, file_content.str);
                        dsdl_composite_cleanup(self, composite);
                        dsdl_free(self, block);
                        return NULL;
                    }
                }
            }

            eval_ctx.offset            = offset;
            eval_ctx.offset_is_defined = true;
            if (!dsdl_eval_array_size_expr(self, &eval_ctx, &def.response_field_types[i])) {
                dsdl_parsed_def_deinit(&def);
                dsdl_free_str(self, file_content.str);
                dsdl_composite_cleanup(self, composite);
                dsdl_free(self, block);
                return NULL;
            }
            if (response->field_types[i] == NULL) {
                response->field_types[i] = dsdl_create_type_descriptor(
                  self, &def.response_field_types[i], type_ref.namespace_part, def.is_deprecated);
                if (response->field_types[i] == NULL) {
                    dsdl_parsed_def_deinit(&def);
                    dsdl_free_str(self, file_content.str);
                    dsdl_composite_cleanup(self, composite);
                    dsdl_free(self, block);
                    return NULL;
                }
            }

            if (!def.response.is_union) {
                const uint_least8_t alignment = dsdl_type_alignment_bits(response->field_types[i]);
                if (alignment > 1U) {
                    offset = dsdl_bls_new_pad(self, offset, alignment);
                }
                dsdl_bls_t* const field_bls   = dsdl_type_bls_field(self, response->field_types[i]);
                dsdl_bls_t*       children[2] = { offset, field_bls };
                offset                        = dsdl_bls_new_concat(self, 2, children);
            }
        }

        if (!def.response.is_union) {
            final_offset = offset;
        } else {
            if (response->field_count > 0U) {
                dsdl_bls_t** variants = (dsdl_bls_t**)dsdl_alloc(self, response->field_count * sizeof(dsdl_bls_t*));
                if (variants == NULL) {
                    dsdl_parsed_def_deinit(&def);
                    dsdl_free_str(self, file_content.str);
                    dsdl_composite_cleanup(self, composite);
                    dsdl_free(self, block);
                    return NULL;
                }
                for (size_t i = 0; i < response->field_count; i++) {
                    variants[i] = dsdl_type_bls_field(self, response->field_types[i]);
                }
                dsdl_bls_t* const variants_bls = dsdl_bls_new_unite(self, response->field_count, variants);
                dsdl_free(self, variants);

                dsdl_bls_t* children[2] = { offset, variants_bls };
                final_offset            = dsdl_bls_new_concat(self, 2, children);
            } else {
                final_offset = offset;
            }
        }

        eval_ctx.offset            = final_offset;
        eval_ctx.offset_is_defined = true;
        while ((next_const < def.response_const_count) &&
               (def.response_const_field_idx[next_const] == response->field_count)) {
            if (!dsdl_eval_constant_value(self,
                                          &eval_ctx,
                                          &def.response_const_types[next_const],
                                          &def.response_const_values[next_const],
                                          false)) {
                dsdl_parsed_def_deinit(&def);
                dsdl_free_str(self, file_content.str);
                dsdl_composite_cleanup(self, composite);
                dsdl_free(self, block);
                return NULL;
            }
            eval_ctx.constant_count = next_const + 1U;
            next_const++;
        }

        for (size_t a = 0; a < def.assert_count; a++) {
            if (def.assert_in_response[a] && (def.assert_field_idx[a] == response->field_count)) {
                DSDL_TRACE(self,
                           "Final response assertion %zu, offset min=%" PRIu64 " max=%" PRIu64,
                           a,
                           dsdl_bls_min(final_offset),
                           dsdl_bls_max(final_offset));
                if (!dsdl_eval_assertion(self, &eval_ctx, &def.assert_exprs[a], final_offset)) {
                    DSDL_TRACE(self, "Final response assertion %zu FAILED", a);
                    dsdl_parsed_def_deinit(&def);
                    dsdl_free_str(self, file_content.str);
                    dsdl_composite_cleanup(self, composite);
                    dsdl_free(self, block);
                    return NULL;
                }
            }
        }
        for (size_t p = 0; p < def.print_count; p++) {
            if (def.print_in_response[p] && (def.print_field_idx[p] == response->field_count)) {
                eval_ctx.offset = final_offset;
                if (!dsdl_resolve_value(&def.print_exprs[p])) {
                    dsdl_parsed_def_deinit(&def);
                    dsdl_free_str(self, file_content.str);
                    dsdl_composite_cleanup(self, composite);
                    dsdl_free(self, block);
                    return NULL;
                }
            }
        }

        if (def.response.has_extent_expr) {
            eval_ctx.offset = final_offset;
            if (!dsdl_resolve_value(&def.response.extent_expr) ||
                (def.response.extent_expr.kind != dsdl_value_rational) ||
                !dsdl_rational_is_int(def.response.extent_expr.as.rational)) {
                DSDL_TRACE(self, "Response extent expression evaluation FAILED");
                dsdl_parsed_def_deinit(&def);
                dsdl_free_str(self, file_content.str);
                dsdl_composite_cleanup(self, composite);
                dsdl_free(self, block);
                return NULL;
            }
            uintmax_t extent_um = 0U;
            if (!dsdl_rational_to_uintmax(def.response.extent_expr.as.rational, &extent_um) ||
                (extent_um > UINT64_MAX)) {
                DSDL_TRACE(self, "Response extent expression value out of range");
                dsdl_parsed_def_deinit(&def);
                dsdl_free_str(self, file_content.str);
                dsdl_composite_cleanup(self, composite);
                dsdl_free(self, block);
                return NULL;
            }
            def.response.extent_bits = (uint64_t)extent_um;
        }
    }

    composite->extent = def.request.has_extent ? def.request.extent_bits / 8U : 0U;
    if (response != NULL) {
        response->extent = def.response.has_extent ? def.response.extent_bits / 8U : 0U;
    }

    // Extent validation: check that max serialized size doesn't exceed declared extent (request)
    if (def.request.has_extent) {
        dsdl_bls_t* const type_bls = dsdl_type_bls(self, &composite->type);
        if (type_bls != NULL) {
            const uint64_t max_bits = dsdl_bls_max(type_bls);
            if (max_bits > def.request.extent_bits) {
                DSDL_TRACE(self,
                           "Extent validation FAILED: max_bits=%" PRIu64 " > extent_bits=%" PRIu64,
                           max_bits,
                           def.request.extent_bits);
                dsdl_parsed_def_deinit(&def);
                dsdl_free_str(self, file_content.str);
                dsdl_composite_cleanup(self, composite);
                dsdl_free(self, block);
                return NULL;
            }
            if (def.request.is_sealed && (max_bits != def.request.extent_bits)) {
                DSDL_TRACE(self,
                           "Sealed extent validation FAILED: max_bits=%" PRIu64 " != extent_bits=%" PRIu64,
                           max_bits,
                           def.request.extent_bits);
                dsdl_parsed_def_deinit(&def);
                dsdl_free_str(self, file_content.str);
                dsdl_composite_cleanup(self, composite);
                dsdl_free(self, block);
                return NULL;
            }
        }
    }

    // Extent validation: check that max serialized size doesn't exceed declared extent (response)
    if ((response != NULL) && def.response.has_extent) {
        dsdl_bls_t* const type_bls = dsdl_type_bls(self, &response->type);
        if (type_bls != NULL) {
            const uint64_t max_bits = dsdl_bls_max(type_bls);
            if (max_bits > def.response.extent_bits) {
                DSDL_TRACE(self,
                           "Response extent validation FAILED: max_bits=%" PRIu64 " > extent_bits=%" PRIu64,
                           max_bits,
                           def.response.extent_bits);
                dsdl_parsed_def_deinit(&def);
                dsdl_free_str(self, file_content.str);
                dsdl_composite_cleanup(self, composite);
                dsdl_free(self, block);
                return NULL;
            }
            if (def.response.is_sealed && (max_bits != def.response.extent_bits)) {
                DSDL_TRACE(self,
                           "Response sealed extent validation FAILED: max_bits=%" PRIu64 " != extent_bits=%" PRIu64,
                           max_bits,
                           def.response.extent_bits);
                dsdl_parsed_def_deinit(&def);
                dsdl_free_str(self, file_content.str);
                dsdl_composite_cleanup(self, composite);
                dsdl_free(self, block);
                return NULL;
            }
        }
    }

    dsdl_extend_bls_to_extent(self, composite);
    if (response != NULL) {
        dsdl_extend_bls_to_extent(self, response);
    }

    // Finalize deferred constants using the final _offset_.
    if (def.const_count > 0U) {
        eval_ctx.constant_names  = def.const_names;
        eval_ctx.constant_values = def.const_values;
        eval_ctx.constant_count  = def.const_count;

        bool has_deferred = false;
        for (size_t i = 0; i < def.const_count; i++) {
            if (def.const_values[i].kind == dsdl_value_deferred) {
                has_deferred = true;
                break;
            }
        }
        if (has_deferred) {
            dsdl_bls_t* offset_bls = dsdl_type_bls(self, &composite->type);
            if (offset_bls == NULL) {
                dsdl_parsed_def_deinit(&def);
                dsdl_free_str(self, file_content.str);
                dsdl_composite_cleanup(self, composite);
                dsdl_free(self, block);
                return NULL;
            }
            if (offset_bls->kind == dsdl_bls_pad) {
                offset_bls = offset_bls->data.pad.child;
            }
            eval_ctx.offset         = offset_bls;
            eval_ctx.constant_count = 0U;
            for (size_t i = 0; i < def.const_count; i++) {
                if (!dsdl_eval_constant_value(self, &eval_ctx, &def.const_types[i], &def.const_values[i], false)) {
                    dsdl_parsed_def_deinit(&def);
                    dsdl_free_str(self, file_content.str);
                    dsdl_composite_cleanup(self, composite);
                    dsdl_free(self, block);
                    return NULL;
                }
                eval_ctx.constant_count = i + 1U;
            }
        }

        for (size_t i = 0; i < def.const_count; i++) {
            if (!dsdl_value_clone(self, &def.const_values[i], &composite->constant_values[i])) {
                dsdl_parsed_def_deinit(&def);
                dsdl_free_str(self, file_content.str);
                dsdl_composite_cleanup(self, composite);
                dsdl_free(self, block);
                return NULL;
            }
        }
    }

    if ((response != NULL) && (def.response_const_count > 0U)) {
        eval_ctx.constant_names  = def.response_const_names;
        eval_ctx.constant_values = def.response_const_values;
        eval_ctx.constant_count  = def.response_const_count;

        bool has_deferred = false;
        for (size_t i = 0; i < def.response_const_count; i++) {
            if (def.response_const_values[i].kind == dsdl_value_deferred) {
                has_deferred = true;
                break;
            }
        }
        if (has_deferred) {
            dsdl_bls_t* offset_bls = dsdl_type_bls(self, &response->type);
            if (offset_bls == NULL) {
                dsdl_parsed_def_deinit(&def);
                dsdl_free_str(self, file_content.str);
                dsdl_composite_cleanup(self, composite);
                dsdl_free(self, block);
                return NULL;
            }
            if (offset_bls->kind == dsdl_bls_pad) {
                offset_bls = offset_bls->data.pad.child;
            }
            eval_ctx.offset         = offset_bls;
            eval_ctx.constant_count = 0U;
            for (size_t i = 0; i < def.response_const_count; i++) {
                if (!dsdl_eval_constant_value(
                      self, &eval_ctx, &def.response_const_types[i], &def.response_const_values[i], false)) {
                    dsdl_parsed_def_deinit(&def);
                    dsdl_free_str(self, file_content.str);
                    dsdl_composite_cleanup(self, composite);
                    dsdl_free(self, block);
                    return NULL;
                }
                eval_ctx.constant_count = i + 1U;
            }
        }

        for (size_t i = 0; i < def.response_const_count; i++) {
            if (!dsdl_value_clone(self, &def.response_const_values[i], &response->constant_values[i])) {
                dsdl_parsed_def_deinit(&def);
                dsdl_free_str(self, file_content.str);
                dsdl_composite_cleanup(self, composite);
                dsdl_free(self, block);
                return NULL;
            }
        }
    }

    // Cache the result using type_name as key
    wkv_node_t* const cache_node = wkv_set(&self->types, type_name);
    if (cache_node == NULL) {
        dsdl_parsed_def_deinit(&def);
        dsdl_free_str(self, file_content.str);
        dsdl_composite_cleanup(self, composite);
        dsdl_free(self, block);
        return NULL;
    }
    cache_node->value = composite;

    dsdl_free_str(self, file_content.str);
    dsdl_parsed_def_deinit(&def);
    return composite;
}

// ============================================================================
// Size calculation
// ============================================================================

/// Calculate ceil(log2(n)) for n > 0. Returns 0 for n <= 1.
/// Used for array length prefix and union tag bit widths.
static uint_least8_t dsdl_ceil_log2(const uint64_t n)
{
    if (n <= 1) {
        return 0;
    }
    uint_least8_t result = 0;
    uint64_t      val    = n - 1; // -1 because we want ceil, not floor
    while (val > 0) {
        val >>= 1;
        result++;
    }
    return result;
}

/// Round up to the next power of two, min 8 (byte-aligned).
static uint_least8_t dsdl_pow2_ceil_min8(uint_least8_t bits)
{
    if (bits < 8U) {
        bits = 8U;
    }
    uint_least8_t result = 1U;
    while (result < bits) {
        result = (uint_least8_t)(result << 1U);
    }
    return result;
}

/// Calculate the bit width of the length prefix for a variable-length array.
/// For capacity C (max elements), prefix is byte-aligned power-of-two bits.
static uint_least8_t dsdl_array_length_prefix_bits(const uint64_t capacity)
{
    return dsdl_pow2_ceil_min8(dsdl_ceil_log2(capacity + 1U));
}

/// Calculate the bit width of the union tag for a union with N alternatives.
/// Tag is byte-aligned power-of-two bits.
static uint_least8_t dsdl_union_tag_bits(const size_t field_count)
{
    return dsdl_pow2_ceil_min8(dsdl_ceil_log2((uint64_t)field_count));
}

/// Calculate alignment requirement (in bits) for a type.
static uint_least8_t dsdl_type_alignment_bits(const dsdl_type_t* const type_ptr)
{
    if (type_ptr == NULL) {
        return 1U;
    }

    const dsdl_type_t kind = *type_ptr;
    if (dsdl_type_is_composite(kind)) {
        return 8U;
    }
    if (dsdl_type_is_array(kind)) {
        const dsdl_type_array_t* const arr = (const dsdl_type_array_t*)type_ptr;
        return dsdl_type_alignment_bits(arr->member_type);
    }
    return 1U;
}

/// Forward declaration for recursive type size calculation.
static uint64_t dsdl_type_max_bits(const dsdl_type_t* type_ptr);
static uint64_t dsdl_type_max_bits_field(const dsdl_type_t* type_ptr);

/// Calculate the maximum serialized size in bits for a composite type.
static uint64_t dsdl_composite_max_bits(const dsdl_type_composite_t* const composite)
{
    if (composite == NULL) {
        return 0;
    }

    const bool is_union = (composite->type == DSDL_COMPOSITE_UNION);

    uint64_t max_bits = 0;
    if (is_union) {
        // Union: tag bits + max variant size, padded to composite alignment.
        const uint64_t tag_bits = dsdl_union_tag_bits(composite->field_count);

        uint64_t max_field_bits = 0;
        for (size_t i = 0; i < composite->field_count; i++) {
            const uint64_t field_bits = dsdl_type_max_bits_field(composite->field_types[i]);
            if (field_bits > max_field_bits) {
                max_field_bits = field_bits;
            }
        }

        max_bits = dsdl_align_up(tag_bits + max_field_bits, 8U);
    } else {
        // Struct: sum with inter-field padding to satisfy alignment, plus final padding.
        uint64_t total_bits = 0;
        for (size_t i = 0; i < composite->field_count; i++) {
            const dsdl_type_t*  field_type = composite->field_types[i];
            const uint_least8_t alignment  = dsdl_type_alignment_bits(field_type);
            if (alignment > 1U) {
                total_bits = dsdl_align_up(total_bits, alignment);
            }
            total_bits += dsdl_type_max_bits_field(field_type);
        }
        max_bits = dsdl_align_up(total_bits, 8U);
    }

    if ((!composite->sealed) && (composite->extent > 0U)) {
        const uint64_t extent_bits = composite->extent * 8U;
        if (extent_bits > max_bits) {
            max_bits = extent_bits;
        }
    }

    return max_bits;
}

/// Calculate the native storage size in bytes for a single value of the given type.
/// This is the size of the C type used to represent the value, not the serialized bit size.
/// For arrays of primitives/composites, this returns the element size for iterating.
static uint64_t dsdl_type_native_size(const dsdl_type_t* type_ptr)
{
    if (type_ptr == NULL) {
        return 0;
    }

    const dsdl_type_t kind = *type_ptr;

    // Void has no storage
    if (dsdl_type_is_void(kind)) {
        return 0;
    }

    // Bool
    if (kind == DSDL_BOOL) {
        return sizeof(bool);
    }

    // Integers (signed and unsigned) - use int_leastX_t sizes
    if (dsdl_type_is_int(kind) || dsdl_type_is_uint(kind) || (kind == DSDL_BYTE)) {
        const uint64_t bits = dsdl_type_bit_width(kind);
        if (bits <= 8) {
            return sizeof(uint_least8_t);
        } else if (bits <= 16) {
            return sizeof(uint_least16_t);
        } else if (bits <= 32) {
            return sizeof(uint_least32_t);
        } else {
            return sizeof(uint_least64_t);
        }
    }

    // Floats
    if (dsdl_type_is_float(kind)) {
        const uint64_t bits = dsdl_type_bit_width(kind);
        if (bits <= 32) {
            return sizeof(float);
        } else {
            return sizeof(double);
        }
    }

    // Aliases (UTF8 maps to char)
    if (dsdl_type_is_alias(kind)) {
        if (kind == DSDL_UTF8) {
            return sizeof(char);
        }
        // Other aliases - strip alias and recurse
        const dsdl_type_t base = (dsdl_type_t)(kind & ~DSDL_TYPE_ALIAS_MASK);
        return dsdl_type_native_size(&base);
    }

    // Arrays - the value itself is a pointer (fixed) or dsdl_value_array_variable_t (variable)
    if (dsdl_type_is_array(kind)) {
        if (kind == DSDL_ARRAY_VARIABLE) {
            return sizeof(dsdl_value_array_variable_t);
        } else {
            // Fixed array is represented as void* to elements
            return sizeof(void*);
        }
    }

    // Composites - struct or union value wrapper
    if (dsdl_type_is_composite(kind)) {
        if (kind == DSDL_COMPOSITE_UNION) {
            return sizeof(dsdl_value_union_t);
        } else {
            return sizeof(dsdl_value_struct_t);
        }
    }

    return 0;
}

/// Calculate the maximum serialized size in bits for any type.
/// The type_ptr can point to a dsdl_type_t (primitive), dsdl_type_array_t, or dsdl_type_composite_t.
static uint64_t dsdl_type_max_bits(const dsdl_type_t* type_ptr)
{
    if (type_ptr == NULL) {
        return 0;
    }

    const dsdl_type_t kind = *type_ptr;

    // Primitive types (void, int, uint, float, bool, byte)
    if (dsdl_type_is_void(kind) || dsdl_type_is_int(kind) || dsdl_type_is_uint(kind) || dsdl_type_is_float(kind)) {
        return dsdl_type_bit_width(kind);
    }

    // Handle aliases (bool = uint1, byte = uint8)
    if (dsdl_type_is_alias(kind)) {
        // Strip alias flag to get base type bit width
        return dsdl_type_bit_width((dsdl_type_t)(kind & ~DSDL_TYPE_ALIAS_MASK));
    }

    // Array types
    if (dsdl_type_is_array(kind)) {
        const dsdl_type_array_t* arr          = (const dsdl_type_array_t*)type_ptr;
        const uint64_t           element_bits = dsdl_type_max_bits_field(arr->member_type);
        const uint64_t           total_bits   = arr->capacity * element_bits;

        if (kind == DSDL_ARRAY_VARIABLE) {
            // Variable-length array: length prefix + elements
            const uint64_t prefix_bits = dsdl_array_length_prefix_bits(arr->capacity);
            return prefix_bits + total_bits;
        } else {
            // Fixed-length array: just the elements
            return total_bits;
        }
    }

    // Composite types
    if (dsdl_type_is_composite(kind)) {
        const dsdl_type_composite_t* composite = (const dsdl_type_composite_t*)type_ptr;
        return dsdl_composite_max_bits(composite);
    }

    return 0; // Unknown type
}

/// Maximum serialized size in bits for a type when used as a field.
/// Adds delimiter header size for delimited composites.
static uint64_t dsdl_type_max_bits_field(const dsdl_type_t* const type_ptr)
{
    if (type_ptr == NULL) {
        return 0;
    }
    uint64_t bits = dsdl_type_max_bits(type_ptr);
    if (dsdl_type_is_composite(*type_ptr)) {
        const dsdl_type_composite_t* const composite = (const dsdl_type_composite_t*)type_ptr;
        if (!composite->sealed) {
            bits += 32U; // Delimiter header when nested
        }
    }
    return bits;
}

/// Compute the symbolic bit length set for any type.
/// For primitives, returns a single-value set. For arrays and composites, uses the stored bls.
/// If bls hasn't been computed yet, computes and stores it.
/// This function should be called after type creation is complete.
static dsdl_bls_t* dsdl_type_bls(dsdl_t* const self, dsdl_type_t* type_ptr)
{
    assert(self != NULL);
    if (type_ptr == NULL) {
        DSDL_TRACE(self, "dsdl_type_bls: NULL type_ptr, returning {0}");
        return dsdl_bls_new_single(self, 0);
    }

    const dsdl_type_t kind = *type_ptr;
    DSDL_TRACE(self, "dsdl_type_bls: kind=0x%04x", kind);

    // Primitive types (void, int, uint, float)
    if (dsdl_type_is_void(kind) || dsdl_type_is_int(kind) || dsdl_type_is_uint(kind) || dsdl_type_is_float(kind)) {
        const uint64_t bits = dsdl_type_bit_width(kind);
        DSDL_TRACE(self, "  -> primitive %" PRIu64 " bits", bits);
        return dsdl_bls_new_single(self, bits);
    }

    // Handle aliases (bool = uint1, byte = uint8)
    if (dsdl_type_is_alias(kind)) {
        const uint64_t bits = dsdl_type_bit_width((dsdl_type_t)(kind & ~DSDL_TYPE_ALIAS_MASK));
        DSDL_TRACE(self, "  -> alias %" PRIu64 " bits", bits);
        return dsdl_bls_new_single(self, bits);
    }

    // Array types - use stored bls
    if (dsdl_type_is_array(kind)) {
        dsdl_type_array_t* arr = (dsdl_type_array_t*)type_ptr;
        if (arr->bls != NULL) {
            DSDL_TRACE(self, "  -> array (cached), capacity=%" PRIu64, arr->capacity);
            return (dsdl_bls_t*)arr->bls;
        }

        DSDL_TRACE(self,
                   "  -> array (computing), capacity=%" PRIu64 ", variable=%d",
                   arr->capacity,
                   (kind == DSDL_ARRAY_VARIABLE));

        // Compute and store bls
        dsdl_bls_t* const elem_bls = dsdl_type_bls_field(self, arr->member_type);
        if (elem_bls == NULL) {
            return NULL;
        }

        if (kind == DSDL_ARRAY_VARIABLE) {
            // Variable-length: length_prefix + repeat_range(element, capacity)
            const uint64_t    prefix_bits = dsdl_array_length_prefix_bits(arr->capacity);
            dsdl_bls_t* const prefix_bls  = dsdl_bls_new_single(self, prefix_bits);
            dsdl_bls_t* const var_bls     = dsdl_bls_new_repeat_range(self, elem_bls, arr->capacity);
            dsdl_bls_t*       children[2] = { prefix_bls, var_bls };
            arr->bls                      = dsdl_bls_new_concat(self, 2, children);
        } else {
            // Fixed-length: repeat(element, capacity)
            arr->bls = dsdl_bls_new_repeat(self, elem_bls, arr->capacity);
        }
        return (dsdl_bls_t*)arr->bls;
    }

    // Composite types - use stored bls
    if (dsdl_type_is_composite(kind)) {
        dsdl_type_composite_t* composite = (dsdl_type_composite_t*)type_ptr;
        if (composite->bls != NULL) {
            DSDL_TRACE(self, "  -> composite (cached) '%.*s'", (int)composite->name.len, composite->name.str);
            return (dsdl_bls_t*)composite->bls;
        }

        DSDL_TRACE(self,
                   "  -> composite (computing) '%.*s', %zu fields",
                   (int)composite->name.len,
                   composite->name.str,
                   composite->field_count);

        // Compute bls based on struct vs union
        const bool is_union = (composite->type == DSDL_COMPOSITE_UNION);

        if (is_union) {
            if (composite->field_count == 0) {
                composite->bls = dsdl_bls_new_single(self, 0);
                return (dsdl_bls_t*)composite->bls;
            }
            // Union: tag + union of all variant bit length sets
            const uint64_t    tag_bits = dsdl_union_tag_bits(composite->field_count);
            dsdl_bls_t* const tag_bls  = dsdl_bls_new_single(self, tag_bits);

            // Collect variant bit length sets
            dsdl_bls_t** variants = (dsdl_bls_t**)dsdl_alloc(self, composite->field_count * sizeof(dsdl_bls_t*));
            if (variants == NULL) {
                return NULL;
            }
            for (size_t i = 0; i < composite->field_count; i++) {
                variants[i] = dsdl_type_bls_field(self, composite->field_types[i]);
            }
            dsdl_bls_t* const variants_bls = dsdl_bls_new_unite(self, composite->field_count, variants);
            dsdl_free(self, variants);

            dsdl_bls_t* children[2] = { tag_bls, variants_bls };
            composite->bls          = dsdl_bls_new_concat(self, 2, children);
        } else {
            if (composite->field_count == 0) {
                composite->bls = dsdl_bls_new_single(self, 0);
                return (dsdl_bls_t*)composite->bls;
            }
            // Struct: concatenate fields with alignment padding.
            dsdl_bls_t* offset = dsdl_bls_new_single(self, 0);
            if (offset == NULL) {
                return NULL;
            }
            for (size_t i = 0; i < composite->field_count; i++) {
                const dsdl_type_t* const field_type = composite->field_types[i];
                const uint_least8_t      alignment  = dsdl_type_alignment_bits(field_type);
                if (alignment > 1U) {
                    offset = dsdl_bls_new_pad(self, offset, alignment);
                    if (offset == NULL) {
                        return NULL;
                    }
                }

                dsdl_bls_t* const field_bls = dsdl_type_bls_field(self, composite->field_types[i]);
                if (field_bls == NULL) {
                    return NULL;
                }
                dsdl_bls_t* children[2] = { offset, field_bls };
                offset                  = dsdl_bls_new_concat(self, 2, children);
                if (offset == NULL) {
                    return NULL;
                }
            }
            composite->bls = offset;
        }
        composite->bls = dsdl_bls_new_pad(self, composite->bls, 8);
        if ((!composite->sealed) && (composite->extent > 0U) && (composite->bls != NULL)) {
            const uint64_t extent_bits = composite->extent * 8U;
            const uint64_t max_bits    = dsdl_bls_max(composite->bls);
            if (extent_bits > max_bits) {
                const uint64_t    pad_bits    = extent_bits - max_bits;
                const uint64_t    pad_bytes   = pad_bits / 8U;
                dsdl_bls_t* const pad_unit    = dsdl_bls_new_single(self, 8U);
                dsdl_bls_t* const pad_range   = dsdl_bls_new_repeat_range(self, pad_unit, pad_bytes);
                dsdl_bls_t*       children[2] = { composite->bls, pad_range };
                composite->bls                = dsdl_bls_new_concat(self, 2, children);
            }
        }
        return (dsdl_bls_t*)composite->bls;
    }

    return dsdl_bls_new_single(self, 0);
}

/// Bit length set for a type when used as a field.
/// Adds delimiter header for delimited composites.
static dsdl_bls_t* dsdl_type_bls_field(dsdl_t* const self, dsdl_type_t* const type_ptr)
{
    dsdl_bls_t* const bls = dsdl_type_bls(self, type_ptr);
    if ((bls == NULL) || (type_ptr == NULL)) {
        return bls;
    }
    if (dsdl_type_is_composite(*type_ptr)) {
        const dsdl_type_composite_t* const composite = (const dsdl_type_composite_t*)type_ptr;
        if (!composite->sealed) {
            dsdl_bls_t* const header      = dsdl_bls_new_single(self, 32U);
            dsdl_bls_t*       children[2] = { header, bls };
            return dsdl_bls_new_concat(self, 2, children);
        }
    }
    return bls;
}

static void dsdl_extend_bls_to_extent(dsdl_t* const self, dsdl_type_composite_t* const composite)
{
    if ((self == NULL) || (composite == NULL) || composite->sealed || (composite->extent == 0U) ||
        (composite->bls == NULL)) {
        return;
    }
    const uint64_t extent_bits = composite->extent * 8U;
    const uint64_t max_bits    = dsdl_bls_max((dsdl_bls_t*)composite->bls);
    if (extent_bits <= max_bits) {
        return;
    }
    const uint64_t pad_bits  = extent_bits - max_bits;
    const uint64_t pad_bytes = pad_bits / 8U;
    if (pad_bytes == 0U) {
        return;
    }
    dsdl_bls_t* const pad_unit    = dsdl_bls_new_single(self, 8U);
    dsdl_bls_t* const pad_range   = dsdl_bls_new_repeat_range(self, pad_unit, pad_bytes);
    dsdl_bls_t*       children[2] = { (dsdl_bls_t*)composite->bls, pad_range };
    composite->bls                = dsdl_bls_new_concat(self, 2, children);
}

uint64_t dsdl_serialized_footprint(const dsdl_type_composite_t* const type)
{
    if (type == NULL) {
        return 0;
    }

    const uint64_t max_bits = dsdl_composite_max_bits(type);

    // For non-sealed composites, a 32-bit delimiter header is prepended when nested
    // However, for the top-level type, we return just the content size.
    // The delimiter is only added when this type is used as a field in another type.

    return (max_bits + 7) / 8; // Convert bits to bytes, rounding up
}

// ============================================================================
// Bit buffer operations for serialization/deserialization
// ============================================================================

/// Bit buffer for serialization/deserialization.
/// Tracks current bit position within a byte array.
typedef struct
{
    unsigned char* data;          ///< Pointer to the byte buffer
    uint64_t       capacity_bits; ///< Total capacity in bits
    uint64_t       offset_bits;   ///< Current bit position
    bool           error;         ///< Set on serialization or deserialization error
} dsdl_bitbuf_t;

/// Write up to 64 bits to the buffer, little-endian.
/// Bits are written starting from the LSB of the value.
static void dsdl_bitbuf_write(dsdl_bitbuf_t* const buf, uint64_t value, uint64_t bits)
{
    if ((buf == NULL) || (bits == 0)) {
        return;
    }
    if (buf->error) {
        return;
    }
    if ((bits > 64) || ((buf->offset_bits + bits) > buf->capacity_bits)) {
        buf->error = true;
        return;
    }

    while (bits > 0) {
        const uint64_t byte_index    = buf->offset_bits / 8;
        const uint64_t bit_in_byte   = buf->offset_bits % 8;
        const uint64_t bits_in_byte  = 8 - bit_in_byte;
        const uint64_t bits_to_write = (bits < bits_in_byte) ? bits : bits_in_byte;

        // Mask for the bits we're writing
        const unsigned char mask = (unsigned char)((1U << bits_to_write) - 1U);
        const unsigned char val  = (unsigned char)(value & mask);

        // Clear target bits and write
        buf->data[byte_index] =
          (unsigned char)((buf->data[byte_index] & ~(mask << bit_in_byte)) | (val << bit_in_byte));

        buf->offset_bits += bits_to_write;
        value >>= bits_to_write;
        bits -= bits_to_write;
    }
}

/// Read up to 64 bits from the buffer, little-endian.
static uint64_t dsdl_bitbuf_read(dsdl_bitbuf_t* const buf, uint64_t bits)
{
    if ((buf == NULL) || (bits == 0) || (bits > 64)) {
        return 0;
    }
    if (buf->error) {
        return 0;
    }
    if ((buf->offset_bits + bits) > buf->capacity_bits) {
        buf->error = true;
        return 0;
    }

    uint64_t value     = 0;
    uint64_t bit_shift = 0;

    while (bits > 0) {
        const uint64_t byte_index   = buf->offset_bits / 8;
        const uint64_t bit_in_byte  = buf->offset_bits % 8;
        const uint64_t bits_in_byte = 8 - bit_in_byte;
        const uint64_t bits_to_read = (bits < bits_in_byte) ? bits : bits_in_byte;

        // Mask for the bits we're reading
        const unsigned char mask = (unsigned char)((1U << bits_to_read) - 1U);
        const unsigned char val  = (unsigned char)((buf->data[byte_index] >> bit_in_byte) & mask);

        value |= ((uint64_t)val << bit_shift);

        buf->offset_bits += bits_to_read;
        bit_shift += bits_to_read;
        bits -= bits_to_read;
    }

    return value;
}

/// Align the buffer to the next byte boundary by writing zero padding bits.
static void dsdl_bitbuf_align_write(dsdl_bitbuf_t* const buf)
{
    if ((buf == NULL) || buf->error) {
        return;
    }
    const uint64_t remainder = buf->offset_bits % 8;
    if (remainder != 0) {
        dsdl_bitbuf_write(buf, 0, 8 - remainder);
    }
}

/// Align the buffer to the next byte boundary by skipping bits during read.
static void dsdl_bitbuf_align_read(dsdl_bitbuf_t* const buf)
{
    if ((buf == NULL) || buf->error) {
        return;
    }
    const uint64_t remainder = buf->offset_bits % 8;
    if (remainder != 0) {
        const uint64_t advance = 8 - remainder;
        if ((buf->offset_bits + advance) > buf->capacity_bits) {
            buf->error = true;
            return;
        }
        buf->offset_bits += advance;
    }
}

// ============================================================================
// Float16 conversion (IEEE 754 half-precision)
// ============================================================================

/// Helper union for float32 bit manipulation.
typedef union
{
    uint32_t bits;
    float    real;
} dsdl_float32_bits_t;

/// Pack a float32 into IEEE 754 half-precision (float16) format.
/// Based on Nunavut's nunavutFloat16Pack implementation.
static uint16_t dsdl_float16_pack(const float value)
{
    const uint32_t      round_mask = ~(uint32_t)0x0FFFU;
    dsdl_float32_bits_t f32inf;
    dsdl_float32_bits_t f16inf;
    dsdl_float32_bits_t magic;
    dsdl_float32_bits_t in;
    f32inf.bits         = ((uint32_t)255U) << 23U;
    f16inf.bits         = ((uint32_t)31U) << 23U;
    magic.bits          = ((uint32_t)15U) << 23U;
    in.real             = value;
    const uint32_t sign = in.bits & (((uint32_t)1U) << 31U);
    in.bits ^= sign;
    uint16_t out = 0;
    if (in.bits >= f32inf.bits) {
        if ((in.bits & 0x7FFFFFUL) != 0) {
            out = 0x7E00U; // NaN
        } else {
            out = (in.bits > f32inf.bits) ? (uint16_t)0x7FFFU : (uint16_t)0x7C00U; // Inf
        }
    } else {
        in.bits &= round_mask;
        in.real *= magic.real;
        in.bits -= round_mask;
        if (in.bits > f16inf.bits) {
            in.bits = f16inf.bits;
        }
        out = (uint16_t)(in.bits >> 13U);
    }
    out |= (uint16_t)(sign >> 16U);
    return out;
}

/// Unpack an IEEE 754 half-precision (float16) value into a float32.
/// Based on Nunavut's nunavutFloat16Unpack implementation.
static float dsdl_float16_unpack(const uint16_t value)
{
    dsdl_float32_bits_t magic;
    dsdl_float32_bits_t inf_nan;
    dsdl_float32_bits_t out;
    magic.bits   = ((uint32_t)0xEFU) << 23U;
    inf_nan.bits = ((uint32_t)0x8FU) << 23U;
    out.bits     = ((uint32_t)(value & 0x7FFFU)) << 13U;
    out.real *= magic.real;
    if (out.real >= inf_nan.real) {
        out.bits |= ((uint32_t)0xFFU) << 23U;
    }
    out.bits |= ((uint32_t)(value & 0x8000U)) << 16U;
    return out.real;
}

// ============================================================================
// Primitive serialization helpers
// ============================================================================

static bool dsdl_uint_fits_bits(const uint64_t value, const uint64_t bits)
{
    if (bits >= 64) {
        return true;
    }
    return value <= ((1ULL << bits) - 1ULL);
}

static bool dsdl_int_fits_bits(const int64_t value, const uint64_t bits)
{
    if (bits >= 64) {
        return (value >= INT64_MIN) && (value <= INT64_MAX);
    }
    const int64_t max = (int64_t)((1ULL << (bits - 1U)) - 1ULL);
    const int64_t min = -((int64_t)(1ULL << (bits - 1U)));
    return (value >= min) && (value <= max);
}

static uint64_t dsdl_uint_truncate(const uint64_t value, const uint64_t bits)
{
    if (bits >= 64U) {
        return value;
    }
    const uint64_t mask = (bits == 0U) ? 0U : ((1ULL << bits) - 1ULL);
    return value & mask;
}

static uint64_t dsdl_uint_saturate(const uint64_t value, const uint64_t bits)
{
    if (bits >= 64U) {
        return value;
    }
    const uint64_t max = (bits == 0U) ? 0U : ((1ULL << bits) - 1ULL);
    return (value > max) ? max : value;
}

static int64_t dsdl_int_saturate(const int64_t value, const uint64_t bits)
{
    if (bits >= 64U) {
        return value;
    }
    if (bits == 0U) {
        return 0;
    }
    const int64_t max = (int64_t)((1ULL << (bits - 1U)) - 1ULL);
    const int64_t min = -((int64_t)(1ULL << (bits - 1U)));
    if (value > max) {
        return max;
    }
    if (value < min) {
        return min;
    }
    return value;
}

static float dsdl_float16_saturate(const float value)
{
    if (!isfinite(value)) {
        return value;
    }
    const float limit = 65504.0F;
    if (value > limit) {
        return limit;
    }
    if (value < -limit) {
        return -limit;
    }
    return value;
}

/// Write a primitive value to the buffer based on its type.
static void dsdl_serialize_primitive(dsdl_bitbuf_t* const buf, const dsdl_type_t type, const void* const value)
{
    if ((buf == NULL) || (value == NULL)) {
        return;
    }
    if (buf->error) {
        return;
    }

    const uint64_t bits = dsdl_type_bit_width(type);
    if (bits == 0) {
        return;
    }

    // Handle void types (just write zeros)
    if (dsdl_type_is_void(type)) {
        dsdl_bitbuf_write(buf, 0, bits);
        return;
    }

    // Handle bool (alias for uint1)
    if (type == DSDL_BOOL) {
        const bool* b = (const bool*)value;
        dsdl_bitbuf_write(buf, *b ? 1 : 0, 1);
        return;
    }

    // Handle floats
    if (dsdl_type_is_float(type)) {
        if (bits == 16) {
            // float16 - stored as native float in memory, convert to IEEE 754 half-precision
            const float* f32  = (const float*)value;
            float        fval = *f32;
            if (!dsdl_type_is_truncated(type)) {
                fval = dsdl_float16_saturate(fval);
            }
            dsdl_bitbuf_write(buf, dsdl_float16_pack(fval), 16);
        } else if (bits == 32) {
            const float* f32 = (const float*)value;
            uint32_t     raw;
            memcpy(&raw, f32, sizeof(raw));
            dsdl_bitbuf_write(buf, raw, 32);
        } else if (bits == 64) {
            const double* f64 = (const double*)value;
            uint64_t      raw;
            memcpy(&raw, f64, sizeof(raw));
            dsdl_bitbuf_write(buf, raw, 64);
        }
        return;
    }

    // Handle integers (signed and unsigned)
    if (dsdl_type_is_int(type) || dsdl_type_is_uint(type) || (type == DSDL_BYTE)) {
        // Read the value based on size, then write the appropriate number of bits
        uint64_t raw = 0;
        if (dsdl_type_is_int(type)) {
            int64_t sval = 0;
            if (bits <= 8) {
                sval = *(const int_least8_t*)value;
            } else if (bits <= 16) {
                sval = *(const int_least16_t*)value;
            } else if (bits <= 32) {
                sval = *(const int_least32_t*)value;
            } else {
                sval = *(const int_least64_t*)value;
            }
            sval = dsdl_int_saturate(sval, bits);
            raw  = (uint64_t)sval;
        } else {
            uint64_t uval = 0;
            if (bits <= 8) {
                uval = *(const uint_least8_t*)value;
            } else if (bits <= 16) {
                uval = *(const uint_least16_t*)value;
            } else if (bits <= 32) {
                uval = *(const uint_least32_t*)value;
            } else {
                uval = *(const uint_least64_t*)value;
            }
            if (dsdl_type_is_truncated(type)) {
                uval = dsdl_uint_truncate(uval, bits);
            } else {
                uval = dsdl_uint_saturate(uval, bits);
            }
            raw = uval;
        }
        dsdl_bitbuf_write(buf, raw, bits);
        return;
    }
}

/// Read a primitive value from the buffer based on its type.
static void dsdl_deserialize_primitive(dsdl_bitbuf_t* const buf, const dsdl_type_t type, void* const value)
{
    if ((buf == NULL) || (value == NULL)) {
        return;
    }
    if (buf->error) {
        return;
    }

    const uint64_t bits = dsdl_type_bit_width(type);
    if (bits == 0) {
        return;
    }

    // Handle void types (just skip bits)
    if (dsdl_type_is_void(type)) {
        (void)dsdl_bitbuf_read(buf, bits);
        return;
    }

    // Handle bool (alias for uint1)
    if (type == DSDL_BOOL) {
        bool* b = (bool*)value;
        *b      = (dsdl_bitbuf_read(buf, 1) != 0);
        return;
    }

    // Handle floats
    if (dsdl_type_is_float(type)) {
        if (bits == 16) {
            // float16 - stored as native float in memory, convert from IEEE 754 half-precision
            float* f32 = (float*)value;
            *f32       = dsdl_float16_unpack((uint16_t)dsdl_bitbuf_read(buf, 16));
        } else if (bits == 32) {
            float*   f32 = (float*)value;
            uint32_t raw = (uint32_t)dsdl_bitbuf_read(buf, 32);
            memcpy(f32, &raw, sizeof(raw));
        } else if (bits == 64) {
            double*  f64 = (double*)value;
            uint64_t raw = dsdl_bitbuf_read(buf, 64);
            memcpy(f64, &raw, sizeof(raw));
        }
        return;
    }

    // Handle integers (signed and unsigned)
    if (dsdl_type_is_int(type) || dsdl_type_is_uint(type) || (type == DSDL_BYTE)) {
        uint64_t raw = dsdl_bitbuf_read(buf, bits);

        // Sign-extend for signed integers
        if (dsdl_type_is_int(type) && (bits < 64)) {
            const uint64_t sign_bit = (uint64_t)1 << (bits - 1);
            if (raw & sign_bit) {
                raw |= ~((uint64_t)0) << bits; // Sign extend
            }
        }

        // Store based on size
        if (bits <= 8) {
            *(uint_least8_t*)value = (uint_least8_t)raw;
        } else if (bits <= 16) {
            *(uint_least16_t*)value = (uint_least16_t)raw;
        } else if (bits <= 32) {
            *(uint_least32_t*)value = (uint_least32_t)raw;
        } else {
            *(uint_least64_t*)value = raw;
        }
        return;
    }
}

// ============================================================================
// Type serialization (recursive)
// ============================================================================

// Forward declarations for recursive serialization
static void dsdl_serialize_type(dsdl_bitbuf_t* buf, const dsdl_type_t* type_ptr, const void* value);
static void dsdl_deserialize_type(dsdl_bitbuf_t* buf, const dsdl_type_t* type_ptr, void* value);

/// Serialize composite content (without delimiter header).
/// For structs, value_ptr points to dsdl_value_struct_t.
/// For unions, value_ptr points to dsdl_value_union_t.
static void dsdl_serialize_composite_content(dsdl_bitbuf_t* const               buf,
                                             const dsdl_type_composite_t* const composite,
                                             const void* const                  value_ptr)
{
    if ((buf == NULL) || (composite == NULL) || (value_ptr == NULL) || buf->error) {
        return;
    }
    if (composite->type == DSDL_COMPOSITE_UNION) {
        // Union: value_ptr is dsdl_value_union_t*
        const dsdl_value_union_t* uval = (const dsdl_value_union_t*)value_ptr;
        const size_t              tag  = uval->tag;

        if (tag >= composite->field_count) {
            buf->error = true;
            return;
        }

        // Write tag bits
        const uint_least8_t tag_bits = dsdl_union_tag_bits(composite->field_count);
        dsdl_bitbuf_write(buf, tag, tag_bits);

        // Serialize selected variant (skip void fields when finding the value)
        // Note: unions typically don't have void alternatives, but handle it for completeness
        const dsdl_type_t* field_type = composite->field_types[tag];
        if (!dsdl_type_is_void(*field_type)) {
            dsdl_serialize_type(buf, field_type, uval->value);
        } else {
            // Void field: just write zero bits
            dsdl_bitbuf_write(buf, 0, dsdl_type_bit_width(*field_type));
        }
    } else {
        // Struct: value_ptr is dsdl_value_struct_t*
        const dsdl_value_struct_t* sval = (const dsdl_value_struct_t*)value_ptr;

        // The values array contains only non-void fields, so we track a separate value_index
        size_t value_index = 0;
        for (size_t i = 0; i < composite->field_count; i++) {
            const dsdl_type_t*  field_type = composite->field_types[i];
            const uint_least8_t alignment  = dsdl_type_alignment_bits(field_type);
            if (alignment > 1U) {
                dsdl_bitbuf_align_write(buf);
                if (buf->error) {
                    return;
                }
            }

            if (dsdl_type_is_void(*field_type)) {
                // Void field: serialize zero bits, don't consume from values array
                dsdl_bitbuf_write(buf, 0, dsdl_type_bit_width(*field_type));
            } else {
                // Non-void field: serialize from values array
                dsdl_serialize_type(buf, field_type, sval->values[value_index]);
                value_index++;
            }
        }
    }
}

/// Serialize a composite type (struct or union).
/// For delimited (non-sealed) composites, writes a 32-bit delimiter header.
static void dsdl_serialize_composite(dsdl_bitbuf_t* const               buf,
                                     const dsdl_type_composite_t* const composite,
                                     const void* const                  value)
{
    if ((buf == NULL) || (composite == NULL) || (value == NULL)) {
        return;
    }

    // Serialize the content
    dsdl_serialize_composite_content(buf, composite, value);
    if (buf->error) {
        return;
    }
    // Composite alignment is 8 bits.
    dsdl_bitbuf_align_write(buf);
}

/// Deserialize composite content (without delimiter header).
/// For structs, value_ptr points to dsdl_value_struct_t.
/// For unions, value_ptr points to dsdl_value_union_t.
static void dsdl_deserialize_composite_content(dsdl_bitbuf_t* const               buf,
                                               const dsdl_type_composite_t* const composite,
                                               void* const                        value_ptr)
{
    if ((buf == NULL) || (composite == NULL) || (value_ptr == NULL) || buf->error) {
        return;
    }
    if (composite->type == DSDL_COMPOSITE_UNION) {
        // Union: value_ptr is dsdl_value_union_t*
        dsdl_value_union_t* uval = (dsdl_value_union_t*)value_ptr;

        // Read tag
        const uint_least8_t tag_bits = dsdl_union_tag_bits(composite->field_count);
        size_t              tag      = (size_t)dsdl_bitbuf_read(buf, tag_bits);
        if (buf->error) {
            return;
        }

        if (tag >= composite->field_count) {
            buf->error = true;
            return;
        }

        // Store tag
        uval->tag = tag;

        // Deserialize selected variant
        const dsdl_type_t* field_type = composite->field_types[tag];
        if (!dsdl_type_is_void(*field_type)) {
            dsdl_deserialize_type(buf, field_type, uval->value);
        } else {
            // Void field: just skip bits
            (void)dsdl_bitbuf_read(buf, dsdl_type_bit_width(*field_type));
        }
    } else {
        // Struct: value_ptr is dsdl_value_struct_t*
        dsdl_value_struct_t* sval = (dsdl_value_struct_t*)value_ptr;

        // The values array contains only non-void fields, so we track a separate value_index
        size_t value_index = 0;
        for (size_t i = 0; i < composite->field_count; i++) {
            const dsdl_type_t*  field_type = composite->field_types[i];
            const uint_least8_t alignment  = dsdl_type_alignment_bits(field_type);
            if (alignment > 1U) {
                dsdl_bitbuf_align_read(buf);
                if (buf->error) {
                    return;
                }
            }

            if (dsdl_type_is_void(*field_type)) {
                // Void field: skip bits, don't consume from values array
                (void)dsdl_bitbuf_read(buf, dsdl_type_bit_width(*field_type));
            } else {
                // Non-void field: deserialize into values array
                dsdl_deserialize_type(buf, field_type, sval->values[value_index]);
                value_index++;
            }
        }
    }
}

/// Deserialize a composite type (struct or union).
static void dsdl_deserialize_composite(dsdl_bitbuf_t* const               buf,
                                       const dsdl_type_composite_t* const composite,
                                       void* const                        value)
{
    if ((buf == NULL) || (composite == NULL) || (value == NULL)) {
        return;
    }

    // Deserialize the content
    dsdl_deserialize_composite_content(buf, composite, value);
    if (buf->error) {
        return;
    }
    // Composite alignment is 8 bits.
    dsdl_bitbuf_align_read(buf);
}

/// Serialize any type (primitive, array, or composite).
static void dsdl_serialize_type(dsdl_bitbuf_t* const buf, const dsdl_type_t* const type_ptr, const void* const value)
{
    if ((buf == NULL) || (type_ptr == NULL)) {
        return;
    }
    if (buf->error) {
        return;
    }
    if (value == NULL) {
        buf->error = true;
        return;
    }

    const dsdl_type_t kind = *type_ptr;

    // Primitive types
    if (dsdl_type_is_void(kind) || dsdl_type_is_int(kind) || dsdl_type_is_uint(kind) || dsdl_type_is_float(kind) ||
        (kind == DSDL_BOOL) || (kind == DSDL_BYTE)) {
        dsdl_serialize_primitive(buf, kind, value);
        return;
    }

    // Handle aliases
    if (dsdl_type_is_alias(kind)) {
        const dsdl_type_t base = (dsdl_type_t)(kind & ~DSDL_TYPE_ALIAS_MASK);
        dsdl_serialize_primitive(buf, base, value);
        return;
    }

    // Array types
    if (dsdl_type_is_array(kind)) {
        const dsdl_type_array_t* arr = (const dsdl_type_array_t*)type_ptr;

        // Get native storage size of each element
        const uint64_t element_size = dsdl_type_native_size(arr->member_type);

        if (kind == DSDL_ARRAY_VARIABLE) {
            // Variable array: value is dsdl_value_array_variable_t*
            const dsdl_value_array_variable_t* var = (const dsdl_value_array_variable_t*)value;

            // Write length prefix
            const uint_least8_t prefix_bits = dsdl_array_length_prefix_bits(arr->capacity);
            if (var->count > arr->capacity) {
                buf->error = true;
                return;
            }
            if ((var->count > 0) && (var->members == NULL)) {
                buf->error = true;
                return;
            }
            const uint64_t actual_count = var->count;
            dsdl_bitbuf_write(buf, actual_count, prefix_bits);

            // Write elements from members pointer
            for (uint64_t i = 0; i < actual_count; i++) {
                const void* elem = (const char*)var->members + (i * element_size);
                dsdl_serialize_type(buf, arr->member_type, elem);
            }
        } else {
            // Fixed array: value is void* pointing directly to elements
            for (uint64_t i = 0; i < arr->capacity; i++) {
                const void* elem = (const char*)value + (i * element_size);
                dsdl_serialize_type(buf, arr->member_type, elem);
            }
        }
        return;
    }

    // Composite types
    if (dsdl_type_is_composite(kind)) {
        const dsdl_type_composite_t* composite = (const dsdl_type_composite_t*)type_ptr;

        if (composite->sealed) {
            // Sealed composite: serialize content directly (bit-packed)
            dsdl_serialize_composite_content(buf, composite, value);
            if (buf->error) {
                return;
            }
            dsdl_bitbuf_align_write(buf);
        } else {
            // Delimited composite: byte-align, write 32-bit delimiter, then byte-aligned content
            dsdl_bitbuf_align_write(buf);
            if (buf->error) {
                return;
            }

            // Remember position for delimiter
            const uint64_t delimiter_byte_pos64 = buf->offset_bits / 8U;
            if (delimiter_byte_pos64 > SIZE_MAX) {
                buf->error = true;
                return;
            }
            const size_t delimiter_byte_pos = (size_t)delimiter_byte_pos64;

            // Write placeholder delimiter (32 bits = 4 bytes)
            dsdl_bitbuf_write(buf, 0, 32);
            if (buf->error) {
                return;
            }

            // Serialize content
            const uint64_t content_start_bits = buf->offset_bits;
            dsdl_serialize_composite_content(buf, composite, value);
            if (buf->error) {
                return;
            }

            // Byte-align content
            dsdl_bitbuf_align_write(buf);
            if (buf->error) {
                return;
            }

            // Calculate content size and update delimiter
            const uint64_t content_bytes = (buf->offset_bits - content_start_bits + 7U) / 8U;
            if (content_bytes > UINT32_MAX) {
                buf->error = true;
                return;
            }
            // Write delimiter (little-endian 32-bit)
            buf->data[delimiter_byte_pos + 0] = (unsigned char)(content_bytes & 0xFFU);
            buf->data[delimiter_byte_pos + 1] = (unsigned char)((content_bytes >> 8) & 0xFFU);
            buf->data[delimiter_byte_pos + 2] = (unsigned char)((content_bytes >> 16) & 0xFFU);
            buf->data[delimiter_byte_pos + 3] = (unsigned char)((content_bytes >> 24) & 0xFFU);
        }
        return;
    }
}

/// Deserialize any type (primitive, array, or composite).
static void dsdl_deserialize_type(dsdl_bitbuf_t* const buf, const dsdl_type_t* const type_ptr, void* const value)
{
    if ((buf == NULL) || (type_ptr == NULL)) {
        return;
    }
    if (buf->error) {
        return;
    }
    if (value == NULL) {
        buf->error = true;
        return;
    }

    const dsdl_type_t kind = *type_ptr;

    // Primitive types
    if (dsdl_type_is_void(kind) || dsdl_type_is_int(kind) || dsdl_type_is_uint(kind) || dsdl_type_is_float(kind) ||
        (kind == DSDL_BOOL) || (kind == DSDL_BYTE)) {
        dsdl_deserialize_primitive(buf, kind, value);
        return;
    }

    // Handle aliases
    if (dsdl_type_is_alias(kind)) {
        const dsdl_type_t base = (dsdl_type_t)(kind & ~DSDL_TYPE_ALIAS_MASK);
        dsdl_deserialize_primitive(buf, base, value);
        return;
    }

    // Array types
    if (dsdl_type_is_array(kind)) {
        const dsdl_type_array_t* arr = (const dsdl_type_array_t*)type_ptr;

        // Get native storage size of each element
        const uint64_t element_size = dsdl_type_native_size(arr->member_type);

        if (kind == DSDL_ARRAY_VARIABLE) {
            // Variable array: value is dsdl_value_array_variable_t*
            dsdl_value_array_variable_t* var = (dsdl_value_array_variable_t*)value;

            // Read length prefix
            const uint_least8_t prefix_bits    = dsdl_array_length_prefix_bits(arr->capacity);
            uint64_t            count_from_msg = dsdl_bitbuf_read(buf, prefix_bits);
            if (buf->error) {
                return;
            }

            // Validate against type capacity (message can't exceed type definition)
            if (count_from_msg > arr->capacity) {
                buf->error = true;
                return;
            }

            // Fail if user buffer is too small
            if (count_from_msg > var->count) {
                buf->error = true;
                return;
            }
            if ((count_from_msg > 0) && (var->members == NULL)) {
                buf->error = true;
                return;
            }

            // Update count to actual deserialized elements
            if (count_from_msg > SIZE_MAX) {
                buf->error = true;
                return;
            }
            var->count = (size_t)count_from_msg;

            // Read elements into members buffer
            for (uint64_t i = 0; i < count_from_msg; i++) {
                void* elem = (char*)var->members + (i * element_size);
                dsdl_deserialize_type(buf, arr->member_type, elem);
            }
        } else {
            // Fixed array: value is void* pointing directly to elements
            for (uint64_t i = 0; i < arr->capacity; i++) {
                void* elem = (char*)value + (i * element_size);
                dsdl_deserialize_type(buf, arr->member_type, elem);
            }
        }
        return;
    }

    // Composite types
    if (dsdl_type_is_composite(kind)) {
        const dsdl_type_composite_t* composite = (const dsdl_type_composite_t*)type_ptr;

        if (composite->sealed) {
            // Sealed composite: deserialize content directly (bit-packed)
            dsdl_deserialize_composite_content(buf, composite, value);
            if (buf->error) {
                return;
            }
            dsdl_bitbuf_align_read(buf);
        } else {
            // Delimited composite: byte-align, read 32-bit delimiter, then deserialize content
            dsdl_bitbuf_align_read(buf);
            if (buf->error) {
                return;
            }

            // Read delimiter (32 bits = content size in bytes)
            const uint32_t delimiter = (uint32_t)dsdl_bitbuf_read(buf, 32);
            if (buf->error) {
                return;
            }

            // Remember where content starts
            const uint64_t content_start_bits = buf->offset_bits;
            const uint64_t content_end_bits   = content_start_bits + ((uint64_t)delimiter * 8U);
            if (content_end_bits > buf->capacity_bits) {
                buf->error = true;
                return;
            }

            // Deserialize content
            dsdl_deserialize_composite_content(buf, composite, value);
            if (buf->error) {
                return;
            }

            if (buf->offset_bits > content_end_bits) {
                buf->error = true;
                return;
            }

            // Skip to end of delimited content (in case there's extra data from newer version)
            if (content_end_bits > buf->offset_bits) {
                buf->offset_bits = content_end_bits;
            }

            // Byte-align after content
            dsdl_bitbuf_align_read(buf);
        }
        return;
    }
}

// ============================================================================
// Public serialization API
// ============================================================================

size_t dsdl_serialize(const dsdl_type_composite_t* const type,
                      const void* const                  value,
                      const size_t                       output_size,
                      void* const                        output)
{
    if ((type == NULL) || (value == NULL) || (output == NULL) || (output_size == 0)) {
        return SIZE_MAX;
    }

    // Zero-initialize output buffer
    memset(output, 0, output_size);

    dsdl_bitbuf_t buf = {
        .data          = (unsigned char*)output,
        .capacity_bits = output_size * 8,
        .offset_bits   = 0,
        .error         = false,
    };

    // Serialize the composite type with the provided value
    dsdl_serialize_composite(&buf, type, value);

    if (buf.error) {
        return SIZE_MAX;
    }

    // Return bytes written (rounded up)
    const uint64_t bytes_written = (buf.offset_bits + 7U) / 8U;
    if (bytes_written > SIZE_MAX) {
        return SIZE_MAX;
    }
    return (size_t)bytes_written;
}

size_t dsdl_deserialize(const dsdl_type_composite_t* const type,
                        void* const                        value,
                        const size_t                       input_size,
                        const void* const                  input)
{
    if ((type == NULL) || (value == NULL) || (input == NULL) || (input_size == 0)) {
        return SIZE_MAX;
    }

    dsdl_bitbuf_t buf = {
        .data          = (unsigned char*)(uintptr_t)input, // Cast away const for the buffer struct
        .capacity_bits = input_size * 8,
        .offset_bits   = 0,
        .error         = false,
    };

    // Deserialize the composite type into the provided value buffer
    dsdl_deserialize_composite(&buf, type, value);

    // Return SIZE_MAX on error, otherwise bytes consumed (rounded up)
    if (buf.error) {
        return SIZE_MAX;
    }
    const uint64_t bytes_read = (buf.offset_bits + 7U) / 8U;
    if (bytes_read > SIZE_MAX) {
        return SIZE_MAX;
    }
    return (size_t)bytes_read;
}
