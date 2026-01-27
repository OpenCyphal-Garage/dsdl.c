/// Tests for rational arithmetic implementation
///
/// These tests access internal functions by including dsdl.c directly.

// Include implementation to access internal functions
// This pattern is used in libcanard/libudpard test suites
#include "dsdl.c"

#include "unity.h"

// ============================================================================
// Rational arithmetic tests
// ============================================================================

static void assert_intmax_eq(const intmax_t expected, const intmax_t actual)
{
#ifdef UNITY_SUPPORT_64
    TEST_ASSERT_EQUAL_INT64(expected, actual);
#else
    TEST_ASSERT_EQUAL_INT32((int32_t)expected, (int32_t)actual);
#endif
}

static void assert_uintmax_eq(const uintmax_t expected, const uintmax_t actual)
{
#ifdef UNITY_SUPPORT_64
    TEST_ASSERT_EQUAL_UINT64(expected, actual);
#else
    TEST_ASSERT_EQUAL_UINT32((uint32_t)expected, (uint32_t)actual);
#endif
}

static void assert_bigint_eq_intmax(const intmax_t expected, const dsdl_bigint_t actual)
{
    intmax_t got = 0;
    TEST_ASSERT_TRUE(dsdl_bigint_to_intmax(&actual, &got));
    assert_intmax_eq(expected, got);
}

static void assert_bigint_eq_uintmax(const uintmax_t expected, const dsdl_bigint_t actual)
{
    uintmax_t got = 0;
    TEST_ASSERT_TRUE(dsdl_bigint_to_uintmax(&actual, &got));
    assert_uintmax_eq(expected, got);
}

static dsdl_rational_t make_rational(const intmax_t num, const uintmax_t den)
{
    dsdl_rational_t r;
    TEST_ASSERT_TRUE(dsdl_bigint_from_intmax(&r.num, num));
    TEST_ASSERT_TRUE(dsdl_bigint_from_uintmax(&r.den, den));
    return r;
}

void test_rational_from_int(void)
{
    dsdl_rational_t r = dsdl_rational_from_int(42);
    assert_bigint_eq_intmax(42, r.num);
    assert_bigint_eq_uintmax(1, r.den);

    r = dsdl_rational_from_int(-123);
    assert_bigint_eq_intmax(-123, r.num);
    assert_bigint_eq_uintmax(1, r.den);

    r = dsdl_rational_from_int(0);
    assert_bigint_eq_intmax(0, r.num);
    assert_bigint_eq_uintmax(1, r.den);
}

void test_rational_normalize(void)
{
    // Already normalized
    dsdl_rational_t r = make_rational(6, 1);
    r                 = dsdl_rational_normalize(r);
    assert_bigint_eq_intmax(6, r.num);
    assert_bigint_eq_uintmax(1, r.den);

    // Reduce 6/2 = 3/1
    r = make_rational(6, 2);
    r = dsdl_rational_normalize(r);
    assert_bigint_eq_intmax(3, r.num);
    assert_bigint_eq_uintmax(1, r.den);

    // Reduce 12/8 = 3/2
    r = make_rational(12, 8);
    r = dsdl_rational_normalize(r);
    assert_bigint_eq_intmax(3, r.num);
    assert_bigint_eq_uintmax(2, r.den);

    // Negative numerator: -6/4 = -3/2
    r = make_rational(-6, 4);
    r = dsdl_rational_normalize(r);
    assert_bigint_eq_intmax(-3, r.num);
    assert_bigint_eq_uintmax(2, r.den);

    // Zero numerator: 0/5 = 0/1
    r = make_rational(0, 5);
    r = dsdl_rational_normalize(r);
    assert_bigint_eq_intmax(0, r.num);
    assert_bigint_eq_uintmax(1, r.den);

    // NaN (denominator 0) stays NaN
    r = make_rational(5, 0);
    r = dsdl_rational_normalize(r);
    assert_bigint_eq_uintmax(0, r.den);
}

void test_rational_add(void)
{
    // 1/2 + 1/3 = 5/6
    dsdl_rational_t a = make_rational(1, 2);
    dsdl_rational_t b = make_rational(1, 3);
    dsdl_rational_t r = dsdl_rational_add(a, b);
    assert_bigint_eq_intmax(5, r.num);
    assert_bigint_eq_uintmax(6, r.den);

    // 1/4 + 1/4 = 1/2
    a = make_rational(1, 4);
    b = make_rational(1, 4);
    r = dsdl_rational_add(a, b);
    assert_bigint_eq_intmax(1, r.num);
    assert_bigint_eq_uintmax(2, r.den);

    // 3 + 4 = 7 (integers)
    a = dsdl_rational_from_int(3);
    b = dsdl_rational_from_int(4);
    r = dsdl_rational_add(a, b);
    assert_bigint_eq_intmax(7, r.num);
    assert_bigint_eq_uintmax(1, r.den);

    // -1/2 + 1/2 = 0
    a = make_rational(-1, 2);
    b = make_rational(1, 2);
    r = dsdl_rational_add(a, b);
    assert_bigint_eq_intmax(0, r.num);
}

