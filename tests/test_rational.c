/// Tests for rational arithmetic implementation
///
/// These tests access internal functions by including dsdl.c directly.

#include "unity.h"
#include "dsdl.h"

// Include implementation to access internal functions
// This pattern is used in libcanard/libudpard test suites
#include "dsdl.c"

// ============================================================================
// Rational arithmetic tests
// ============================================================================

void test_rational_from_int(void)
{
    dsdl_rational_t r = dsdl_rational_from_int(42);
    TEST_ASSERT_EQUAL_INT64(42, r.num);
    TEST_ASSERT_EQUAL_UINT64(1, r.den);

    r = dsdl_rational_from_int(-123);
    TEST_ASSERT_EQUAL_INT64(-123, r.num);
    TEST_ASSERT_EQUAL_UINT64(1, r.den);

    r = dsdl_rational_from_int(0);
    TEST_ASSERT_EQUAL_INT64(0, r.num);
    TEST_ASSERT_EQUAL_UINT64(1, r.den);
}

void test_rational_normalize(void)
{
    // Already normalized
    dsdl_rational_t r = { 6, 1 };
    r                 = dsdl_rational_normalize(r);
    TEST_ASSERT_EQUAL_INT64(6, r.num);
    TEST_ASSERT_EQUAL_UINT64(1, r.den);

    // Reduce 6/2 = 3/1
    r.num = 6;
    r.den = 2;
    r     = dsdl_rational_normalize(r);
    TEST_ASSERT_EQUAL_INT64(3, r.num);
    TEST_ASSERT_EQUAL_UINT64(1, r.den);

    // Reduce 12/8 = 3/2
    r.num = 12;
    r.den = 8;
    r     = dsdl_rational_normalize(r);
    TEST_ASSERT_EQUAL_INT64(3, r.num);
    TEST_ASSERT_EQUAL_UINT64(2, r.den);

    // Negative numerator: -6/4 = -3/2
    r.num = -6;
    r.den = 4;
    r     = dsdl_rational_normalize(r);
    TEST_ASSERT_EQUAL_INT64(-3, r.num);
    TEST_ASSERT_EQUAL_UINT64(2, r.den);

    // Zero numerator: 0/5 = 0/1
    r.num = 0;
    r.den = 5;
    r     = dsdl_rational_normalize(r);
    TEST_ASSERT_EQUAL_INT64(0, r.num);
    TEST_ASSERT_EQUAL_UINT64(1, r.den);

    // NaN (denominator 0) stays NaN
    r.num = 5;
    r.den = 0;
    r     = dsdl_rational_normalize(r);
    TEST_ASSERT_EQUAL_UINT64(0, r.den);
}

void test_rational_add(void)
{
    // 1/2 + 1/3 = 5/6
    dsdl_rational_t a = { 1, 2 };
    dsdl_rational_t b = { 1, 3 };
    dsdl_rational_t r = dsdl_rational_add(a, b);
    TEST_ASSERT_EQUAL_INT64(5, r.num);
    TEST_ASSERT_EQUAL_UINT64(6, r.den);

    // 1/4 + 1/4 = 1/2
    a = (dsdl_rational_t){ 1, 4 };
    b = (dsdl_rational_t){ 1, 4 };
    r = dsdl_rational_add(a, b);
    TEST_ASSERT_EQUAL_INT64(1, r.num);
    TEST_ASSERT_EQUAL_UINT64(2, r.den);

    // 3 + 4 = 7 (integers)
    a = dsdl_rational_from_int(3);
    b = dsdl_rational_from_int(4);
    r = dsdl_rational_add(a, b);
    TEST_ASSERT_EQUAL_INT64(7, r.num);
    TEST_ASSERT_EQUAL_UINT64(1, r.den);

    // -1/2 + 1/2 = 0
    a = (dsdl_rational_t){ -1, 2 };
    b = (dsdl_rational_t){ 1, 2 };
    r = dsdl_rational_add(a, b);
    TEST_ASSERT_EQUAL_INT64(0, r.num);
}

void test_rational_sub(void)
{
    // 1/2 - 1/3 = 1/6
    dsdl_rational_t a = { 1, 2 };
    dsdl_rational_t b = { 1, 3 };
    dsdl_rational_t r = dsdl_rational_sub(a, b);
    TEST_ASSERT_EQUAL_INT64(1, r.num);
    TEST_ASSERT_EQUAL_UINT64(6, r.den);

    // 5 - 3 = 2 (integers)
    a = dsdl_rational_from_int(5);
    b = dsdl_rational_from_int(3);
    r = dsdl_rational_sub(a, b);
    TEST_ASSERT_EQUAL_INT64(2, r.num);
    TEST_ASSERT_EQUAL_UINT64(1, r.den);

    // 1/4 - 1/4 = 0
    a = (dsdl_rational_t){ 1, 4 };
    b = (dsdl_rational_t){ 1, 4 };
    r = dsdl_rational_sub(a, b);
    TEST_ASSERT_EQUAL_INT64(0, r.num);
}

