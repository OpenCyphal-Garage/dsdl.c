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
    dsdl_rational_t r = dsdl_rational_from_int_(42);
    TEST_ASSERT_EQUAL_INT64(42, r.num);
    TEST_ASSERT_EQUAL_UINT64(1, r.den);

    r = dsdl_rational_from_int_(-123);
    TEST_ASSERT_EQUAL_INT64(-123, r.num);
    TEST_ASSERT_EQUAL_UINT64(1, r.den);

    r = dsdl_rational_from_int_(0);
    TEST_ASSERT_EQUAL_INT64(0, r.num);
    TEST_ASSERT_EQUAL_UINT64(1, r.den);
}

void test_rational_normalize(void)
{
    // Already normalized
    dsdl_rational_t r = {6, 1};
    r                 = dsdl_rational_normalize_(r);
    TEST_ASSERT_EQUAL_INT64(6, r.num);
    TEST_ASSERT_EQUAL_UINT64(1, r.den);

    // Reduce 6/2 = 3/1
    r.num = 6;
    r.den = 2;
    r     = dsdl_rational_normalize_(r);
    TEST_ASSERT_EQUAL_INT64(3, r.num);
    TEST_ASSERT_EQUAL_UINT64(1, r.den);

    // Reduce 12/8 = 3/2
    r.num = 12;
    r.den = 8;
    r     = dsdl_rational_normalize_(r);
    TEST_ASSERT_EQUAL_INT64(3, r.num);
    TEST_ASSERT_EQUAL_UINT64(2, r.den);

    // Negative numerator: -6/4 = -3/2
    r.num = -6;
    r.den = 4;
    r     = dsdl_rational_normalize_(r);
    TEST_ASSERT_EQUAL_INT64(-3, r.num);
    TEST_ASSERT_EQUAL_UINT64(2, r.den);

    // Zero numerator: 0/5 = 0/1
    r.num = 0;
    r.den = 5;
    r     = dsdl_rational_normalize_(r);
    TEST_ASSERT_EQUAL_INT64(0, r.num);
    TEST_ASSERT_EQUAL_UINT64(1, r.den);

    // NaN (denominator 0) stays NaN
    r.num = 5;
    r.den = 0;
    r     = dsdl_rational_normalize_(r);
    TEST_ASSERT_EQUAL_UINT64(0, r.den);
}

void test_rational_add(void)
{
    // 1/2 + 1/3 = 5/6
    dsdl_rational_t a = {1, 2};
    dsdl_rational_t b = {1, 3};
    dsdl_rational_t r = dsdl_rational_add_(a, b);
    TEST_ASSERT_EQUAL_INT64(5, r.num);
    TEST_ASSERT_EQUAL_UINT64(6, r.den);

    // 1/4 + 1/4 = 1/2
    a = (dsdl_rational_t){1, 4};
    b = (dsdl_rational_t){1, 4};
    r = dsdl_rational_add_(a, b);
    TEST_ASSERT_EQUAL_INT64(1, r.num);
    TEST_ASSERT_EQUAL_UINT64(2, r.den);

    // 3 + 4 = 7 (integers)
    a = dsdl_rational_from_int_(3);
    b = dsdl_rational_from_int_(4);
    r = dsdl_rational_add_(a, b);
    TEST_ASSERT_EQUAL_INT64(7, r.num);
    TEST_ASSERT_EQUAL_UINT64(1, r.den);

    // -1/2 + 1/2 = 0
    a = (dsdl_rational_t){-1, 2};
    b = (dsdl_rational_t){1, 2};
    r = dsdl_rational_add_(a, b);
    TEST_ASSERT_EQUAL_INT64(0, r.num);
}

void test_rational_sub(void)
{
    // 1/2 - 1/3 = 1/6
    dsdl_rational_t a = {1, 2};
    dsdl_rational_t b = {1, 3};
    dsdl_rational_t r = dsdl_rational_sub_(a, b);
    TEST_ASSERT_EQUAL_INT64(1, r.num);
    TEST_ASSERT_EQUAL_UINT64(6, r.den);

    // 5 - 3 = 2 (integers)
    a = dsdl_rational_from_int_(5);
    b = dsdl_rational_from_int_(3);
    r = dsdl_rational_sub_(a, b);
    TEST_ASSERT_EQUAL_INT64(2, r.num);
    TEST_ASSERT_EQUAL_UINT64(1, r.den);

    // 1/4 - 1/4 = 0
    a = (dsdl_rational_t){1, 4};
    b = (dsdl_rational_t){1, 4};
    r = dsdl_rational_sub_(a, b);
    TEST_ASSERT_EQUAL_INT64(0, r.num);
}

