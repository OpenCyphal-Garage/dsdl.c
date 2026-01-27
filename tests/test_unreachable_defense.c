/// Test suite for unreachable defense assertions
///
/// This file exercises all switch case branches in three internal functions:
/// - dsdl_bls_expand() - all 6 BLS kinds
/// - dsdl_value_clone() - all 6 value kinds
/// - dsdl_value_equal() - all 6 value kinds
///
/// Uses ThrowTheSwitch Unity framework.
/// Includes dsdl.c directly to access internal functions.

// Include dsdl.c directly to access internal functions
#include "dsdl.c"

#include "unity.h"

#include <stdlib.h>
#include <string.h>

// ============================================================================
// Test allocator
// ============================================================================

static void* test_realloc(dsdl_t* self, void* ptr, size_t new_size)
{
    (void)self;
    if (new_size == 0) {
        free(ptr);
        return NULL;
    }
    return realloc(ptr, new_size);
}

static dsdl_t g_dsdl;

void setUp(void) { dsdl_new(&g_dsdl, test_realloc, NULL, NULL); }

void tearDown(void) { dsdl_destroy(&g_dsdl); }

// ============================================================================
// Helper functions
// ============================================================================

static dsdl_rational_t make_rational(const intmax_t num, const uintmax_t den)
{
    dsdl_rational_t r;
    TEST_ASSERT_TRUE(dsdl_bigint_from_intmax(&r.num, num));
    TEST_ASSERT_TRUE(dsdl_bigint_from_uintmax(&r.den, den));
    return r;
}

static bool test_deferred_clone_func(const dsdl_closure_t* src, dsdl_closure_t* dst)
{
    if ((src == NULL) || (dst == NULL)) {
        return false;
    }
    dst->context = src->context;
    return true;
}

// ============================================================================
// dsdl_bls_expand() tests - all 6 BLS kinds
// ============================================================================

static void test_bls_expand_nullary(void)
{
    // Test nullary case: concrete set of integers
    const uint64_t    values[] = { 8, 16, 32 };
    dsdl_bls_t* const bls      = dsdl_bls_new_set(&g_dsdl, 3, values);

    uint64_t*  out_values = NULL;
    size_t     out_count  = 0;
    const bool result     = dsdl_bls_expand(&g_dsdl, bls, &out_values, &out_count);

    TEST_ASSERT_TRUE(result);
    TEST_ASSERT_EQUAL_size_t(3, out_count);
    TEST_ASSERT_EQUAL_size_t(8, out_values[0]);
    TEST_ASSERT_EQUAL_size_t(16, out_values[1]);
    TEST_ASSERT_EQUAL_size_t(32, out_values[2]);
    dsdl_free(&g_dsdl, out_values);
}

static void test_bls_expand_concat(void)
{
    // Test concat case: concatenation (sum of cartesian product)
    dsdl_bls_t* const child1      = dsdl_bls_new_single(&g_dsdl, 8);
    dsdl_bls_t* const child2      = dsdl_bls_new_single(&g_dsdl, 16);
    dsdl_bls_t*       children[2] = { child1, child2 };
    dsdl_bls_t* const bls         = dsdl_bls_new_concat(&g_dsdl, 2, children);

    uint64_t*  values = NULL;
    size_t     count  = 0;
    const bool result = dsdl_bls_expand(&g_dsdl, bls, &values, &count);

    TEST_ASSERT_TRUE(result);
    TEST_ASSERT_EQUAL_size_t(1, count);
    TEST_ASSERT_EQUAL_size_t(24, values[0]); // 8 + 16 = 24
    dsdl_free(&g_dsdl, values);
}

static void test_bls_expand_repeat(void)
{
    // Test repeat case: fixed repetition (child * k)
    dsdl_bls_t* const child = dsdl_bls_new_single(&g_dsdl, 8);
    dsdl_bls_t* const bls   = dsdl_bls_new_repeat(&g_dsdl, child, 3);

    uint64_t*  values = NULL;
    size_t     count  = 0;
    const bool result = dsdl_bls_expand(&g_dsdl, bls, &values, &count);

    TEST_ASSERT_TRUE(result);
    TEST_ASSERT_EQUAL_size_t(1, count);
    TEST_ASSERT_EQUAL_size_t(24, values[0]); // 8 * 3 = 24
    dsdl_free(&g_dsdl, values);
}

