/// C++20 API compatibility test for dsdl.h
///
/// This test verifies that the dsdl.h header compiles correctly with C++20
/// and that the API is usable from C++ code.

// Ensure C++20
#if __cplusplus < 202002L
#error "This test requires C++20 or later"
#endif

extern "C"
{
#include "unity.h"
}

#include "dsdl.h"

#include <cstdlib>
#include <cstring>

// ============================================================================
// Test allocator using standard library
// ============================================================================

static void* test_realloc(dsdl_t* /*self*/, void* ptr, size_t size)
{
    if (size == 0) {
        std::free(ptr);
        return nullptr;
    }
    return std::realloc(ptr, size);
}

// ============================================================================
// Tests
// ============================================================================

void setUp()
{
    // Called before each test
}

void tearDown()
{
    // Called after each test
}

static void test_cpp_api_init_destroy()
{
    dsdl_t dsdl{};
    dsdl_new(&dsdl, test_realloc);

    // Verify initial state
    TEST_ASSERT_NOT_NULL(dsdl.realloc);

    dsdl_destroy(&dsdl);
}

static void test_cpp_api_add_namespace()
{
    dsdl_t dsdl{};
    dsdl_new(&dsdl, test_realloc);

    const char*     path = "/test/namespace";
    const wkv_str_t ns   = { std::strlen(path), path };

    const bool result = dsdl_add_namespace(&dsdl, ns);
    TEST_ASSERT_TRUE(result);

    dsdl_destroy(&dsdl);
}

static void test_cpp_type_helpers()
{
    // Test inline type helper functions
    TEST_ASSERT_TRUE(dsdl_type_is_void(DSDL_VOID8));
    TEST_ASSERT_FALSE(dsdl_type_is_void(DSDL_INT8));

    TEST_ASSERT_TRUE(dsdl_type_is_int(DSDL_INT32));
    TEST_ASSERT_FALSE(dsdl_type_is_int(DSDL_UINT32));

    TEST_ASSERT_TRUE(dsdl_type_is_uint(DSDL_UINT64));
    TEST_ASSERT_FALSE(dsdl_type_is_uint(DSDL_FLOAT64));

    TEST_ASSERT_TRUE(dsdl_type_is_float(DSDL_FLOAT32));
    TEST_ASSERT_FALSE(dsdl_type_is_float(DSDL_INT32));

    TEST_ASSERT_TRUE(dsdl_type_is_array(DSDL_ARRAY_FIXED));
    TEST_ASSERT_TRUE(dsdl_type_is_array(DSDL_ARRAY_VARIABLE));
    TEST_ASSERT_FALSE(dsdl_type_is_array(DSDL_INT8));

    TEST_ASSERT_TRUE(dsdl_type_is_composite(DSDL_COMPOSITE_STRUCT));
    TEST_ASSERT_TRUE(dsdl_type_is_composite(DSDL_COMPOSITE_UNION));
    TEST_ASSERT_TRUE(dsdl_type_is_composite(DSDL_COMPOSITE_RPC));
    TEST_ASSERT_FALSE(dsdl_type_is_composite(DSDL_UINT8));

    TEST_ASSERT_TRUE(dsdl_type_is_alias(DSDL_BOOL));
    TEST_ASSERT_TRUE(dsdl_type_is_alias(DSDL_BYTE));
    TEST_ASSERT_FALSE(dsdl_type_is_alias(DSDL_UINT8));

    // Test bit width extraction
    TEST_ASSERT_EQUAL_UINT8(8, dsdl_type_bit_width(DSDL_UINT8));
    TEST_ASSERT_EQUAL_UINT8(16, dsdl_type_bit_width(DSDL_INT16));
    TEST_ASSERT_EQUAL_UINT8(32, dsdl_type_bit_width(DSDL_FLOAT32));
    TEST_ASSERT_EQUAL_UINT8(64, dsdl_type_bit_width(DSDL_VOID64));
}

static void test_cpp_wkv_str_usage()
{
    // Verify wkv_str_t works correctly from C++
    const char*     str  = "test.type.Name.1.0";
    const wkv_str_t wstr = wkv_key(str);

    TEST_ASSERT_EQUAL_size_t(std::strlen(str), wstr.len);
    TEST_ASSERT_EQUAL_PTR(str, wstr.str);
}

static void test_cpp_lambda_allocator()
{
    // C++20: verify we can use lambda as allocator (via static)
    // Note: lambdas can't be directly used as C function pointers if they capture,
    // but captureless lambdas are OK.

    static auto allocator = +[](dsdl_t* /*self*/, void* ptr, size_t size) -> void* {
        if (size == 0) {
            std::free(ptr);
            return nullptr;
        }
        return std::realloc(ptr, size);
    };

    dsdl_t dsdl{};
    dsdl_new(&dsdl, allocator);

    TEST_ASSERT_NOT_NULL(dsdl.realloc);

    dsdl_destroy(&dsdl);
}

// ============================================================================
// Main
// ============================================================================

int main()
{
    UNITY_BEGIN();

    RUN_TEST(test_cpp_api_init_destroy);
    RUN_TEST(test_cpp_api_add_namespace);
    RUN_TEST(test_cpp_type_helpers);
    RUN_TEST(test_cpp_wkv_str_usage);
    RUN_TEST(test_cpp_lambda_allocator);

    return UNITY_END();
}