void test_rational_sub(void)
{
    // 1/2 - 1/3 = 1/6
    dsdl_rational_t a = make_rational(1, 2);
    dsdl_rational_t b = make_rational(1, 3);
    dsdl_rational_t r = dsdl_rational_sub(a, b);
    assert_bigint_eq_intmax(1, r.num);
    assert_bigint_eq_uintmax(6, r.den);

    // 5 - 3 = 2 (integers)
    a = dsdl_rational_from_int(5);
    b = dsdl_rational_from_int(3);
    r = dsdl_rational_sub(a, b);
    assert_bigint_eq_intmax(2, r.num);
    assert_bigint_eq_uintmax(1, r.den);

    // 1/4 - 1/4 = 0
    a = make_rational(1, 4);
    b = make_rational(1, 4);
    r = dsdl_rational_sub(a, b);
    assert_bigint_eq_intmax(0, r.num);
}

void test_rational_mul(void)
{
    // 2/3 * 3/4 = 1/2
    dsdl_rational_t a = make_rational(2, 3);
    dsdl_rational_t b = make_rational(3, 4);
    dsdl_rational_t r = dsdl_rational_mul(a, b);
    assert_bigint_eq_intmax(1, r.num);
    assert_bigint_eq_uintmax(2, r.den);

    // 3 * 4 = 12 (integers)
    a = dsdl_rational_from_int(3);
    b = dsdl_rational_from_int(4);
    r = dsdl_rational_mul(a, b);
    assert_bigint_eq_intmax(12, r.num);
    assert_bigint_eq_uintmax(1, r.den);

    // -2/3 * 3/5 = -2/5
    a = make_rational(-2, 3);
    b = make_rational(3, 5);
    r = dsdl_rational_mul(a, b);
    assert_bigint_eq_intmax(-2, r.num);
    assert_bigint_eq_uintmax(5, r.den);

    // Multiply by zero
    a = make_rational(5, 7);
    b = dsdl_rational_from_int(0);
    r = dsdl_rational_mul(a, b);
    assert_bigint_eq_intmax(0, r.num);
}

void test_rational_div(void)
{
    // (2/3) / (4/5) = 10/12 = 5/6
    dsdl_rational_t a = make_rational(2, 3);
    dsdl_rational_t b = make_rational(4, 5);
    dsdl_rational_t r = dsdl_rational_div(a, b);
    assert_bigint_eq_intmax(5, r.num);
    assert_bigint_eq_uintmax(6, r.den);

    // 6 / 2 = 3 (integers)
    a = dsdl_rational_from_int(6);
    b = dsdl_rational_from_int(2);
    r = dsdl_rational_div(a, b);
    assert_bigint_eq_intmax(3, r.num);
    assert_bigint_eq_uintmax(1, r.den);

    // Division by negative: 1/2 / (-1/3) = -3/2
    a = make_rational(1, 2);
    b = make_rational(-1, 3);
    r = dsdl_rational_div(a, b);
    assert_bigint_eq_intmax(-3, r.num);
    assert_bigint_eq_uintmax(2, r.den);

    // Division by zero -> NaN
    a = make_rational(5, 7);
    b = dsdl_rational_from_int(0);
    r = dsdl_rational_div(a, b);
    assert_bigint_eq_uintmax(0, r.den); // NaN indicator
}

void test_rational_cmp(void)
{
    dsdl_rational_t a;
    dsdl_rational_t b;

    // 1/2 < 2/3
    a = make_rational(1, 2);
    b = make_rational(2, 3);
    TEST_ASSERT_TRUE(dsdl_rational_cmp(a, b) < 0);

    // 2/3 > 1/2
    TEST_ASSERT_TRUE(dsdl_rational_cmp(b, a) > 0);

    // 1/2 == 2/4
    a = make_rational(1, 2);
    b = make_rational(2, 4);
    b = dsdl_rational_normalize(b); // Normalize to 1/2
    TEST_ASSERT_EQUAL_INT(0, dsdl_rational_cmp(a, b));

    // -1 < 1
    a = dsdl_rational_from_int(-1);
    b = dsdl_rational_from_int(1);
    TEST_ASSERT_TRUE(dsdl_rational_cmp(a, b) < 0);

    // 0 == 0
    a = dsdl_rational_from_int(0);
    b = dsdl_rational_from_int(0);
    TEST_ASSERT_EQUAL_INT(0, dsdl_rational_cmp(a, b));
}

