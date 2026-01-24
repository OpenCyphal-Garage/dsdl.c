/// Test type resolution and recursive loading
///
/// Tests for dsdl_read() and dependency resolution.

// Include the implementation directly for internal access
#include "dsdl.c"

#include "unity.h"

#include <stdlib.h>
#include <string.h>

// ============================================================================
// Test helpers
// ============================================================================

static dsdl_t g_dsdl;

/// Standard realloc wrapper for tests
static void* test_realloc(dsdl_t* self, void* ptr, size_t new_size)
{
    (void)self;
    if (new_size == 0) {
        free(ptr);
        return NULL;
    }
    return realloc(ptr, new_size);
}

/// Standard file reader for tests
static void* test_read_file(dsdl_t* self, wkv_str_t path, size_t* out_size)
{
    (void)self;

    // Null-terminate path
    char path_buf[512];
    if (path.len >= sizeof(path_buf)) {
        return NULL;
    }
    memcpy(path_buf, path.str, path.len);
    path_buf[path.len] = '\0';

    FILE* f = fopen(path_buf, "rb");
    if (f == NULL) {
        return NULL;
    }

    // Get file size
    fseek(f, 0, SEEK_END);
    const long size = ftell(f);
    fseek(f, 0, SEEK_SET);

    if (size < 0) {
        fclose(f);
        return NULL;
    }

    // Allocate buffer
    void* buffer = self->realloc(self, NULL, (size_t)size);
    if (buffer == NULL) {
        fclose(f);
        return NULL;
    }

    // Read file
    const size_t read_count = fread(buffer, 1, (size_t)size, f);
    fclose(f);

    if (read_count != (size_t)size) {
        self->realloc(self, buffer, 0);
        return NULL;
    }

    *out_size = (size_t)size;
    return buffer;
}

static void setup_dsdl(void)
{
    dsdl_new(&g_dsdl, test_realloc);
    g_dsdl.read = test_read_file;
}

static void teardown_dsdl(void) { dsdl_destroy(&g_dsdl); }

// ============================================================================
// Type resolution tests
// ============================================================================

static void test_load_simple_type(void)
{
    setup_dsdl();

    // Add namespace root
    TEST_ASSERT_TRUE(
      dsdl_add_namespace(&g_dsdl, wkv_key("test_dsdl_root_namespaces/nunavut_test_types/nested_array_types")));

    // Load Simple.1.0 (no dependencies)
    const dsdl_type_composite_t* simple = dsdl_read(&g_dsdl, wkv_key("mymsgs.Simple.1.0"));
    TEST_ASSERT_NOT_NULL(simple);

    // Check basic properties
    TEST_ASSERT_EQUAL_UINT8(1, simple->version[0]);
    TEST_ASSERT_EQUAL_UINT8(0, simple->version[1]);
    TEST_ASSERT_TRUE(simple->sealed);
    TEST_ASSERT_EQUAL(DSDL_COMPOSITE_STRUCT, simple->type);

    // Simple.1.0 has 3 fields: int32 a, float16 b, bool c
    TEST_ASSERT_EQUAL_size_t(3, simple->field_count);

    teardown_dsdl();
}

static void test_load_type_with_dependency(void)
{
    setup_dsdl();

    // Add namespace root
    TEST_ASSERT_TRUE(
      dsdl_add_namespace(&g_dsdl, wkv_key("test_dsdl_root_namespaces/nunavut_test_types/nested_array_types")));

    // Load Outer.1.0 which references Inner.1.0
    const dsdl_type_composite_t* outer = dsdl_read(&g_dsdl, wkv_key("mymsgs.Outer.1.0"));
    TEST_ASSERT_NOT_NULL(outer);

    // Check that Inner.1.0 was also loaded (cached)
    const dsdl_type_composite_t* inner = dsdl_read(&g_dsdl, wkv_key("mymsgs.Inner.1.0"));
    TEST_ASSERT_NOT_NULL(inner);

    // Inner should be the same instance (from cache)
    const dsdl_type_composite_t* inner2 = dsdl_read(&g_dsdl, wkv_key("mymsgs.Inner.1.0"));
    TEST_ASSERT_EQUAL_PTR(inner, inner2);

    teardown_dsdl();
}