void test_rational_mul(void)
{
    // 2/3 * 3/4 = 1/2
    dsdl_rational_t a = { 2, 3 };
    dsdl_rational_t b = { 3, 4 };
    dsdl_rational_t r = dsdl_rational_mul(a, b);
    TEST_ASSERT_EQUAL_INT64(1, r.num);
    TEST_ASSERT_EQUAL_UINT64(2, r.den);

    // 3 * 4 = 12 (integers)
    a = dsdl_rational_from_int(3);
    b = dsdl_rational_from_int(4);
    r = dsdl_rational_mul(a, b);
    TEST_ASSERT_EQUAL_INT64(12, r.num);
    TEST_ASSERT_EQUAL_UINT64(1, r.den);

    // -2/3 * 3/5 = -2/5
    a = (dsdl_rational_t){ -2, 3 };
    b = (dsdl_rational_t){ 3, 5 };
    r = dsdl_rational_mul(a, b);
    TEST_ASSERT_EQUAL_INT64(-2, r.num);
    TEST_ASSERT_EQUAL_UINT64(5, r.den);

    // Multiply by zero
    a = (dsdl_rational_t){ 5, 7 };
    b = dsdl_rational_from_int(0);
    r = dsdl_rational_mul(a, b);
    TEST_ASSERT_EQUAL_INT64(0, r.num);
}

void test_rational_div(void)
{
    // (2/3) / (4/5) = 10/12 = 5/6
    dsdl_rational_t a = { 2, 3 };
    dsdl_rational_t b = { 4, 5 };
    dsdl_rational_t r = dsdl_rational_div(a, b);
    TEST_ASSERT_EQUAL_INT64(5, r.num);
    TEST_ASSERT_EQUAL_UINT64(6, r.den);

    // 6 / 2 = 3 (integers)
    a = dsdl_rational_from_int(6);
    b = dsdl_rational_from_int(2);
    r = dsdl_rational_div(a, b);
    TEST_ASSERT_EQUAL_INT64(3, r.num);
    TEST_ASSERT_EQUAL_UINT64(1, r.den);

    // Division by negative: 1/2 / (-1/3) = -3/2
    a = (dsdl_rational_t){ 1, 2 };
    b = (dsdl_rational_t){ -1, 3 };
    r = dsdl_rational_div(a, b);
    TEST_ASSERT_EQUAL_INT64(-3, r.num);
    TEST_ASSERT_EQUAL_UINT64(2, r.den);

    // Division by zero -> NaN
    a = (dsdl_rational_t){ 5, 7 };
    b = dsdl_rational_from_int(0);
    r = dsdl_rational_div(a, b);
    TEST_ASSERT_EQUAL_UINT64(0, r.den); // NaN indicator
}

void test_rational_cmp(void)
{
    dsdl_rational_t a;
    dsdl_rational_t b;

    // 1/2 < 2/3
    a = (dsdl_rational_t){ 1, 2 };
    b = (dsdl_rational_t){ 2, 3 };
    TEST_ASSERT_TRUE(dsdl_rational_cmp(a, b) < 0);

    // 2/3 > 1/2
    TEST_ASSERT_TRUE(dsdl_rational_cmp(b, a) > 0);

    // 1/2 == 2/4
    a = (dsdl_rational_t){ 1, 2 };
    b = (dsdl_rational_t){ 2, 4 };
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
    // Multiplying large values should not crash and should approximate
    // INTMAX_MAX * 2 would overflow, but halving should keep it reasonable
    dsdl_rational_t a = dsdl_rational_from_int(INTMAX_MAX / 2);
    dsdl_rational_t b = dsdl_rational_from_int(4);
    dsdl_rational_t r = dsdl_rational_mul(a, b);

    // Result should be approximately 2 * INTMAX_MAX / 2 = INTMAX_MAX
    // but halved down to fit - should be positive and large
    TEST_ASSERT_TRUE(r.num > 0);
    TEST_ASSERT_TRUE(r.den >= 1);

    // The ratio should be approximately (INTMAX_MAX/2) * 4 = 2*INTMAX_MAX
    // After halving approximation, it should still be a large positive number
}