// ============================================================================
// Overflow handling tests
// ============================================================================

void test_rational_overflow_mul(void)
{
    // Multiplying large values should remain exact within bigint range.
    dsdl_rational_t a = dsdl_rational_from_int(INTMAX_MAX / 2);
    dsdl_rational_t b = dsdl_rational_from_int(4);
    dsdl_rational_t r = dsdl_rational_mul(a, b);

    const uintmax_t expected = ((uintmax_t)(INTMAX_MAX / 2)) * 4U;
    assert_bigint_eq_uintmax(expected, r.num);
    assert_bigint_eq_uintmax(1, r.den);
}

void test_rational_overflow_add(void)
{
    // Adding values near INTMAX_MAX should remain exact within bigint range.
    dsdl_rational_t a = dsdl_rational_from_int(INTMAX_MAX / 2);
    dsdl_rational_t b = dsdl_rational_from_int(INTMAX_MAX / 2);
    dsdl_rational_t r = dsdl_rational_add(a, b);

    const uintmax_t expected = ((uintmax_t)(INTMAX_MAX / 2)) * 2U;
    assert_bigint_eq_uintmax(expected, r.num);
    assert_bigint_eq_uintmax(1, r.den);
}

void test_rational_overflow_large_denominators(void)
{
    // Large denominators that would overflow when multiplied
    dsdl_rational_t a = make_rational(1, UINTMAX_MAX / 2);
    dsdl_rational_t b = make_rational(1, UINTMAX_MAX / 2);
    dsdl_rational_t r = dsdl_rational_mul(a, b);

    assert_bigint_eq_intmax(1, r.num);
    TEST_ASSERT_FALSE(dsdl_bigint_is_zero(&r.den));
}

void test_rational_overflow_preserves_sign(void)
{
    // Negative overflow should preserve sign
    dsdl_rational_t a = dsdl_rational_from_int(INTMAX_MIN / 2);
    dsdl_rational_t b = dsdl_rational_from_int(3);
    dsdl_rational_t r = dsdl_rational_mul(a, b);

    // Result should be negative
    TEST_ASSERT_TRUE(r.num.negative);
    TEST_ASSERT_FALSE(dsdl_bigint_is_zero(&r.num));
    TEST_ASSERT_FALSE(dsdl_bigint_is_zero(&r.den));
}

void test_rational_halve_approximation(void)
{
    // Test that normalization reduces fractions exactly.
    dsdl_rational_t r = make_rational(8, 16);
    r                 = dsdl_rational_normalize(r);
    assert_bigint_eq_intmax(1, r.num);
    assert_bigint_eq_uintmax(2, r.den);

    r = make_rational(1000000, 8000000); // 1/8
    r = dsdl_rational_normalize(r);
    assert_bigint_eq_intmax(1, r.num);
    assert_bigint_eq_uintmax(8, r.den);
}

void test_rational_intmax_min_negate(void)
{
    // Negating INTMAX_MIN is tricky - test we handle it
    dsdl_rational_t a = make_rational(INTMAX_MIN, 1);
    dsdl_rational_t r = dsdl_rational_neg(a);

    // Should be positive with valid denominator.
    TEST_ASSERT_FALSE(r.num.negative);
    TEST_ASSERT_FALSE(dsdl_bigint_is_zero(&r.num));
    TEST_ASSERT_FALSE(dsdl_bigint_is_zero(&r.den));
}

void test_rational_div_large_denominator(void)
{
    // Division where denominator > INTMAX_MAX
    // 1 / (1/UINTMAX_MAX) should give UINTMAX_MAX
    dsdl_rational_t a = make_rational(1, 1);
    dsdl_rational_t b = make_rational(1, UINTMAX_MAX);
    dsdl_rational_t r = dsdl_rational_div(a, b);

    assert_bigint_eq_uintmax(UINTMAX_MAX, r.num);
    assert_bigint_eq_uintmax(1, r.den);
}

void test_rational_add_large_denominator(void)
{
    // Addition where denominator > INTMAX_MAX
    dsdl_rational_t a = make_rational(1, UINTMAX_MAX);
    dsdl_rational_t b = make_rational(1, UINTMAX_MAX);
    dsdl_rational_t r = dsdl_rational_add(a, b);

    assert_bigint_eq_intmax(2, r.num);
    assert_bigint_eq_uintmax(UINTMAX_MAX, r.den);
}

void test_rational_cmp_large_denominator(void)
{
    // Comparison where denominator > INTMAX_MAX
    dsdl_rational_t a = make_rational(1, UINTMAX_MAX);
    dsdl_rational_t b = make_rational(2, UINTMAX_MAX);

    // a < b (1/UINTMAX_MAX < 2/UINTMAX_MAX)
    TEST_ASSERT_TRUE(dsdl_rational_cmp(a, b) < 0);
    TEST_ASSERT_TRUE(dsdl_rational_cmp(b, a) > 0);

    // Equal comparison
    dsdl_rational_t c = make_rational(1, UINTMAX_MAX);
    TEST_ASSERT_EQUAL_INT(0, dsdl_rational_cmp(a, c));
}

