/// Internal tests for dsdl.c
///
/// This file includes dsdl.c directly to access internal functions.
/// Used for testing implementation details that aren't exposed in the public API.

// Include the implementation directly for internal access
#include "dsdl.c"

#include "unity.h"

#include <stdlib.h>

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
    TEST_ASSERT_EQUAL_UINT64(1, dsdl_gcd(1, 1));
    TEST_ASSERT_EQUAL_UINT64(1, dsdl_gcd(3, 5));
    TEST_ASSERT_EQUAL_UINT64(6, dsdl_gcd(12, 18));
    TEST_ASSERT_EQUAL_UINT64(4, dsdl_gcd(12, 8));
    TEST_ASSERT_EQUAL_UINT64(5, dsdl_gcd(0, 5));
    TEST_ASSERT_EQUAL_UINT64(7, dsdl_gcd(7, 0));
}

static void test_gcd_large(void)
{
    // Test with larger numbers
    TEST_ASSERT_EQUAL_UINT64(1, dsdl_gcd(1000000007ULL, 1000000009ULL)); // Two primes
    TEST_ASSERT_EQUAL_UINT64(1000000000ULL, dsdl_gcd(1000000000ULL, 2000000000ULL));
}

// ============================================================================
// Absolute value tests
// ============================================================================

static void test_abs_basic(void)
{
    TEST_ASSERT_EQUAL_UINT64(0, dsdl_abs(0));
    TEST_ASSERT_EQUAL_UINT64(42, dsdl_abs(42));
    TEST_ASSERT_EQUAL_UINT64(42, dsdl_abs(-42));
    TEST_ASSERT_EQUAL_UINT64(1, dsdl_abs(-1));
}

static void test_abs_intmax_min(void)
{
    // Special case: INTMAX_MIN
    const uintmax_t expected = (uintmax_t)INTMAX_MAX + 1U;
    TEST_ASSERT_EQUAL_UINT64(expected, dsdl_abs(INTMAX_MIN));
}

// ============================================================================
// Rational is_int tests
// ============================================================================

static void test_rational_is_int(void)
{
    dsdl_rational_t r;

    r = dsdl_rational_from_int(42);
    TEST_ASSERT_TRUE(dsdl_rational_is_int(r));

    r = (dsdl_rational_t){ 1, 2 };
    TEST_ASSERT_FALSE(dsdl_rational_is_int(r));

    r = (dsdl_rational_t){ 4, 2 };
    r = dsdl_rational_normalize(r);
    TEST_ASSERT_TRUE(dsdl_rational_is_int(r));
}

// ============================================================================
// Rational negation tests
// ============================================================================

static void test_rational_neg(void)
{
    dsdl_rational_t r;

    r = dsdl_rational_from_int(5);
    r = dsdl_rational_neg(r);
    TEST_ASSERT_EQUAL_INT64(-5, r.num);

    r = (dsdl_rational_t){ 3, 7 };
    r = dsdl_rational_neg(r);
    TEST_ASSERT_EQUAL_INT64(-3, r.num);
    TEST_ASSERT_EQUAL_UINT64(7, r.den);
}

// ============================================================================
// Memory helpers tests
// ============================================================================

static void* test_realloc(dsdl_t* self, void* ptr, size_t size)
{
    (void)self;
    if (size == 0) {
        free(ptr);
        return NULL;
    }
    return realloc(ptr, size);
}

static void test_alloc_free(void)
{
    dsdl_t dsdl;
    dsdl.realloc = test_realloc;

    void* ptr = dsdl_alloc(&dsdl, 100);
    TEST_ASSERT_NOT_NULL(ptr);

    dsdl_free(&dsdl, ptr);

    // Allocating 0 should return NULL
    ptr = dsdl_alloc(&dsdl, 0);
    TEST_ASSERT_NULL(ptr);

    // Freeing NULL should be safe
    dsdl_free(&dsdl, NULL);
}

// ============================================================================
// Type name parsing tests
// ============================================================================

static void test_parse_type_name_simple(void)
{
    dsdl_type_ref_t ref;

    // Just "TypeName"
    TEST_ASSERT_TRUE(dsdl_parse_typename(wkv_key("TypeName"), &ref));
    TEST_ASSERT_EQUAL_size_t(8, ref.type_name.len);
    TEST_ASSERT_EQUAL_STRING_LEN("TypeName", ref.type_name.str, 8);
    TEST_ASSERT_EQUAL_size_t(0, ref.namespace_part.len);
    TEST_ASSERT_FALSE(ref.has_major);
    TEST_ASSERT_FALSE(ref.has_minor);
}

static void test_parse_type_name_with_namespace(void)
{
    dsdl_type_ref_t ref;

    // "uavcan.node.Heartbeat"
    TEST_ASSERT_TRUE(dsdl_parse_typename(wkv_key("uavcan.node.Heartbeat"), &ref));
    TEST_ASSERT_EQUAL_size_t(9, ref.type_name.len);
    TEST_ASSERT_EQUAL_STRING_LEN("Heartbeat", ref.type_name.str, 9);
    TEST_ASSERT_EQUAL_size_t(11, ref.namespace_part.len);
    TEST_ASSERT_EQUAL_STRING_LEN("uavcan.node", ref.namespace_part.str, 11);
    TEST_ASSERT_EQUAL_size_t(21, ref.full_name.len);
    TEST_ASSERT_FALSE(ref.has_major);
    TEST_ASSERT_FALSE(ref.has_minor);
}