static void test_bls_expand_repeat_range(void)
{
    // Test repeat_range case: range repetition (child * [0..k_max])
    dsdl_bls_t* const child = dsdl_bls_new_single(&g_dsdl, 8);
    dsdl_bls_t* const bls   = dsdl_bls_new_repeat_range(&g_dsdl, child, 2);

    uint64_t*  values = NULL;
    size_t     count  = 0;
    const bool result = dsdl_bls_expand(&g_dsdl, bls, &values, &count);

    TEST_ASSERT_TRUE(result);
    // Should have: 0 (k=0), 8 (k=1), 16 (k=2)
    TEST_ASSERT_EQUAL_size_t(3, count);
    TEST_ASSERT_EQUAL_size_t(0, values[0]);
    TEST_ASSERT_EQUAL_size_t(8, values[1]);
    TEST_ASSERT_EQUAL_size_t(16, values[2]);
    dsdl_free(&g_dsdl, values);
}

static void test_bls_expand_union(void)
{
    // Test union case: set union of children
    dsdl_bls_t* const child1      = dsdl_bls_new_single(&g_dsdl, 8);
    dsdl_bls_t* const child2      = dsdl_bls_new_single(&g_dsdl, 16);
    dsdl_bls_t*       children[2] = { child1, child2 };
    dsdl_bls_t* const bls         = dsdl_bls_new_unite(&g_dsdl, 2, children);

    uint64_t*  values = NULL;
    size_t     count  = 0;
    const bool result = dsdl_bls_expand(&g_dsdl, bls, &values, &count);

    TEST_ASSERT_TRUE(result);
    TEST_ASSERT_EQUAL_size_t(2, count);
    bool has_8  = false;
    bool has_16 = false;
    for (size_t i = 0; i < count; i++) {
        if (values[i] == 8) {
            has_8 = true;
        }
        if (values[i] == 16) {
            has_16 = true;
        }
    }
    TEST_ASSERT_TRUE(has_8);
    TEST_ASSERT_TRUE(has_16);
    dsdl_free(&g_dsdl, values);
}

static void test_bls_expand_pad(void)
{
    // Test pad case: padding (align child to boundary)
    dsdl_bls_t* const child = dsdl_bls_new_single(&g_dsdl, 7);
    dsdl_bls_t* const bls   = dsdl_bls_new_pad(&g_dsdl, child, 8);

    uint64_t*  values = NULL;
    size_t     count  = 0;
    const bool result = dsdl_bls_expand(&g_dsdl, bls, &values, &count);

    TEST_ASSERT_TRUE(result);
    TEST_ASSERT_EQUAL_size_t(1, count);
    TEST_ASSERT_EQUAL_size_t(8, values[0]); // 7 aligned up to 8
    dsdl_free(&g_dsdl, values);
}

// ============================================================================
// dsdl_value_clone() tests - all 6 value kinds
// ============================================================================

static void test_value_clone_rational(void)
{
    // Test cloning rational value
    dsdl_value_t src = { 0 };
    src.kind         = dsdl_value_rational;
    src.as.rational  = make_rational(3, 7);

    dsdl_value_t dst = { 0 };
    TEST_ASSERT_TRUE(dsdl_value_clone(&g_dsdl, &src, &dst));
    TEST_ASSERT_EQUAL(dsdl_value_rational, dst.kind);

    // Verify the cloned value is equal
    TEST_ASSERT_TRUE(dsdl_value_equal(&src, &dst));
}

static void test_value_clone_bool(void)
{
    // Test cloning bool value
    dsdl_value_t src = { 0 };
    src.kind         = dsdl_value_bool;
    src.as.boolean   = true;

    dsdl_value_t dst = { 0 };
    TEST_ASSERT_TRUE(dsdl_value_clone(&g_dsdl, &src, &dst));
    TEST_ASSERT_EQUAL(dsdl_value_bool, dst.kind);
    TEST_ASSERT_TRUE(dst.as.boolean);

    // Verify the cloned value is equal
    TEST_ASSERT_TRUE(dsdl_value_equal(&src, &dst));
}