// ============================================================================
// Overflow fallback tests (lines 906-916)
// ============================================================================

void test_rational_cmp_overflow_fallback_positive(void)
{
    dsdl_rational_t a;
    dsdl_rational_t b;

    a.num.limb_count = 5U;
    a.num.limbs[0]   = 999999999U;
    a.num.limbs[1]   = 999999999U;
    a.num.limbs[2]   = 999999999U;
    a.num.limbs[3]   = 999999999U;
    a.num.limbs[4]   = 999999999U;
    a.num.negative   = false;

    a.den.limb_count = 1U;
    a.den.limbs[0]   = 1U;

    b.num.limb_count = 5U;
    b.num.limbs[0]   = 100000000U;
    b.num.limbs[1]   = 100000000U;
    b.num.limbs[2]   = 100000000U;
    b.num.limbs[3]   = 100000000U;
    b.num.limbs[4]   = 100000000U;
    b.num.negative   = false;

    b.den.limb_count = 1U;
    b.den.limbs[0]   = 1U;

    int cmp = dsdl_rational_cmp(a, b);
    TEST_ASSERT_TRUE(cmp > 0);
}

void test_rational_cmp_overflow_fallback_negative(void)
{
    dsdl_rational_t a;
    dsdl_rational_t b;

    a.num.limb_count = 5U;
    a.num.limbs[0]   = 123456789U;
    a.num.limbs[1]   = 987654321U;
    a.num.limbs[2]   = 111111111U;
    a.num.limbs[3]   = 222222222U;
    a.num.limbs[4]   = 333333333U;
    a.num.negative   = true;

    a.den.limb_count = 5U;
    a.den.limbs[0]   = 100000000U;
    a.den.limbs[1]   = 200000000U;
    a.den.limbs[2]   = 300000000U;
    a.den.limbs[3]   = 400000000U;
    a.den.limbs[4]   = 500000000U;

    b.num.limb_count = 5U;
    b.num.limbs[0]   = 111111111U;
    b.num.limbs[1]   = 222222222U;
    b.num.limbs[2]   = 333333333U;
    b.num.limbs[3]   = 444444444U;
    b.num.limbs[4]   = 555555555U;
    b.num.negative   = false;

    b.den.limb_count = 5U;
    b.den.limbs[0]   = 100000000U;
    b.den.limbs[1]   = 100000000U;
    b.den.limbs[2]   = 100000000U;
    b.den.limbs[3]   = 100000000U;
    b.den.limbs[4]   = 100000000U;

    int cmp = dsdl_rational_cmp(a, b);
    TEST_ASSERT_TRUE(cmp < 0);
}

void test_rational_cmp_overflow_fallback_equal(void)
{
    dsdl_rational_t a;
    dsdl_rational_t b;

    a.num.limb_count = 5U;
    a.num.limbs[0]   = 123456789U;
    a.num.limbs[1]   = 987654321U;
    a.num.limbs[2]   = 111111111U;
    a.num.limbs[3]   = 222222222U;
    a.num.limbs[4]   = 333333333U;
    a.num.negative   = false;

    a.den.limb_count = 5U;
    a.den.limbs[0]   = 100000000U;
    a.den.limbs[1]   = 200000000U;
    a.den.limbs[2]   = 300000000U;
    a.den.limbs[3]   = 400000000U;
    a.den.limbs[4]   = 500000000U;

    b.num.limb_count = 5U;
    b.num.limbs[0]   = 123456789U;
    b.num.limbs[1]   = 987654321U;
    b.num.limbs[2]   = 111111111U;
    b.num.limbs[3]   = 222222222U;
    b.num.limbs[4]   = 333333333U;
    b.num.negative   = false;

    b.den.limb_count = 5U;
    b.den.limbs[0]   = 100000000U;
    b.den.limbs[1]   = 200000000U;
    b.den.limbs[2]   = 300000000U;
    b.den.limbs[3]   = 400000000U;
    b.den.limbs[4]   = 500000000U;

    int cmp = dsdl_rational_cmp(a, b);
    TEST_ASSERT_EQUAL_INT(0, cmp);
}

// ============================================================================
// dsdl_rational_is_nan edge case tests
// ============================================================================

void test_rational_is_nan_zero_denominator(void)
{
    dsdl_rational_t r;
    TEST_ASSERT_TRUE(dsdl_bigint_from_intmax(&r.num, 42));
    TEST_ASSERT_TRUE(dsdl_bigint_from_uintmax(&r.den, 0U));

    TEST_ASSERT_TRUE(dsdl_rational_is_nan(r));
}