void test_rational_mul(void)
{
    // 2/3 * 3/4 = 1/2
    dsdl_rational_t a = {2, 3};
    dsdl_rational_t b = {3, 4};
    dsdl_rational_t r = dsdl_rational_mul_(a, b);
    TEST_ASSERT_EQUAL_INT64(1, r.num);
    TEST_ASSERT_EQUAL_UINT64(2, r.den);

    // 3 * 4 = 12 (integers)
    a = dsdl_rational_from_int_(3);
    b = dsdl_rational_from_int_(4);
    r = dsdl_rational_mul_(a, b);
    TEST_ASSERT_EQUAL_INT64(12, r.num);
    TEST_ASSERT_EQUAL_UINT64(1, r.den);

    // -2/3 * 3/5 = -2/5
    a = (dsdl_rational_t){-2, 3};
    b = (dsdl_rational_t){3, 5};
    r = dsdl_rational_mul_(a, b);
    TEST_ASSERT_EQUAL_INT64(-2, r.num);
    TEST_ASSERT_EQUAL_UINT64(5, r.den);

    // Multiply by zero
    a = (dsdl_rational_t){5, 7};
    b = dsdl_rational_from_int_(0);
    r = dsdl_rational_mul_(a, b);
    TEST_ASSERT_EQUAL_INT64(0, r.num);
}

void test_rational_div(void)
{
    // (2/3) / (4/5) = 10/12 = 5/6
    dsdl_rational_t a = {2, 3};
    dsdl_rational_t b = {4, 5};
    dsdl_rational_t r = dsdl_rational_div_(a, b);
    TEST_ASSERT_EQUAL_INT64(5, r.num);
    TEST_ASSERT_EQUAL_UINT64(6, r.den);

    // 6 / 2 = 3 (integers)
    a = dsdl_rational_from_int_(6);
    b = dsdl_rational_from_int_(2);
    r = dsdl_rational_div_(a, b);
    TEST_ASSERT_EQUAL_INT64(3, r.num);
    TEST_ASSERT_EQUAL_UINT64(1, r.den);

    // Division by negative: 1/2 / (-1/3) = -3/2
    a = (dsdl_rational_t){1, 2};
    b = (dsdl_rational_t){-1, 3};
    r = dsdl_rational_div_(a, b);
    TEST_ASSERT_EQUAL_INT64(-3, r.num);
    TEST_ASSERT_EQUAL_UINT64(2, r.den);

    // Division by zero -> NaN
    a = (dsdl_rational_t){5, 7};
    b = dsdl_rational_from_int_(0);
    r = dsdl_rational_div_(a, b);
    TEST_ASSERT_EQUAL_UINT64(0, r.den);   // NaN indicator
}

void test_rational_cmp(void)
{
    dsdl_rational_t a;
    dsdl_rational_t b;

    // 1/2 < 2/3
    a = (dsdl_rational_t){1, 2};
    b = (dsdl_rational_t){2, 3};
    TEST_ASSERT_TRUE(dsdl_rational_cmp_(a, b) < 0);

    // 2/3 > 1/2
    TEST_ASSERT_TRUE(dsdl_rational_cmp_(b, a) > 0);

    // 1/2 == 2/4
    a = (dsdl_rational_t){1, 2};
    b = (dsdl_rational_t){2, 4};
    b = dsdl_rational_normalize_(b);   // Normalize to 1/2
    TEST_ASSERT_EQUAL_INT(0, dsdl_rational_cmp_(a, b));

    // -1 < 1
    a = dsdl_rational_from_int_(-1);
    b = dsdl_rational_from_int_(1);
    TEST_ASSERT_TRUE(dsdl_rational_cmp_(a, b) < 0);

    // 0 == 0
    a = dsdl_rational_from_int_(0);
    b = dsdl_rational_from_int_(0);
    TEST_ASSERT_EQUAL_INT(0, dsdl_rational_cmp_(a, b));
}

// ============================================================================
// Main
// ============================================================================

void setUp(void) { }
void tearDown(void) { }

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

    return UNITY_END();
}
