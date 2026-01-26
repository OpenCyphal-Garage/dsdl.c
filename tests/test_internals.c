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

static void assert_bigint_gcd(const uintmax_t a_val, const uintmax_t b_val, const uintmax_t expected)
{
    dsdl_bigint_t a;
    dsdl_bigint_t b;
    dsdl_bigint_t g;
    TEST_ASSERT_TRUE(dsdl_bigint_from_uintmax(&a, a_val));
    TEST_ASSERT_TRUE(dsdl_bigint_from_uintmax(&b, b_val));
    TEST_ASSERT_TRUE(dsdl_bigint_gcd(a, b, &g));
    assert_bigint_eq_uintmax(expected, g);
}

// ============================================================================
// GCD tests
// ============================================================================

static void test_gcd_basic(void)
{
    assert_bigint_gcd(1U, 1U, 1U);
    assert_bigint_gcd(3U, 5U, 1U);
    assert_bigint_gcd(12U, 18U, 6U);
    assert_bigint_gcd(12U, 8U, 4U);
    assert_bigint_gcd(0U, 5U, 5U);
    assert_bigint_gcd(7U, 0U, 7U);
}

static void test_gcd_large(void)
{
    // Test with larger numbers
    assert_bigint_gcd(1000000007ULL, 1000000009ULL, 1ULL); // Two primes
    assert_bigint_gcd(1000000000ULL, 2000000000ULL, 1000000000ULL);
}

// ============================================================================
// Absolute value tests
// ============================================================================

static void test_abs_basic(void)
{
    dsdl_bigint_t v;

    TEST_ASSERT_TRUE(dsdl_bigint_from_intmax(&v, 0));
    assert_bigint_eq_intmax(0, v);
    TEST_ASSERT_FALSE(v.negative);

    TEST_ASSERT_TRUE(dsdl_bigint_from_intmax(&v, 42));
    assert_bigint_eq_intmax(42, v);
    TEST_ASSERT_FALSE(v.negative);

    TEST_ASSERT_TRUE(dsdl_bigint_from_intmax(&v, -42));
    assert_bigint_eq_intmax(-42, v);
    TEST_ASSERT_TRUE(v.negative);

    TEST_ASSERT_TRUE(dsdl_bigint_from_intmax(&v, -1));
    assert_bigint_eq_intmax(-1, v);
    TEST_ASSERT_TRUE(v.negative);
}

static void test_abs_intmax_min(void)
{
    // Special case: INTMAX_MIN should round-trip.
    dsdl_bigint_t v;
    TEST_ASSERT_TRUE(dsdl_bigint_from_intmax(&v, INTMAX_MIN));
    assert_bigint_eq_intmax(INTMAX_MIN, v);
    TEST_ASSERT_TRUE(v.negative);
}

// ============================================================================
// Rational is_int tests
// ============================================================================

static void test_rational_is_int(void)
{
    dsdl_rational_t r;

    r = dsdl_rational_from_int(42);
    TEST_ASSERT_TRUE(dsdl_rational_is_int(r));

    r = make_rational(1, 2);
    TEST_ASSERT_FALSE(dsdl_rational_is_int(r));

    r = make_rational(4, 2);
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
    assert_bigint_eq_intmax(-5, r.num);

    r = make_rational(3, 7);
    r = dsdl_rational_neg(r);
    assert_bigint_eq_intmax(-3, r.num);
    assert_bigint_eq_uintmax(7, r.den);
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
    TEST_ASSERT_TRUE(dsdl_parse_typename(wkv_key("TypeName.255.255"), &ref));
    TEST_ASSERT_EQUAL_size_t(8, ref.type_name.len);
    TEST_ASSERT_EQUAL_STRING_LEN("TypeName", ref.type_name.str, 8);
    TEST_ASSERT_TRUE(ref.has_major);
    TEST_ASSERT_TRUE(ref.has_minor);
    TEST_ASSERT_EQUAL_UINT8(255, ref.major);
    TEST_ASSERT_EQUAL_UINT8(255, ref.minor);
}