void test_rational_is_nan_valid_rational(void)
{
    dsdl_rational_t r = make_rational(1, 2);
    TEST_ASSERT_FALSE(dsdl_rational_is_nan(r));
}

void test_rational_is_nan_zero_numerator(void)
{
    dsdl_rational_t r = make_rational(0, 1);
    TEST_ASSERT_FALSE(dsdl_rational_is_nan(r));
}

void test_rational_is_nan_large_denominator(void)
{
    dsdl_rational_t r = make_rational(1, UINTMAX_MAX);
    TEST_ASSERT_FALSE(dsdl_rational_is_nan(r));
}

// ============================================================================
// dsdl_rational_from_double edge case tests
// ============================================================================

void test_rational_from_double_zero(void)
{
    dsdl_rational_t r = dsdl_rational_from_double(0.0);
    assert_bigint_eq_intmax(0, r.num);
    assert_bigint_eq_uintmax(1, r.den);
}

void test_rational_from_double_negative_zero(void)
{
    dsdl_rational_t r = dsdl_rational_from_double(-0.0);
    assert_bigint_eq_intmax(0, r.num);
    assert_bigint_eq_uintmax(1, r.den);
}

void test_rational_from_double_one(void)
{
    dsdl_rational_t r = dsdl_rational_from_double(1.0);
    assert_bigint_eq_intmax(1, r.num);
    assert_bigint_eq_uintmax(1, r.den);
}

void test_rational_from_double_negative_one(void)
{
    dsdl_rational_t r = dsdl_rational_from_double(-1.0);
    assert_bigint_eq_intmax(-1, r.num);
    assert_bigint_eq_uintmax(1, r.den);
}

void test_rational_from_double_half(void)
{
    dsdl_rational_t r = dsdl_rational_from_double(0.5);
    assert_bigint_eq_intmax(1, r.num);
    assert_bigint_eq_uintmax(2, r.den);
}

void test_rational_from_double_very_small(void)
{
    double          x = 1.0e-50;
    dsdl_rational_t r = dsdl_rational_from_double(x);

    TEST_ASSERT_FALSE(dsdl_rational_is_nan(r));
    TEST_ASSERT_FALSE(r.num.negative);
    TEST_ASSERT_FALSE(dsdl_bigint_is_zero(&r.num));
}

void test_rational_from_double_infinity(void)
{
    dsdl_rational_t r = dsdl_rational_from_double((double)INFINITY);
    TEST_ASSERT_TRUE(dsdl_rational_is_nan(r));
}

void test_rational_from_double_negative_infinity(void)
{
    dsdl_rational_t r = dsdl_rational_from_double(-(double)INFINITY);
    TEST_ASSERT_TRUE(dsdl_rational_is_nan(r));
}

void test_rational_from_double_nan(void)
{
    dsdl_rational_t r = dsdl_rational_from_double((double)NAN);
    TEST_ASSERT_TRUE(dsdl_rational_is_nan(r));
}

void test_rational_from_double_large_positive(void)
{
    double          x = 1.0e50;
    dsdl_rational_t r = dsdl_rational_from_double(x);

    TEST_ASSERT_FALSE(dsdl_rational_is_nan(r));
    TEST_ASSERT_FALSE(r.num.negative);
}

void test_rational_from_double_large_negative(void)
{
    double          x = -1.0e50;
    dsdl_rational_t r = dsdl_rational_from_double(x);

    TEST_ASSERT_FALSE(dsdl_rational_is_nan(r));
    TEST_ASSERT_TRUE(r.num.negative);
}

// ============================================================================
// Extreme value and error path tests
// ============================================================================

void test_rational_div_by_zero(void)
{
    // Division by zero should return NaN
    dsdl_rational_t a = make_rational(5, 7);
    dsdl_rational_t b = dsdl_rational_from_int(0);
    dsdl_rational_t r = dsdl_rational_div(a, b);

    TEST_ASSERT_TRUE(dsdl_rational_is_nan(r));
    assert_bigint_eq_uintmax(0, r.den);
}

void test_rational_div_zero_by_nonzero(void)
{
    // 0 / 5 = 0 (valid, not NaN)
    dsdl_rational_t a = dsdl_rational_from_int(0);
    dsdl_rational_t b = make_rational(5, 1);
    dsdl_rational_t r = dsdl_rational_div(a, b);

    TEST_ASSERT_FALSE(dsdl_rational_is_nan(r));
    assert_bigint_eq_intmax(0, r.num);
    assert_bigint_eq_uintmax(1, r.den);
}