static void test_value_clone_type(void)
{
    // Test cloning type value
    dsdl_value_t src = { 0 };
    src.kind         = dsdl_value_type;
    src.as.type_ref  = (void*)(uintptr_t)0x12345678;

    dsdl_value_t dst = { 0 };
    TEST_ASSERT_TRUE(dsdl_value_clone(&g_dsdl, &src, &dst));
    TEST_ASSERT_EQUAL(dsdl_value_type, dst.kind);
    TEST_ASSERT_EQUAL_PTR((void*)(uintptr_t)0x12345678, dst.as.type_ref);

    // Verify the cloned value is equal
    TEST_ASSERT_TRUE(dsdl_value_equal(&src, &dst));
}

static void test_value_clone_string(void)
{
    // Test cloning string value with actual content
    const char   test_str[] = "Hello, World!";
    dsdl_value_t src        = { 0 };
    src.kind                = dsdl_value_string;
    src.as.string           = (wkv_str_t){ .str = test_str, .len = strlen(test_str) };

    dsdl_value_t dst = { 0 };
    TEST_ASSERT_TRUE(dsdl_value_clone(&g_dsdl, &src, &dst));
    TEST_ASSERT_EQUAL(dsdl_value_string, dst.kind);
    TEST_ASSERT_EQUAL_size_t(strlen(test_str), dst.as.string.len);
    TEST_ASSERT_EQUAL_MEMORY(test_str, dst.as.string.str, strlen(test_str));

    // Verify the cloned value is equal
    TEST_ASSERT_TRUE(dsdl_value_equal(&src, &dst));

    // Clean up
    dsdl_value_dispose(&g_dsdl, &dst);
}

static void test_value_clone_set(void)
{
    // Test cloning set value with elements
    dsdl_value_t elem1 = { 0 };
    elem1.kind         = dsdl_value_rational;
    elem1.as.rational  = make_rational(1, 2);

    dsdl_value_t elem2 = { 0 };
    elem2.kind         = dsdl_value_rational;
    elem2.as.rational  = make_rational(3, 4);

    dsdl_value_t src    = { 0 };
    src.kind            = dsdl_value_set;
    src.as.set.count    = 2;
    src.as.set.elements = (dsdl_value_t*)malloc(2 * sizeof(dsdl_value_t)); // Use malloc for test setup
    TEST_ASSERT_NOT_NULL(src.as.set.elements);
    src.as.set.elements[0] = elem1;
    src.as.set.elements[1] = elem2;

    dsdl_value_t dst = { 0 };
    TEST_ASSERT_TRUE(dsdl_value_clone(&g_dsdl, &src, &dst));
    TEST_ASSERT_EQUAL(dsdl_value_set, dst.kind);
    TEST_ASSERT_EQUAL_size_t(2, dst.as.set.count);
    TEST_ASSERT_NOT_NULL(dst.as.set.elements);

    // Verify elements were cloned
    TEST_ASSERT_EQUAL(dsdl_value_rational, dst.as.set.elements[0].kind);
    TEST_ASSERT_EQUAL(dsdl_value_rational, dst.as.set.elements[1].kind);

    // Clean up
    free(src.as.set.elements);
    dsdl_value_dispose(&g_dsdl, &dst);
}

static void test_value_clone_deferred(void)
{
    // Test cloning deferred value with closure
    dsdl_closure_t closure = { 0 };
    closure.context        = (void*)(uintptr_t)0xDEADBEEF;
    closure.clone          = test_deferred_clone_func;

    dsdl_value_t src = { 0 };
    src.kind         = dsdl_value_deferred;
    src.as.deferred  = closure;

    dsdl_value_t dst = { 0 };
    TEST_ASSERT_TRUE(dsdl_value_clone(&g_dsdl, &src, &dst));
    TEST_ASSERT_EQUAL(dsdl_value_deferred, dst.kind);
    TEST_ASSERT_EQUAL_PTR((void*)(uintptr_t)0xDEADBEEF, dst.as.deferred.context);
}

// ============================================================================
// dsdl_value_equal() tests - all 6 value kinds
// ============================================================================

static void test_value_equal_rational(void)
{
    // Test equality of rational values
    dsdl_value_t v1 = { 0 };
    v1.kind         = dsdl_value_rational;
    v1.as.rational  = make_rational(1, 2);

    dsdl_value_t v2 = { 0 };
    v2.kind         = dsdl_value_rational;
    v2.as.rational  = make_rational(1, 2);

    TEST_ASSERT_TRUE(dsdl_value_equal(&v1, &v2));

    // Test inequality
    v2.as.rational = make_rational(2, 3);
    TEST_ASSERT_FALSE(dsdl_value_equal(&v1, &v2));
}