// ============================================================================
// Bigint overflow tests
// ============================================================================

static void test_bigint_overflow_from_uintmax(void)
{
    dsdl_bigint_t v;
    TEST_ASSERT_TRUE(dsdl_bigint_from_uintmax(&v, UINTMAX_MAX));
    TEST_ASSERT_TRUE(v.limb_count <= DSDL_BIGINT_LIMB_COUNT);
}

static void test_bigint_overflow_add_abs(void)
{
    dsdl_bigint_t a;
    dsdl_bigint_t b;
    dsdl_bigint_t result;
    dsdl_bigint_zero(&a);
    dsdl_bigint_zero(&b);

    for (uint_least8_t i = 0; i < DSDL_BIGINT_LIMB_COUNT; i++) {
        a.limbs[i] = DSDL_BIGINT_BASE - 1;
        b.limbs[i] = DSDL_BIGINT_BASE - 1;
    }
    a.limb_count = DSDL_BIGINT_LIMB_COUNT;
    b.limb_count = DSDL_BIGINT_LIMB_COUNT;

    TEST_ASSERT_FALSE(dsdl_bigint_add_abs(&a, &b, &result));
    TEST_ASSERT_EQUAL_UINT8(0, result.limb_count);
}

static void test_bigint_overflow_mul_abs(void)
{
    dsdl_bigint_t a;
    dsdl_bigint_t b;
    dsdl_bigint_t result;
    dsdl_bigint_zero(&a);
    dsdl_bigint_zero(&b);

    for (uint_least8_t i = 0; i < 5; i++) {
        a.limbs[i] = DSDL_BIGINT_BASE - 1;
        b.limbs[i] = DSDL_BIGINT_BASE - 1;
    }
    a.limb_count = 5;
    b.limb_count = 5;

    TEST_ASSERT_FALSE(dsdl_bigint_mul_abs(&a, &b, &result));
    TEST_ASSERT_EQUAL_UINT8(0, result.limb_count);
}

static void test_bigint_overflow_mul_small_inplace(void)
{
    dsdl_bigint_t v;
    dsdl_bigint_zero(&v);

    for (uint_least8_t i = 0; i < DSDL_BIGINT_LIMB_COUNT; i++) {
        v.limbs[i] = DSDL_BIGINT_BASE - 1;
    }
    v.limb_count = DSDL_BIGINT_LIMB_COUNT;

    TEST_ASSERT_FALSE(dsdl_bigint_mul_small_inplace(&v, 2U));
}

static void test_bigint_overflow_add_small_inplace(void)
{
    dsdl_bigint_t v;
    dsdl_bigint_zero(&v);

    for (uint_least8_t i = 0; i < DSDL_BIGINT_LIMB_COUNT; i++) {
        v.limbs[i] = DSDL_BIGINT_BASE - 1;
    }
    v.limb_count = DSDL_BIGINT_LIMB_COUNT;

    TEST_ASSERT_FALSE(dsdl_bigint_add_small_inplace(&v, 1U));
}

static void test_bigint_overflow_shift_base_add(void)
{
    dsdl_bigint_t v;
    dsdl_bigint_zero(&v);

    for (uint_least8_t i = 0; i < DSDL_BIGINT_LIMB_COUNT; i++) {
        v.limbs[i] = DSDL_BIGINT_BASE - 1;
    }
    v.limb_count = DSDL_BIGINT_LIMB_COUNT;

    TEST_ASSERT_FALSE(dsdl_bigint_shift_base_add(&v, 1U));
}

// ============================================================================
// Main
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

    // Bigint overflow tests
    RUN_TEST(test_bigint_overflow_from_uintmax);
    RUN_TEST(test_bigint_overflow_add_abs);
    RUN_TEST(test_bigint_overflow_mul_abs);
    RUN_TEST(test_bigint_overflow_mul_small_inplace);
    RUN_TEST(test_bigint_overflow_add_small_inplace);
    RUN_TEST(test_bigint_overflow_shift_base_add);

    return UNITY_END();
}