void test_rational_div_nan_operand(void)
{
    // NaN / x = NaN
    dsdl_rational_t a = dsdl_rational_nan();
    dsdl_rational_t b = make_rational(3, 4);
    dsdl_rational_t r = dsdl_rational_div(a, b);

    TEST_ASSERT_TRUE(dsdl_rational_is_nan(r));
}

void test_rational_add_nan_operand(void)
{
    // NaN + x = NaN
    dsdl_rational_t a = dsdl_rational_nan();
    dsdl_rational_t b = make_rational(1, 2);
    dsdl_rational_t r = dsdl_rational_add(a, b);

    TEST_ASSERT_TRUE(dsdl_rational_is_nan(r));
}

void test_rational_mul_nan_operand(void)
{
    // NaN * x = NaN
    dsdl_rational_t a = dsdl_rational_nan();
    dsdl_rational_t b = make_rational(2, 3);
    dsdl_rational_t r = dsdl_rational_mul(a, b);

    TEST_ASSERT_TRUE(dsdl_rational_is_nan(r));
}

void test_rational_sub_nan_operand(void)
{
    // x - NaN = NaN
    dsdl_rational_t a = make_rational(5, 7);
    dsdl_rational_t b = dsdl_rational_nan();
    dsdl_rational_t r = dsdl_rational_sub(a, b);

    TEST_ASSERT_TRUE(dsdl_rational_is_nan(r));
}

void test_rational_cmp_nan_operand(void)
{
    // cmp(NaN, x) = 0
    dsdl_rational_t a   = dsdl_rational_nan();
    dsdl_rational_t b   = make_rational(1, 2);
    int             cmp = dsdl_rational_cmp(a, b);

    TEST_ASSERT_EQUAL_INT(0, cmp);
}

void test_rational_normalize_nan(void)
{
    // normalize(NaN) = NaN
    dsdl_rational_t r = dsdl_rational_nan();
    r                 = dsdl_rational_normalize(r);

    TEST_ASSERT_TRUE(dsdl_rational_is_nan(r));
}

void test_rational_to_double_nan(void)
{
    // to_double(NaN) should fail
    dsdl_rational_t r   = dsdl_rational_nan();
    double          out = 0.0;

    TEST_ASSERT_FALSE(dsdl_rational_to_double(r, &out));
}

void test_rational_to_intmax_non_integer(void)
{
    // to_intmax(1/2) should fail (not an integer)
    dsdl_rational_t r   = make_rational(1, 2);
    intmax_t        out = 0;

    TEST_ASSERT_FALSE(dsdl_rational_to_intmax(r, &out));
}

void test_rational_to_uintmax_negative(void)
{
    // to_uintmax(-5/1) should fail (negative)
    dsdl_rational_t r   = make_rational(-5, 1);
    uintmax_t       out = 0;

    TEST_ASSERT_FALSE(dsdl_rational_to_uintmax(r, &out));
}

void test_rational_to_uintmax_non_integer(void)
{
    // to_uintmax(3/4) should fail (not an integer)
    dsdl_rational_t r   = make_rational(3, 4);
    uintmax_t       out = 0;

    TEST_ASSERT_FALSE(dsdl_rational_to_uintmax(r, &out));
}

void test_rational_is_int_true(void)
{
    // is_int(5/1) = true
    dsdl_rational_t r = make_rational(5, 1);
    TEST_ASSERT_TRUE(dsdl_rational_is_int(r));
}

void test_rational_is_int_false(void)
{
    // is_int(5/2) = false
    dsdl_rational_t r = make_rational(5, 2);
    TEST_ASSERT_FALSE(dsdl_rational_is_int(r));
}

void test_rational_is_int_nan(void)
{
    // is_int(NaN) = false
    dsdl_rational_t r = dsdl_rational_nan();
    TEST_ASSERT_FALSE(dsdl_rational_is_int(r));
}

void test_rational_neg_zero(void)
{
    // neg(0) = 0 (sign not flipped for zero)
    dsdl_rational_t r = dsdl_rational_from_int(0);
    r                 = dsdl_rational_neg(r);

    assert_bigint_eq_intmax(0, r.num);
    assert_bigint_eq_uintmax(1, r.den);
}

void test_rational_neg_positive(void)
{
    // neg(3/4) = -3/4
    dsdl_rational_t r = make_rational(3, 4);
    r                 = dsdl_rational_neg(r);

    TEST_ASSERT_TRUE(r.num.negative);
    assert_bigint_eq_intmax(-3, r.num);
    assert_bigint_eq_uintmax(4, r.den);
}

void test_rational_neg_negative(void)
{
    // neg(-2/5) = 2/5
    dsdl_rational_t r = make_rational(-2, 5);
    r                 = dsdl_rational_neg(r);

    TEST_ASSERT_FALSE(r.num.negative);
    assert_bigint_eq_intmax(2, r.num);
    assert_bigint_eq_uintmax(5, r.den);
}

