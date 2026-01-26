/// Test suite for symbolic bit length set implementation
///
/// Uses ThrowTheSwitch Unity framework.
/// Tests the dsdl_bls_* functions for correct symbolic evaluation.

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

static dsdl_t test_dsdl;

void setUp(void) { dsdl_new(&test_dsdl, test_realloc, NULL, NULL); }

void tearDown(void) { dsdl_destroy(&test_dsdl); }

// ============================================================================
// Basic construction tests
// ============================================================================

static void test_bls_single(void)
{
    dsdl_bls_t* const bls = dsdl_bls_new_single(&test_dsdl, 42);
    TEST_ASSERT_NOT_NULL(bls);
    TEST_ASSERT_EQUAL(dsdl_bls_nullary, bls->kind);
    TEST_ASSERT_EQUAL_size_t(42, dsdl_bls_min(bls));
    TEST_ASSERT_EQUAL_size_t(42, dsdl_bls_max(bls));
    TEST_ASSERT_TRUE(dsdl_bls_is_fixed(bls));
}

static void test_bls_set(void)
{
    const uint64_t    values[] = { 16, 8, 32, 8, 24 }; // Unsorted with duplicate
    dsdl_bls_t* const bls      = dsdl_bls_new_set(&test_dsdl, 5, values);
    TEST_ASSERT_NOT_NULL(bls);
    TEST_ASSERT_EQUAL(dsdl_bls_nullary, bls->kind);
    TEST_ASSERT_EQUAL_size_t(4, bls->data.nullary.count); // Deduplicated
    TEST_ASSERT_EQUAL_size_t(8, dsdl_bls_min(bls));
    TEST_ASSERT_EQUAL_size_t(32, dsdl_bls_max(bls));
    TEST_ASSERT_FALSE(dsdl_bls_is_fixed(bls));
}

// ============================================================================
// Primitive type tests
// ============================================================================

static void test_bls_primitive_uint8(void)
{
    // uint8 has bit length set {8}
    dsdl_bls_t* const bls = dsdl_bls_new_single(&test_dsdl, 8);
    TEST_ASSERT_EQUAL_size_t(8, dsdl_bls_min(bls));
    TEST_ASSERT_EQUAL_size_t(8, dsdl_bls_max(bls));
    TEST_ASSERT_TRUE(dsdl_bls_is_aligned(&test_dsdl, bls, 8));
    TEST_ASSERT_FALSE(dsdl_bls_is_aligned(&test_dsdl, bls, 16));
}

static void test_bls_primitive_float16(void)
{
    // float16 has bit length set {16}
    dsdl_bls_t* const bls = dsdl_bls_new_single(&test_dsdl, 16);
    TEST_ASSERT_EQUAL_size_t(16, dsdl_bls_min(bls));
    TEST_ASSERT_EQUAL_size_t(16, dsdl_bls_max(bls));
    TEST_ASSERT_TRUE(dsdl_bls_is_aligned(&test_dsdl, bls, 8));
    TEST_ASSERT_TRUE(dsdl_bls_is_aligned(&test_dsdl, bls, 16));
    TEST_ASSERT_FALSE(dsdl_bls_is_aligned(&test_dsdl, bls, 32));
}

// ============================================================================
// Fixed array tests
// ============================================================================

static void test_bls_fixed_array_uint8_4(void)
{
    // uint8[4] has bit length set {32}
    dsdl_bls_t* const elem = dsdl_bls_new_single(&test_dsdl, 8);
    dsdl_bls_t* const bls  = dsdl_bls_new_repeat(&test_dsdl, elem, 4);
    TEST_ASSERT_EQUAL_size_t(32, dsdl_bls_min(bls));
    TEST_ASSERT_EQUAL_size_t(32, dsdl_bls_max(bls));
    TEST_ASSERT_TRUE(dsdl_bls_is_fixed(bls));
}

static void test_bls_fixed_array_repeat_1(void)
{
    // uint8[1] should optimize to just {8}
    dsdl_bls_t* const elem = dsdl_bls_new_single(&test_dsdl, 8);
    dsdl_bls_t* const bls  = dsdl_bls_new_repeat(&test_dsdl, elem, 1);
    // Optimization: repeat(1) returns child directly
    TEST_ASSERT_EQUAL_PTR(elem, bls);
    TEST_ASSERT_EQUAL_size_t(8, dsdl_bls_min(bls));
}