void test_rational_overflow_add(void)
{
    // Adding values near INTMAX_MAX should not crash
    dsdl_rational_t a = dsdl_rational_from_int(INTMAX_MAX / 2);
    dsdl_rational_t b = dsdl_rational_from_int(INTMAX_MAX / 2);
    dsdl_rational_t r = dsdl_rational_add(a, b);

    // Result should be approximately INTMAX_MAX (or halved approximation)
    TEST_ASSERT_TRUE(r.num > 0);
    TEST_ASSERT_TRUE(r.den >= 1);
}

void test_rational_overflow_large_denominators(void)
{
    // Large denominators that would overflow when multiplied
    dsdl_rational_t a = { 1, UINTMAX_MAX / 2 };
    dsdl_rational_t b = { 1, UINTMAX_MAX / 2 };
    dsdl_rational_t r = dsdl_rational_mul(a, b);

    // Should produce a very small positive fraction (close to 0)
    // or an approximation thereof
    TEST_ASSERT_TRUE(r.den > 0);
    // Since 1/(large^2) is tiny, the result should be very small
    // After halving, we get an approximation but it shouldn't crash
}

void test_rational_overflow_preserves_sign(void)
{
    // Negative overflow should preserve sign
    dsdl_rational_t a = dsdl_rational_from_int(INTMAX_MIN / 2);
    dsdl_rational_t b = dsdl_rational_from_int(3);
    dsdl_rational_t r = dsdl_rational_mul(a, b);

    // Result should be negative
    TEST_ASSERT_TRUE(r.num < 0);
    TEST_ASSERT_TRUE(r.den >= 1);
}

void test_rational_halve_approximation(void)
{
    // Test that halving produces reasonable approximations
    // 8/16 halved should give 4/8 = 1/2 after normalization
    dsdl_rational_t r = { 8, 16 };
    r                 = dsdl_rational_halve(r);
    r                 = dsdl_rational_normalize(r);
    TEST_ASSERT_EQUAL_INT64(1, r.num);
    TEST_ASSERT_EQUAL_UINT64(2, r.den);

    // Large value halving: preserves approximate ratio
    r = (dsdl_rational_t){ 1000000, 8000000 }; // 1/8
    r = dsdl_rational_halve(r);
    r = dsdl_rational_normalize(r);
    // 500000/4000000 = 1/8
    TEST_ASSERT_EQUAL_INT64(1, r.num);
    TEST_ASSERT_EQUAL_UINT64(8, r.den);
}

void test_rational_intmax_min_negate(void)
{
    // Negating INTMAX_MIN is tricky - test we handle it
    dsdl_rational_t a = { INTMAX_MIN, 1 };
    dsdl_rational_t r = dsdl_rational_neg(a);

    // Should be positive (after halving to make it representable)
    TEST_ASSERT_TRUE(r.num > 0);
    TEST_ASSERT_TRUE(r.den >= 1);
}

void test_rational_div_large_denominator(void)
{
    // Division where denominator > INTMAX_MAX
    // 1 / (1/UINTMAX_MAX) should give approximately UINTMAX_MAX
    // but we need to halve to fit in intmax_t
    dsdl_rational_t a = { 1, 1 };
    dsdl_rational_t b = { 1, UINTMAX_MAX };
    dsdl_rational_t r = dsdl_rational_div(a, b);

    // Result should be positive and large (halved approximation of UINTMAX_MAX)
    TEST_ASSERT_TRUE(r.num > 0);
    TEST_ASSERT_TRUE(r.den >= 1);

    // Should be approximately UINTMAX_MAX or a halved version
    // The ratio r.num/r.den should be large
    TEST_ASSERT_TRUE(r.num > 1000); // Should be much larger than 1
}

void test_rational_add_large_denominator(void)
{
    // Addition where denominator > INTMAX_MAX
    dsdl_rational_t a = { 1, UINTMAX_MAX };
    dsdl_rational_t b = { 1, UINTMAX_MAX };
    dsdl_rational_t r = dsdl_rational_add(a, b);

    // Should not crash and produce a valid result
    TEST_ASSERT_TRUE(r.den > 0);
    // Result should be approximately 2/UINTMAX_MAX (very small but positive)
    TEST_ASSERT_TRUE(r.num >= 0);
}

void test_rational_cmp_large_denominator(void)
{
    // Comparison where denominator > INTMAX_MAX
    dsdl_rational_t a = { 1, UINTMAX_MAX };
    dsdl_rational_t b = { 2, UINTMAX_MAX };

    // a < b (1/UINTMAX_MAX < 2/UINTMAX_MAX)
    TEST_ASSERT_TRUE(dsdl_rational_cmp(a, b) < 0);
    TEST_ASSERT_TRUE(dsdl_rational_cmp(b, a) > 0);

    // Equal comparison
    dsdl_rational_t c = { 1, UINTMAX_MAX };
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