static void test_parse_type_name_with_full_version(void)
{
    dsdl_type_ref_t ref;

    // "uavcan.node.Heartbeat.1.0"
    TEST_ASSERT_TRUE(dsdl_parse_typename(wkv_key("uavcan.node.Heartbeat.1.0"), &ref));
    TEST_ASSERT_EQUAL_size_t(9, ref.type_name.len);
    TEST_ASSERT_EQUAL_STRING_LEN("Heartbeat", ref.type_name.str, 9);
    TEST_ASSERT_EQUAL_size_t(11, ref.namespace_part.len);
    TEST_ASSERT_EQUAL_STRING_LEN("uavcan.node", ref.namespace_part.str, 11);
    TEST_ASSERT_TRUE(ref.has_major);
    TEST_ASSERT_TRUE(ref.has_minor);
    TEST_ASSERT_EQUAL_UINT8(1, ref.major);
    TEST_ASSERT_EQUAL_UINT8(0, ref.minor);
}

static void test_parse_type_name_with_major_only(void)
{
    dsdl_type_ref_t ref;

    // "uavcan.node.Heartbeat.1"
    TEST_ASSERT_TRUE(dsdl_parse_typename(wkv_key("uavcan.node.Heartbeat.1"), &ref));
    TEST_ASSERT_EQUAL_size_t(9, ref.type_name.len);
    TEST_ASSERT_EQUAL_STRING_LEN("Heartbeat", ref.type_name.str, 9);
    TEST_ASSERT_EQUAL_size_t(11, ref.namespace_part.len);
    TEST_ASSERT_EQUAL_STRING_LEN("uavcan.node", ref.namespace_part.str, 11);
    TEST_ASSERT_TRUE(ref.has_major);
    TEST_ASSERT_FALSE(ref.has_minor);
    TEST_ASSERT_EQUAL_UINT8(1, ref.major);
}

static void test_parse_type_name_no_namespace_with_version(void)
{
    dsdl_type_ref_t ref;

    // "TypeName.1.0"
    TEST_ASSERT_TRUE(dsdl_parse_typename(wkv_key("TypeName.1.0"), &ref));
    TEST_ASSERT_EQUAL_size_t(8, ref.type_name.len);
    TEST_ASSERT_EQUAL_STRING_LEN("TypeName", ref.type_name.str, 8);
    TEST_ASSERT_EQUAL_size_t(0, ref.namespace_part.len);
    TEST_ASSERT_TRUE(ref.has_major);
    TEST_ASSERT_TRUE(ref.has_minor);
    TEST_ASSERT_EQUAL_UINT8(1, ref.major);
    TEST_ASSERT_EQUAL_UINT8(0, ref.minor);
}

static void test_parse_type_name_single_namespace(void)
{
    dsdl_type_ref_t ref;

    // "mymsgs.Inner.1.0"
    TEST_ASSERT_TRUE(dsdl_parse_typename(wkv_key("mymsgs.Inner.1.0"), &ref));
    TEST_ASSERT_EQUAL_size_t(5, ref.type_name.len);
    TEST_ASSERT_EQUAL_STRING_LEN("Inner", ref.type_name.str, 5);
    TEST_ASSERT_EQUAL_size_t(6, ref.namespace_part.len);
    TEST_ASSERT_EQUAL_STRING_LEN("mymsgs", ref.namespace_part.str, 6);
    TEST_ASSERT_TRUE(ref.has_major);
    TEST_ASSERT_TRUE(ref.has_minor);
    TEST_ASSERT_EQUAL_UINT8(1, ref.major);
    TEST_ASSERT_EQUAL_UINT8(0, ref.minor);
}

static void test_parse_type_name_large_version(void)
{
    dsdl_type_ref_t ref;

    // "TypeName.255.255"
    TEST_ASSERT_TRUE(dsdl_parse_typename(wkv_key("TypeName.255.255"), &ref));
    TEST_ASSERT_EQUAL_size_t(8, ref.type_name.len);
    TEST_ASSERT_EQUAL_STRING_LEN("TypeName", ref.type_name.str, 8);
    TEST_ASSERT_TRUE(ref.has_major);
    TEST_ASSERT_TRUE(ref.has_minor);
    TEST_ASSERT_EQUAL_UINT8(255, ref.major);
    TEST_ASSERT_EQUAL_UINT8(255, ref.minor);
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
    RUN_TEST(test_rational_neg);

    // Memory helper tests
    RUN_TEST(test_alloc_free);

    // Type name parsing tests
    RUN_TEST(test_parse_type_name_simple);
    RUN_TEST(test_parse_type_name_with_namespace);
    RUN_TEST(test_parse_type_name_with_full_version);
    RUN_TEST(test_parse_type_name_with_major_only);
    RUN_TEST(test_parse_type_name_no_namespace_with_version);
    RUN_TEST(test_parse_type_name_single_namespace);
    RUN_TEST(test_parse_type_name_large_version);

    return UNITY_END();
}