static void test_bls_fixed_array_repeat_0(void)
{
    // uint8[0] should give {0}
    dsdl_bls_t* const elem = dsdl_bls_new_single(&test_dsdl, 8);
    dsdl_bls_t* const bls  = dsdl_bls_new_repeat(&test_dsdl, elem, 0);
    TEST_ASSERT_EQUAL_size_t(0, dsdl_bls_min(bls));
    TEST_ASSERT_EQUAL_size_t(0, dsdl_bls_max(bls));
}

// ============================================================================
// Variable array tests
// ============================================================================

static void test_bls_variable_array_uint8_3(void)
{
    // uint8[<=3] has bit length set = length_prefix + repeat_range(3)
    // Length prefix is 8 bits (for capacity 3)
    // repeat_range({8}, 3) = {0, 8, 16, 24}
    // Total = 8 + {0, 8, 16, 24} = {8, 16, 24, 32}
    dsdl_bls_t* const elem     = dsdl_bls_new_single(&test_dsdl, 8);
    dsdl_bls_t* const var_part = dsdl_bls_new_repeat_range(&test_dsdl, elem, 3);
    dsdl_bls_t* const prefix   = dsdl_bls_new_single(&test_dsdl, 8);

    dsdl_bls_t*       children[2] = { prefix, var_part };
    dsdl_bls_t* const bls         = dsdl_bls_new_concat(&test_dsdl, 2, children);

    TEST_ASSERT_EQUAL_size_t(8, dsdl_bls_min(bls));
    TEST_ASSERT_EQUAL_size_t(32, dsdl_bls_max(bls));
    TEST_ASSERT_FALSE(dsdl_bls_is_fixed(bls));

    // Check modulo 8 alignment
    uint64_t       mods[64];
    const uint64_t count = dsdl_bls_modulo(&test_dsdl, bls, 8, mods);
    TEST_ASSERT_EQUAL_size_t(1, count);
    TEST_ASSERT_EQUAL_size_t(0, mods[0]); // All values are multiples of 8
}

static void test_bls_variable_array_uint8_1(void)
{
    // uint8[<=1] = 8 + {0, 8} = {8, 16}
    dsdl_bls_t* const elem     = dsdl_bls_new_single(&test_dsdl, 8);
    dsdl_bls_t* const var_part = dsdl_bls_new_repeat_range(&test_dsdl, elem, 1);
    dsdl_bls_t* const prefix   = dsdl_bls_new_single(&test_dsdl, 8);

    dsdl_bls_t*       children[2] = { prefix, var_part };
    dsdl_bls_t* const bls         = dsdl_bls_new_concat(&test_dsdl, 2, children);

    TEST_ASSERT_EQUAL_size_t(8, dsdl_bls_min(bls));
    TEST_ASSERT_EQUAL_size_t(16, dsdl_bls_max(bls));
}

// ============================================================================
// Struct tests (concatenation)
// ============================================================================

static void test_bls_struct_two_fields(void)
{
    // struct { uint16 a; uint8 b; } has bit length set {16 + 8} = {24}
    dsdl_bls_t* const f1 = dsdl_bls_new_single(&test_dsdl, 16);
    dsdl_bls_t* const f2 = dsdl_bls_new_single(&test_dsdl, 8);

    dsdl_bls_t*       children[2] = { f1, f2 };
    dsdl_bls_t* const bls         = dsdl_bls_new_concat(&test_dsdl, 2, children);

    TEST_ASSERT_EQUAL_size_t(24, dsdl_bls_min(bls));
    TEST_ASSERT_EQUAL_size_t(24, dsdl_bls_max(bls));
    TEST_ASSERT_TRUE(dsdl_bls_is_fixed(bls));
}