void test_rational_cmp_both_nan(void)
{
    // cmp(NaN, NaN) = 0
    dsdl_rational_t a   = dsdl_rational_nan();
    dsdl_rational_t b   = dsdl_rational_nan();
    int             cmp = dsdl_rational_cmp(a, b);

    TEST_ASSERT_EQUAL_INT(0, cmp);
}

void test_rational_cmp_zero_positive(void)
{
    // cmp(0, 5) < 0
    dsdl_rational_t a = dsdl_rational_from_int(0);
    dsdl_rational_t b = dsdl_rational_from_int(5);

    TEST_ASSERT_TRUE(dsdl_rational_cmp(a, b) < 0);
}

void test_rational_cmp_zero_negative(void)
{
    // cmp(0, -5) > 0
    dsdl_rational_t a = dsdl_rational_from_int(0);
    dsdl_rational_t b = dsdl_rational_from_int(-5);

    TEST_ASSERT_TRUE(dsdl_rational_cmp(a, b) > 0);
}

void test_rational_cmp_zero_zero(void)
{
    // cmp(0, 0) = 0
    dsdl_rational_t a = dsdl_rational_from_int(0);
    dsdl_rational_t b = dsdl_rational_from_int(0);

    TEST_ASSERT_EQUAL_INT(0, dsdl_rational_cmp(a, b));
}

void test_rational_from_uintmax_zero(void)
{
    // from_uintmax(0) = 0/1
    dsdl_rational_t r = dsdl_rational_from_uintmax(0U);

    assert_bigint_eq_intmax(0, r.num);
    assert_bigint_eq_uintmax(1, r.den);
    TEST_ASSERT_FALSE(r.num.negative);
}

void test_rational_from_uintmax_large(void)
{
    // from_uintmax(UINTMAX_MAX) = UINTMAX_MAX/1
    dsdl_rational_t r = dsdl_rational_from_uintmax(UINTMAX_MAX);

    assert_bigint_eq_uintmax(UINTMAX_MAX, r.num);
    assert_bigint_eq_uintmax(1, r.den);
    TEST_ASSERT_FALSE(r.num.negative);
}

void test_rational_normalize_zero_numerator(void)
{
    // normalize(0/5) = 0/1
    dsdl_rational_t r = make_rational(0, 5);
    r                 = dsdl_rational_normalize(r);

    assert_bigint_eq_intmax(0, r.num);
    assert_bigint_eq_uintmax(1, r.den);
    TEST_ASSERT_FALSE(r.num.negative);
}

void test_rational_add_opposite_signs(void)
{
    // 5/7 + (-5/7) = 0
    dsdl_rational_t a = make_rational(5, 7);
    dsdl_rational_t b = make_rational(-5, 7);
    dsdl_rational_t r = dsdl_rational_add(a, b);

    assert_bigint_eq_intmax(0, r.num);
    assert_bigint_eq_uintmax(1, r.den);
}

void test_rational_sub_same_values(void)
{
    // 3/4 - 3/4 = 0
    dsdl_rational_t a = make_rational(3, 4);
    dsdl_rational_t b = make_rational(3, 4);
    dsdl_rational_t r = dsdl_rational_sub(a, b);

    assert_bigint_eq_intmax(0, r.num);
    assert_bigint_eq_uintmax(1, r.den);
}

void test_rational_mul_by_one(void)
{
    // 5/7 * 1 = 5/7
    dsdl_rational_t a = make_rational(5, 7);
    dsdl_rational_t b = dsdl_rational_from_int(1);
    dsdl_rational_t r = dsdl_rational_mul(a, b);

    assert_bigint_eq_intmax(5, r.num);
    assert_bigint_eq_uintmax(7, r.den);
}

void test_rational_mul_by_negative_one(void)
{
    // 5/7 * (-1) = -5/7
    dsdl_rational_t a = make_rational(5, 7);
    dsdl_rational_t b = dsdl_rational_from_int(-1);
    dsdl_rational_t r = dsdl_rational_mul(a, b);

    TEST_ASSERT_TRUE(r.num.negative);
    assert_bigint_eq_intmax(-5, r.num);
    assert_bigint_eq_uintmax(7, r.den);
}

void test_rational_div_by_one(void)
{
    // 5/7 / 1 = 5/7
    dsdl_rational_t a = make_rational(5, 7);
    dsdl_rational_t b = dsdl_rational_from_int(1);
    dsdl_rational_t r = dsdl_rational_div(a, b);

    assert_bigint_eq_intmax(5, r.num);
    assert_bigint_eq_uintmax(7, r.den);
}

