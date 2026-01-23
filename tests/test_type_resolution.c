/// Test type resolution and recursive loading
///
/// Tests for dsdl_read() and dependency resolution.

#include "unity.h"

#include <stdlib.h>
#include <string.h>

// Include the implementation directly for internal access
#include "dsdl.c"

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
    const char* root = "test_dsdl_root_namespaces/nunavut_test_types/nested_array_types";
    TEST_ASSERT_TRUE(dsdl_add_namespace(&g_dsdl, (wkv_str_t){ strlen(root), root }));

    // Load Simple.1.0 (no dependencies)
    const dsdl_composite_t* simple = dsdl_read(&g_dsdl, (wkv_str_t){ 17, "mymsgs.Simple.1.0" });
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
    const char* root = "test_dsdl_root_namespaces/nunavut_test_types/nested_array_types";
    TEST_ASSERT_TRUE(dsdl_add_namespace(&g_dsdl, (wkv_str_t){ strlen(root), root }));

    // Load Outer.1.0 which references Inner.1.0
    const dsdl_composite_t* outer = dsdl_read(&g_dsdl, (wkv_str_t){ 16, "mymsgs.Outer.1.0" });
    TEST_ASSERT_NOT_NULL(outer);

    // Check that Inner.1.0 was also loaded (cached)
    const dsdl_composite_t* inner = dsdl_read(&g_dsdl, (wkv_str_t){ 16, "mymsgs.Inner.1.0" });
    TEST_ASSERT_NOT_NULL(inner);

    // Inner should be the same instance (from cache)
    const dsdl_composite_t* inner2 = dsdl_read(&g_dsdl, (wkv_str_t){ 16, "mymsgs.Inner.1.0" });
    TEST_ASSERT_EQUAL_PTR(inner, inner2);

    teardown_dsdl();
}

static void test_cache_hit(void)
{
    setup_dsdl();

    // Add namespace root
    const char* root = "test_dsdl_root_namespaces/nunavut_test_types/nested_array_types";
    TEST_ASSERT_TRUE(dsdl_add_namespace(&g_dsdl, (wkv_str_t){ strlen(root), root }));

    // Load same type twice
    const dsdl_composite_t* type1 = dsdl_read(&g_dsdl, (wkv_str_t){ 16, "mymsgs.Inner.1.0" });
    TEST_ASSERT_NOT_NULL(type1);

    const dsdl_composite_t* type2 = dsdl_read(&g_dsdl, (wkv_str_t){ 16, "mymsgs.Inner.1.0" });
    TEST_ASSERT_NOT_NULL(type2);

    // Should return same instance
    TEST_ASSERT_EQUAL_PTR(type1, type2);

    teardown_dsdl();
}

static void test_type_not_found(void)
{
    setup_dsdl();

    // Add namespace root
    const char* root = "test_dsdl_root_namespaces/nunavut_test_types/nested_array_types";
    TEST_ASSERT_TRUE(dsdl_add_namespace(&g_dsdl, (wkv_str_t){ strlen(root), root }));

    // Try to load non-existent type
    const dsdl_composite_t* type = dsdl_read(&g_dsdl, (wkv_str_t){ 23, "mymsgs.DoesNotExist.1.0" });
    TEST_ASSERT_NULL(type);

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

    return UNITY_END();
}