static void test_bls_struct_with_variable_array(void)
{
    // struct { uint8[<=2] arr; uint8 end; }
    // arr bit length: 8 + {0, 8, 16} = {8, 16, 24}
    // end bit length: 8
    // Total: {8, 16, 24} + 8 = {16, 24, 32}
    dsdl_bls_t* const elem            = dsdl_bls_new_single(&test_dsdl, 8);
    dsdl_bls_t* const var_part        = dsdl_bls_new_repeat_range(&test_dsdl, elem, 2);
    dsdl_bls_t* const prefix          = dsdl_bls_new_single(&test_dsdl, 8);
    dsdl_bls_t*       arr_children[2] = { prefix, var_part };
    dsdl_bls_t* const arr_bls         = dsdl_bls_new_concat(&test_dsdl, 2, arr_children);

    dsdl_bls_t* const end_bls = dsdl_bls_new_single(&test_dsdl, 8);

    dsdl_bls_t*       struct_children[2] = { arr_bls, end_bls };
    dsdl_bls_t* const bls                = dsdl_bls_new_concat(&test_dsdl, 2, struct_children);

    TEST_ASSERT_EQUAL_size_t(16, dsdl_bls_min(bls));
    TEST_ASSERT_EQUAL_size_t(32, dsdl_bls_max(bls));
    TEST_ASSERT_FALSE(dsdl_bls_is_fixed(bls));
}

// ============================================================================
// Union tests
// ============================================================================

static void test_bls_union_two_variants(void)
{
    // union { uint16 a; uint8 b; }
    // Tag: 8 bits (for 2 variants)
    // Variants: {16} | {8} = {8, 16}
    // Total: 8 + {8, 16} = {16, 24}
    dsdl_bls_t* const v1  = dsdl_bls_new_single(&test_dsdl, 16);
    dsdl_bls_t* const v2  = dsdl_bls_new_single(&test_dsdl, 8);
    dsdl_bls_t* const tag = dsdl_bls_new_single(&test_dsdl, 8);

    dsdl_bls_t*       variants[2]  = { v1, v2 };
    dsdl_bls_t* const variants_bls = dsdl_bls_new_unite(&test_dsdl, 2, variants);

    dsdl_bls_t*       children[2] = { tag, variants_bls };
    dsdl_bls_t* const bls         = dsdl_bls_new_concat(&test_dsdl, 2, children);

    TEST_ASSERT_EQUAL_size_t(16, dsdl_bls_min(bls));
    TEST_ASSERT_EQUAL_size_t(24, dsdl_bls_max(bls));
}

// ============================================================================
// Padding tests
// ============================================================================

static void test_bls_padding_to_byte(void)
{
    // Padding 10 bits to byte alignment: ceil(10/8)*8 = 16
    dsdl_bls_t* const inner = dsdl_bls_new_single(&test_dsdl, 10);
    dsdl_bls_t* const bls   = dsdl_bls_new_pad(&test_dsdl, inner, 8);

    TEST_ASSERT_EQUAL_size_t(16, dsdl_bls_min(bls));
    TEST_ASSERT_EQUAL_size_t(16, dsdl_bls_max(bls));
    TEST_ASSERT_TRUE(dsdl_bls_is_aligned(&test_dsdl, bls, 8));
}

static void test_bls_padding_already_aligned(void)
{
    // Padding 16 bits to byte alignment: already aligned, stays 16
    dsdl_bls_t* const inner = dsdl_bls_new_single(&test_dsdl, 16);
    dsdl_bls_t* const bls   = dsdl_bls_new_pad(&test_dsdl, inner, 8);

    TEST_ASSERT_EQUAL_size_t(16, dsdl_bls_min(bls));
    TEST_ASSERT_EQUAL_size_t(16, dsdl_bls_max(bls));
}

static void test_bls_padding_variable_set(void)
{
    // Padding {10, 15, 17} to byte alignment: {16, 16, 24} = {16, 24}
    const uint64_t    values[] = { 10, 15, 17 };
    dsdl_bls_t* const inner    = dsdl_bls_new_set(&test_dsdl, 3, values);
    dsdl_bls_t* const bls      = dsdl_bls_new_pad(&test_dsdl, inner, 8);

    TEST_ASSERT_EQUAL_size_t(16, dsdl_bls_min(bls));
    TEST_ASSERT_EQUAL_size_t(24, dsdl_bls_max(bls));
    TEST_ASSERT_TRUE(dsdl_bls_is_aligned(&test_dsdl, bls, 8));
}

// ============================================================================
// Modulo tests
// ============================================================================

static void test_bls_modulo_fixed(void)
{
    // {32} % 8 = {0}
    dsdl_bls_t* const bls = dsdl_bls_new_single(&test_dsdl, 32);
    uint64_t          mods[64];
    const uint64_t    count = dsdl_bls_modulo(&test_dsdl, bls, 8, mods);
    TEST_ASSERT_EQUAL_size_t(1, count);
    TEST_ASSERT_EQUAL_size_t(0, mods[0]);
}