void test_rational_div_by_negative_one(void)
{
    // 5/7 / (-1) = -5/7
    dsdl_rational_t a = make_rational(5, 7);
    dsdl_rational_t b = dsdl_rational_from_int(-1);
    dsdl_rational_t r = dsdl_rational_div(a, b);

    TEST_ASSERT_TRUE(r.num.negative);
    assert_bigint_eq_intmax(-5, r.num);
    assert_bigint_eq_uintmax(7, r.den);
}

void test_rational_div_self(void)
{
    // 5/7 / (5/7) = 1
    dsdl_rational_t a = make_rational(5, 7);
    dsdl_rational_t b = make_rational(5, 7);
    dsdl_rational_t r = dsdl_rational_div(a, b);

    assert_bigint_eq_intmax(1, r.num);
    assert_bigint_eq_uintmax(1, r.den);
}

// ============================================================================
// Main
// ============================================================================

void setUp(void) {}
void tearDown(void) {}

int main(void)
{
    UNITY_BEGIN();

    RUN_TEST(test_rational_from_int);
    RUN_TEST(test_rational_normalize);
    RUN_TEST(test_rational_add);
    RUN_TEST(test_rational_sub);
    RUN_TEST(test_rational_mul);
    RUN_TEST(test_rational_div);
    RUN_TEST(test_rational_cmp);

    RUN_TEST(test_rational_overflow_mul);
    RUN_TEST(test_rational_overflow_add);
    RUN_TEST(test_rational_overflow_large_denominators);
    RUN_TEST(test_rational_overflow_preserves_sign);
    RUN_TEST(test_rational_halve_approximation);
    RUN_TEST(test_rational_intmax_min_negate);
    RUN_TEST(test_rational_div_large_denominator);
    RUN_TEST(test_rational_add_large_denominator);
    RUN_TEST(test_rational_cmp_large_denominator);

    RUN_TEST(test_rational_cmp_overflow_fallback_positive);
    RUN_TEST(test_rational_cmp_overflow_fallback_negative);
    RUN_TEST(test_rational_cmp_overflow_fallback_equal);

    RUN_TEST(test_rational_is_nan_zero_denominator);
    RUN_TEST(test_rational_is_nan_valid_rational);
    RUN_TEST(test_rational_is_nan_zero_numerator);
    RUN_TEST(test_rational_is_nan_large_denominator);

    RUN_TEST(test_rational_from_double_zero);
    RUN_TEST(test_rational_from_double_negative_zero);
    RUN_TEST(test_rational_from_double_one);
    RUN_TEST(test_rational_from_double_negative_one);
    RUN_TEST(test_rational_from_double_half);
    RUN_TEST(test_rational_from_double_very_small);
    RUN_TEST(test_rational_from_double_infinity);
    RUN_TEST(test_rational_from_double_negative_infinity);
    RUN_TEST(test_rational_from_double_nan);
    RUN_TEST(test_rational_from_double_large_positive);
    RUN_TEST(test_rational_from_double_large_negative);

    RUN_TEST(test_rational_div_by_zero);
    RUN_TEST(test_rational_div_zero_by_nonzero);
    RUN_TEST(test_rational_div_nan_operand);
    RUN_TEST(test_rational_add_nan_operand);
    RUN_TEST(test_rational_mul_nan_operand);
    RUN_TEST(test_rational_sub_nan_operand);
    RUN_TEST(test_rational_cmp_nan_operand);
    RUN_TEST(test_rational_normalize_nan);
    RUN_TEST(test_rational_to_double_nan);
    RUN_TEST(test_rational_to_intmax_non_integer);
    RUN_TEST(test_rational_to_uintmax_negative);
    RUN_TEST(test_rational_to_uintmax_non_integer);
    RUN_TEST(test_rational_is_int_true);
    RUN_TEST(test_rational_is_int_false);
    RUN_TEST(test_rational_is_int_nan);
    RUN_TEST(test_rational_neg_zero);
    RUN_TEST(test_rational_neg_positive);
    RUN_TEST(test_rational_neg_negative);
    RUN_TEST(test_rational_cmp_both_nan);
    RUN_TEST(test_rational_cmp_zero_positive);
    RUN_TEST(test_rational_cmp_zero_negative);
    RUN_TEST(test_rational_cmp_zero_zero);
    RUN_TEST(test_rational_from_uintmax_zero);
    RUN_TEST(test_rational_from_uintmax_large);
    RUN_TEST(test_rational_normalize_zero_numerator);
    RUN_TEST(test_rational_add_opposite_signs);
    RUN_TEST(test_rational_sub_same_values);
    RUN_TEST(test_rational_mul_by_one);
    RUN_TEST(test_rational_mul_by_negative_one);
    RUN_TEST(test_rational_div_by_one);
    RUN_TEST(test_rational_div_by_negative_one);
    RUN_TEST(test_rational_div_self);

    return UNITY_END();
}