static void test_value_equal_bool(void)
{
    // Test equality of bool values
    dsdl_value_t v1 = { 0 };
    v1.kind         = dsdl_value_bool;
    v1.as.boolean   = true;

    dsdl_value_t v2 = { 0 };
    v2.kind         = dsdl_value_bool;
    v2.as.boolean   = true;

    TEST_ASSERT_TRUE(dsdl_value_equal(&v1, &v2));

    // Test inequality
    v2.as.boolean = false;
    TEST_ASSERT_FALSE(dsdl_value_equal(&v1, &v2));
}

static void test_value_equal_type(void)
{
    // Test equality of type values
    dsdl_value_t v1 = { 0 };
    v1.kind         = dsdl_value_type;
    v1.as.type_ref  = (void*)(uintptr_t)0xABCD;

    dsdl_value_t v2 = { 0 };
    v2.kind         = dsdl_value_type;
    v2.as.type_ref  = (void*)(uintptr_t)0xABCD;

    TEST_ASSERT_TRUE(dsdl_value_equal(&v1, &v2));

    // Test inequality
    v2.as.type_ref = (void*)(uintptr_t)0xDEAD;
    TEST_ASSERT_FALSE(dsdl_value_equal(&v1, &v2));
}

static void test_value_equal_string(void)
{
    // Test equality of string values with NFC normalization
    const char str1[] = "Hello";
    const char str2[] = "Hello";
    const char str3[] = "World";

    dsdl_value_t v1 = { 0 };
    v1.kind         = dsdl_value_string;
    v1.as.string    = (wkv_str_t){ .str = str1, .len = strlen(str1) };

    dsdl_value_t v2 = { 0 };
    v2.kind         = dsdl_value_string;
    v2.as.string    = (wkv_str_t){ .str = str2, .len = strlen(str2) };

    TEST_ASSERT_TRUE(dsdl_value_equal(&v1, &v2));

    // Test inequality
    v2.as.string = (wkv_str_t){ .str = str3, .len = strlen(str3) };
    TEST_ASSERT_FALSE(dsdl_value_equal(&v1, &v2));
}

static void test_value_equal_set(void)
{
    // Test that set values always return false for equality
    dsdl_value_t v1 = { 0 };
    v1.kind         = dsdl_value_set;
    v1.as.set.count = 0;

    dsdl_value_t v2 = { 0 };
    v2.kind         = dsdl_value_set;
    v2.as.set.count = 0;

    // Sets always return false for equality (per implementation)
    TEST_ASSERT_FALSE(dsdl_value_equal(&v1, &v2));
}

static void test_value_equal_deferred(void)
{
    // Test that deferred values always return false for equality
    dsdl_value_t v1 = { 0 };
    v1.kind         = dsdl_value_deferred;

    dsdl_value_t v2 = { 0 };
    v2.kind         = dsdl_value_deferred;

    // Deferred values always return false for equality (per implementation)
    TEST_ASSERT_FALSE(dsdl_value_equal(&v1, &v2));
}

// ============================================================================
// Main test runner
// ============================================================================

int main(void)
{
    UNITY_BEGIN();

    // dsdl_bls_expand() tests
    RUN_TEST(test_bls_expand_nullary);
    RUN_TEST(test_bls_expand_concat);
    RUN_TEST(test_bls_expand_repeat);
    RUN_TEST(test_bls_expand_repeat_range);
    RUN_TEST(test_bls_expand_union);
    RUN_TEST(test_bls_expand_pad);

    // dsdl_value_clone() tests
    RUN_TEST(test_value_clone_rational);
    RUN_TEST(test_value_clone_bool);
    RUN_TEST(test_value_clone_type);
    RUN_TEST(test_value_clone_string);
    RUN_TEST(test_value_clone_set);
    RUN_TEST(test_value_clone_deferred);

    // dsdl_value_equal() tests
    RUN_TEST(test_value_equal_rational);
    RUN_TEST(test_value_equal_bool);
    RUN_TEST(test_value_equal_type);
    RUN_TEST(test_value_equal_string);
    RUN_TEST(test_value_equal_set);
    RUN_TEST(test_value_equal_deferred);

    return UNITY_END();
}