static void test_bls_modulo_variable_set(void)
{
    // {8, 12, 16} % 8 = {0, 4}
    const uint64_t    values[] = { 8, 12, 16 };
    dsdl_bls_t* const bls      = dsdl_bls_new_set(&test_dsdl, 3, values);
    uint64_t          mods[64];
    const uint64_t    count = dsdl_bls_modulo(&test_dsdl, bls, 8, mods);
    TEST_ASSERT_EQUAL_size_t(2, count);
    // Check both 0 and 4 are present (order may vary)
    bool has_0 = false, has_4 = false;
    for (uint64_t i = 0; i < count; i++) {
        if (mods[i] == 0)
            has_0 = true;
        if (mods[i] == 4)
            has_4 = true;
    }
    TEST_ASSERT_TRUE(has_0);
    TEST_ASSERT_TRUE(has_4);
}

static void test_bls_modulo_repeat(void)
{
    // repeat({8}, 3) = {24}, {24} % 8 = {0}
    dsdl_bls_t* const elem = dsdl_bls_new_single(&test_dsdl, 8);
    dsdl_bls_t* const bls  = dsdl_bls_new_repeat(&test_dsdl, elem, 3);
    uint64_t          mods[64];
    const uint64_t    count = dsdl_bls_modulo(&test_dsdl, bls, 8, mods);
    TEST_ASSERT_EQUAL_size_t(1, count);
    TEST_ASSERT_EQUAL_size_t(0, mods[0]);
}

static void test_bls_modulo_repeat_range(void)
{
    // repeat_range({8}, 3) = {0, 8, 16, 24}, all % 8 = {0}
    dsdl_bls_t* const elem = dsdl_bls_new_single(&test_dsdl, 8);
    dsdl_bls_t* const bls  = dsdl_bls_new_repeat_range(&test_dsdl, elem, 3);
    uint64_t          mods[64];
    const uint64_t    count = dsdl_bls_modulo(&test_dsdl, bls, 8, mods);
    TEST_ASSERT_EQUAL_size_t(1, count);
    TEST_ASSERT_EQUAL_size_t(0, mods[0]);
}

// ============================================================================
// Complex tests (based on PyDSDL examples)
// ============================================================================

static void test_bls_nested_variable_arrays(void)
{
    // uint8[<=2][<=2]
    // Inner: 8 + repeat_range({8}, 2) = 8 + {0, 8, 16} = {8, 16, 24}
    // Outer: 8 + repeat_range(inner, 2)
    //
    // This is a complex case that would explode numerically but works symbolically.
    dsdl_bls_t* const elem       = dsdl_bls_new_single(&test_dsdl, 8);
    dsdl_bls_t* const inner_var  = dsdl_bls_new_repeat_range(&test_dsdl, elem, 2);
    dsdl_bls_t* const inner_pref = dsdl_bls_new_single(&test_dsdl, 8);

    dsdl_bls_t*       inner_children[2] = { inner_pref, inner_var };
    dsdl_bls_t* const inner_bls         = dsdl_bls_new_concat(&test_dsdl, 2, inner_children);

    // Inner: min=8, max=24
    TEST_ASSERT_EQUAL_size_t(8, dsdl_bls_min(inner_bls));
    TEST_ASSERT_EQUAL_size_t(24, dsdl_bls_max(inner_bls));

    dsdl_bls_t* const outer_var  = dsdl_bls_new_repeat_range(&test_dsdl, inner_bls, 2);
    dsdl_bls_t* const outer_pref = dsdl_bls_new_single(&test_dsdl, 8);

    dsdl_bls_t*       outer_children[2] = { outer_pref, outer_var };
    dsdl_bls_t* const outer_bls         = dsdl_bls_new_concat(&test_dsdl, 2, outer_children);

    // Outer: min = 8 + 0 = 8, max = 8 + 24*2 = 56
    TEST_ASSERT_EQUAL_size_t(8, dsdl_bls_min(outer_bls));
    TEST_ASSERT_EQUAL_size_t(56, dsdl_bls_max(outer_bls));

    // Check byte alignment
    TEST_ASSERT_TRUE(dsdl_bls_is_aligned(&test_dsdl, outer_bls, 8));
}