static void test_cache_hit(void)
{
    setup_dsdl();

    // Add namespace root
    TEST_ASSERT_TRUE(
      dsdl_add_namespace(&g_dsdl, wkv_key("test_dsdl_root_namespaces/nunavut_test_types/nested_array_types")));

    // Load same type twice
    const dsdl_type_composite_t* type1 = dsdl_read(&g_dsdl, wkv_key("mymsgs.Inner.1.0"));
    TEST_ASSERT_NOT_NULL(type1);

    const dsdl_type_composite_t* type2 = dsdl_read(&g_dsdl, wkv_key("mymsgs.Inner.1.0"));
    TEST_ASSERT_NOT_NULL(type2);

    // Should return same instance
    TEST_ASSERT_EQUAL_PTR(type1, type2);

    teardown_dsdl();
}

static void test_type_not_found(void)
{
    setup_dsdl();

    // Add namespace root
    TEST_ASSERT_TRUE(
      dsdl_add_namespace(&g_dsdl, wkv_key("test_dsdl_root_namespaces/nunavut_test_types/nested_array_types")));

    // Try to load non-existent type
    const dsdl_type_composite_t* type = dsdl_read(&g_dsdl, wkv_key("mymsgs.DoesNotExist.1.0"));
    TEST_ASSERT_NULL(type);

    teardown_dsdl();
}

static void test_serialized_footprint_simple(void)
{
    setup_dsdl();

    TEST_ASSERT_TRUE(
      dsdl_add_namespace(&g_dsdl, wkv_key("test_dsdl_root_namespaces/nunavut_test_types/nested_array_types")));

    // Simple.1.0: int32 a, float16 b, bool c
    // = 32 + 16 + 1 = 49 bits = 7 bytes (byte-aligned)
    const dsdl_type_composite_t* simple = dsdl_read(&g_dsdl, wkv_key("mymsgs.Simple.1.0"));
    TEST_ASSERT_NOT_NULL(simple);
    TEST_ASSERT_EQUAL_size_t(7, dsdl_serialized_footprint(simple));

    teardown_dsdl();
}

static void test_serialized_footprint_array(void)
{
    setup_dsdl();

    TEST_ASSERT_TRUE(
      dsdl_add_namespace(&g_dsdl, wkv_key("test_dsdl_root_namespaces/nunavut_test_types/nested_array_types")));

    // Inner.1.0: uint32[<=5] inner_items
    // Length prefix: ceil(log2(5+1)) = ceil(log2(6)) = 3 bits
    // Elements: 5 * 32 = 160 bits
    // Total: 3 + 160 = 163 bits = 21 bytes
    const dsdl_type_composite_t* inner = dsdl_read(&g_dsdl, wkv_key("mymsgs.Inner.1.0"));
    TEST_ASSERT_NOT_NULL(inner);
    TEST_ASSERT_EQUAL_size_t(21, dsdl_serialized_footprint(inner));

    teardown_dsdl();
}

static void test_serialized_footprint_nested(void)
{
    setup_dsdl();

    TEST_ASSERT_TRUE(
      dsdl_add_namespace(&g_dsdl, wkv_key("test_dsdl_root_namespaces/nunavut_test_types/nested_array_types")));

    // Outer.1.0: float32[<=8] outer_items, Inner.1.0 inner
    // float32[<=8]: ceil(log2(9))=4 prefix bits + 8*32=256 element bits = 260 bits
    // Inner.1.0: 163 bits (from above)
    // Total: 260 + 163 = 423 bits = 53 bytes
    const dsdl_type_composite_t* outer = dsdl_read(&g_dsdl, wkv_key("mymsgs.Outer.1.0"));
    TEST_ASSERT_NOT_NULL(outer);
    TEST_ASSERT_EQUAL_size_t(53, dsdl_serialized_footprint(outer));

    teardown_dsdl();
}

// ============================================================================
// Main
// ============================================================================

void setUp(void) {}
void tearDown(void) {}

int main(void)
{
    UNITY_BEGIN();

    RUN_TEST(test_load_simple_type);
    RUN_TEST(test_load_type_with_dependency);
    RUN_TEST(test_cache_hit);
    RUN_TEST(test_type_not_found);
    RUN_TEST(test_serialized_footprint_simple);
    RUN_TEST(test_serialized_footprint_array);
    RUN_TEST(test_serialized_footprint_nested);

    return UNITY_END();
}
