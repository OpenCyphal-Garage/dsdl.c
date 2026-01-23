/// Internal tests for dsdl.c
///
/// This file includes dsdl.c directly to access internal functions.
/// Used for testing implementation details that aren't exposed in the public API.

#include "unity.h"

#include <stdlib.h>

// Include the implementation directly for internal access
#include "dsdl.c"

// ============================================================================
// Setup/Teardown
// ============================================================================

void setUp(void)
{
    // Called before each test
}

void tearDown(void)
{
    // Called after each test
}

// ============================================================================
// GCD tests
// ============================================================================

static void test_gcd_basic(void)
{
    TEST_ASSERT_EQUAL_UINT64(1, dsdl_gcd_(1, 1));
    TEST_ASSERT_EQUAL_UINT64(1, dsdl_gcd_(3, 5));
    TEST_ASSERT_EQUAL_UINT64(6, dsdl_gcd_(12, 18));
    TEST_ASSERT_EQUAL_UINT64(4, dsdl_gcd_(12, 8));
    TEST_ASSERT_EQUAL_UINT64(5, dsdl_gcd_(0, 5));
    TEST_ASSERT_EQUAL_UINT64(7, dsdl_gcd_(7, 0));
}

static void test_gcd_large(void)
{
    // Test with larger numbers
    TEST_ASSERT_EQUAL_UINT64(1, dsdl_gcd_(1000000007ULL, 1000000009ULL));   // Two primes
    TEST_ASSERT_EQUAL_UINT64(1000000000ULL, dsdl_gcd_(1000000000ULL, 2000000000ULL));
}

// ============================================================================
// Absolute value tests
// ============================================================================

static void test_abs_basic(void)
{
    TEST_ASSERT_EQUAL_UINT64(0, dsdl_abs_(0));
    TEST_ASSERT_EQUAL_UINT64(42, dsdl_abs_(42));
    TEST_ASSERT_EQUAL_UINT64(42, dsdl_abs_(-42));
    TEST_ASSERT_EQUAL_UINT64(1, dsdl_abs_(-1));
}

static void test_abs_intmax_min(void)
{
    // Special case: INTMAX_MIN
    const uintmax_t expected = (uintmax_t) INTMAX_MAX + 1U;
    TEST_ASSERT_EQUAL_UINT64(expected, dsdl_abs_(INTMAX_MIN));
}

// ============================================================================
// Rational is_int tests
// ============================================================================

static void test_rational_is_int(void)
{
    dsdl_rational_t r;

    r = dsdl_rational_from_int_(42);
    TEST_ASSERT_TRUE(dsdl_rational_is_int_(r));

    r = (dsdl_rational_t){1, 2};
    TEST_ASSERT_FALSE(dsdl_rational_is_int_(r));

    r = (dsdl_rational_t){4, 2};
    r = dsdl_rational_normalize_(r);
    TEST_ASSERT_TRUE(dsdl_rational_is_int_(r));
}

// ============================================================================
// Rational to_int tests
// ============================================================================

static void test_rational_to_int(void)
{
    dsdl_rational_t r;

    r = dsdl_rational_from_int_(123);
    TEST_ASSERT_EQUAL_INT64(123, dsdl_rational_to_int_(r));

    r = dsdl_rational_from_int_(-456);
    TEST_ASSERT_EQUAL_INT64(-456, dsdl_rational_to_int_(r));
}

// ============================================================================
// Rational negation tests
// ============================================================================

static void test_rational_neg(void)
{
    dsdl_rational_t r;

    r = dsdl_rational_from_int_(5);
    r = dsdl_rational_neg_(r);
    TEST_ASSERT_EQUAL_INT64(-5, r.num);

    r = (dsdl_rational_t){3, 7};
    r = dsdl_rational_neg_(r);
    TEST_ASSERT_EQUAL_INT64(-3, r.num);
    TEST_ASSERT_EQUAL_UINT64(7, r.den);
}

// ============================================================================
// Memory helpers tests
// ============================================================================

static void* test_realloc(dsdl_t* self, void* ptr, size_t size)
{
    (void) self;
    if (size == 0)
    {
        free(ptr);
        return NULL;
    }
    return realloc(ptr, size);
}

static void test_alloc_free(void)
{
    dsdl_t dsdl;
    dsdl.realloc = test_realloc;

    void* ptr = dsdl_alloc_(&dsdl, 100);
    TEST_ASSERT_NOT_NULL(ptr);

    dsdl_free_(&dsdl, ptr);

    // Allocating 0 should return NULL
    ptr = dsdl_alloc_(&dsdl, 0);
    TEST_ASSERT_NULL(ptr);

    // Freeing NULL should be safe
    dsdl_free_(&dsdl, NULL);
}

// ============================================================================
// Main
// ============================================================================

int main(void)
{
    UNITY_BEGIN();

    // GCD tests
    RUN_TEST(test_gcd_basic);
    RUN_TEST(test_gcd_large);

    // Absolute value tests
    RUN_TEST(test_abs_basic);
    RUN_TEST(test_abs_intmax_min);

    // Rational tests
    RUN_TEST(test_rational_is_int);
    RUN_TEST(test_rational_to_int);
    RUN_TEST(test_rational_neg);

    // Memory helper tests
    RUN_TEST(test_alloc_free);

    return UNITY_END();
}
