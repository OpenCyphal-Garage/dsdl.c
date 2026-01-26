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

    // Overflow handling tests
    RUN_TEST(test_rational_overflow_mul);
    RUN_TEST(test_rational_overflow_add);
    RUN_TEST(test_rational_overflow_large_denominators);
    RUN_TEST(test_rational_overflow_preserves_sign);
    RUN_TEST(test_rational_halve_approximation);
    RUN_TEST(test_rational_intmax_min_negate);
    RUN_TEST(test_rational_div_large_denominator);
    RUN_TEST(test_rational_add_large_denominator);
    RUN_TEST(test_rational_cmp_large_denominator);

    return UNITY_END();
}