static void test_bls_large_variable_array(void)
{
    // uint8[<=65536] - this would have 65537 elements numerically!
    // But symbolically: min=8 (length prefix), max=8 + 8*65536 = 524296
    dsdl_bls_t* const elem     = dsdl_bls_new_single(&test_dsdl, 8);
    dsdl_bls_t* const var_part = dsdl_bls_new_repeat_range(&test_dsdl, elem, 65536);
    // Length prefix for 65536 is 32 bits (ceil(log2(65537)) = 17, round to 32)
    dsdl_bls_t* const prefix = dsdl_bls_new_single(&test_dsdl, 32);

    dsdl_bls_t*       children[2] = { prefix, var_part };
    dsdl_bls_t* const bls         = dsdl_bls_new_concat(&test_dsdl, 2, children);

    TEST_ASSERT_EQUAL_size_t(32, dsdl_bls_min(bls));
    TEST_ASSERT_EQUAL_size_t(32 + 8 * 65536, dsdl_bls_max(bls));

    // Still computes quickly!
    TEST_ASSERT_TRUE(dsdl_bls_is_aligned(&test_dsdl, bls, 8));
}

static void test_bls_pydsdl_example(void)
{
    // From PyDSDL docstring:
    // b = 16 + BitLengthSet(8).repeat_range(256)
    // b.min, b.max => (16, 2064)
    // sorted(b % 16) => [0, 8]
    dsdl_bls_t* const elem     = dsdl_bls_new_single(&test_dsdl, 8);
    dsdl_bls_t* const var_part = dsdl_bls_new_repeat_range(&test_dsdl, elem, 256);
    dsdl_bls_t* const prefix   = dsdl_bls_new_single(&test_dsdl, 16);

    dsdl_bls_t*       children[2] = { prefix, var_part };
    dsdl_bls_t* const bls         = dsdl_bls_new_concat(&test_dsdl, 2, children);

    TEST_ASSERT_EQUAL_size_t(16, dsdl_bls_min(bls));
    TEST_ASSERT_EQUAL_size_t(16 + 8 * 256, dsdl_bls_max(bls));

    // b % 16 = {0, 8}
    uint64_t       mods[64];
    const uint64_t count = dsdl_bls_modulo(&test_dsdl, bls, 16, mods);
    TEST_ASSERT_EQUAL_size_t(2, count);
    bool has_0 = false, has_8 = false;
    for (uint64_t i = 0; i < count; i++) {
        if (mods[i] == 0)
            has_0 = true;
        if (mods[i] == 8)
            has_8 = true;
    }
    TEST_ASSERT_TRUE(has_0);
    TEST_ASSERT_TRUE(has_8);
}

// ============================================================================
// Main
// ============================================================================

int main(void)
{
    UNITY_BEGIN();

    // Basic construction
    RUN_TEST(test_bls_single);
    RUN_TEST(test_bls_set);

    // Primitive types
    RUN_TEST(test_bls_primitive_uint8);
    RUN_TEST(test_bls_primitive_float16);

    // Fixed arrays
    RUN_TEST(test_bls_fixed_array_uint8_4);
    RUN_TEST(test_bls_fixed_array_repeat_1);
    RUN_TEST(test_bls_fixed_array_repeat_0);

    // Variable arrays
    RUN_TEST(test_bls_variable_array_uint8_3);
    RUN_TEST(test_bls_variable_array_uint8_1);

    // Structs
    RUN_TEST(test_bls_struct_two_fields);
    RUN_TEST(test_bls_struct_with_variable_array);

    // Unions
    RUN_TEST(test_bls_union_two_variants);

    // Padding
    RUN_TEST(test_bls_padding_to_byte);
    RUN_TEST(test_bls_padding_already_aligned);
    RUN_TEST(test_bls_padding_variable_set);

    // Modulo
    RUN_TEST(test_bls_modulo_fixed);
    RUN_TEST(test_bls_modulo_variable_set);
    RUN_TEST(test_bls_modulo_repeat);
    RUN_TEST(test_bls_modulo_repeat_range);

    // Complex cases
    RUN_TEST(test_bls_nested_variable_arrays);
    RUN_TEST(test_bls_large_variable_array);
    RUN_TEST(test_bls_pydsdl_example);

    return UNITY_END();
}
