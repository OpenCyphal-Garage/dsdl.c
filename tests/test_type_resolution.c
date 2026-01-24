/// Test type resolution and recursive loading
///
/// Tests for dsdl_read() and dependency resolution.

// Include the implementation directly for internal access
#include "dsdl.c"

#include "unity.h"

#include <dirent.h>
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

/// Standard file reader for tests - returns wkv_str_t
static wkv_str_t test_read_file(dsdl_t* self, wkv_str_t path)
{
    wkv_str_t result = { 0, NULL };

    // Null-terminate path
    char path_buf[512];
    if (path.len >= sizeof(path_buf)) {
        return result;
    }
    memcpy(path_buf, path.str, path.len);
    path_buf[path.len] = '\0';

    FILE* f = fopen(path_buf, "rb");
    if (f == NULL) {
        return result;
    }

    // Get file size
    fseek(f, 0, SEEK_END);
    const long size = ftell(f);
    fseek(f, 0, SEEK_SET);

    if (size < 0) {
        fclose(f);
        return result;
    }

    // Allocate buffer
    char* buffer = (char*)self->realloc(self, NULL, (size_t)size);
    if (buffer == NULL) {
        fclose(f);
        return result;
    }

    // Read file
    const size_t read_count = fread(buffer, 1, (size_t)size, f);
    fclose(f);

    if (read_count != (size_t)size) {
        self->realloc(self, buffer, 0);
        return result;
    }

    result.len = (size_t)size;
    result.str = buffer;
    return result;
}

/// Standard directory lister for tests
static wkv_str_t* test_list_dir(dsdl_t* self, wkv_str_t path)
{
    // Null-terminate path
    char path_buf[512];
    if (path.len >= sizeof(path_buf)) {
        return NULL;
    }
    memcpy(path_buf, path.str, path.len);
    path_buf[path.len] = '\0';

    DIR* dir = opendir(path_buf);
    if (dir == NULL) {
        return NULL;
    }

    // First pass: count entries
    size_t         count = 0;
    struct dirent* entry;
    while ((entry = readdir(dir)) != NULL) {
        // Skip . and ..
        if ((strcmp(entry->d_name, ".") == 0) || (strcmp(entry->d_name, "..") == 0)) {
            continue;
        }
        count++;
    }

    // Allocate array (count + 1 for NULL terminator)
    wkv_str_t* result = (wkv_str_t*)self->realloc(self, NULL, (count + 1) * sizeof(wkv_str_t));
    if (result == NULL) {
        closedir(dir);
        return NULL;
    }

    // Second pass: copy entries
    rewinddir(dir);
    size_t idx = 0;
    while ((entry = readdir(dir)) != NULL) {
        if ((strcmp(entry->d_name, ".") == 0) || (strcmp(entry->d_name, "..") == 0)) {
            continue;
        }
        const size_t name_len  = strlen(entry->d_name);
        char*        name_copy = (char*)self->realloc(self, NULL, name_len);
        if (name_copy == NULL) {
            // Cleanup on OOM
            for (size_t i = 0; i < idx; i++) {
                self->realloc(self, (void*)result[i].str, 0);
            }
            self->realloc(self, result, 0);
            closedir(dir);
            return NULL;
        }
        memcpy(name_copy, entry->d_name, name_len);
        result[idx].len = name_len;
        result[idx].str = name_copy;
        idx++;
    }

    // NULL terminator
    result[idx].len = 0;
    result[idx].str = NULL;

    closedir(dir);
    return result;
}

static void setup_dsdl(void)
{
    dsdl_new(&g_dsdl, test_realloc);
    g_dsdl.read = test_read_file;
    g_dsdl.list = test_list_dir;
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

static void test_bit_length_set_simple(void)
{
    setup_dsdl();

    TEST_ASSERT_TRUE(
      dsdl_add_namespace(&g_dsdl, wkv_key("test_dsdl_root_namespaces/nunavut_test_types/nested_array_types")));

    // Simple.1.0: int32 a, float16 b, bool c
    // = 32 + 16 + 1 = 49 bits (fixed)
    dsdl_type_composite_t* simple = (dsdl_type_composite_t*)dsdl_read(&g_dsdl, wkv_key("mymsgs.Simple.1.0"));
    TEST_ASSERT_NOT_NULL(simple);

    // Compute bit length set
    dsdl_bls_t* bls = dsdl_type_bls(&g_dsdl, &simple->type);
    TEST_ASSERT_NOT_NULL(bls);

    // Simple has fixed size, so min == max == 49
    TEST_ASSERT_EQUAL_size_t(49, dsdl_bls_min(bls));
    TEST_ASSERT_EQUAL_size_t(49, dsdl_bls_max(bls));
    TEST_ASSERT_TRUE(dsdl_bls_is_fixed(bls));

    // Should be stored in composite
    TEST_ASSERT_NOT_NULL(simple->bls);

    teardown_dsdl();
}

static void test_bit_length_set_variable_array(void)
{
    setup_dsdl();

    TEST_ASSERT_TRUE(
      dsdl_add_namespace(&g_dsdl, wkv_key("test_dsdl_root_namespaces/nunavut_test_types/nested_array_types")));

    // Inner.1.0: uint32[<=5] inner_items
    // Length prefix: ceil(log2(5+1)) = ceil(log2(6)) = 3 bits
    // Elements: 0..5 * 32 = 0..160 bits
    // Total: min = 3 + 0 = 3 bits, max = 3 + 160 = 163 bits
    dsdl_type_composite_t* inner = (dsdl_type_composite_t*)dsdl_read(&g_dsdl, wkv_key("mymsgs.Inner.1.0"));
    TEST_ASSERT_NOT_NULL(inner);

    // Compute bit length set
    dsdl_bls_t* bls = dsdl_type_bls(&g_dsdl, &inner->type);
    TEST_ASSERT_NOT_NULL(bls);

    // Inner has variable size due to variable array
    TEST_ASSERT_EQUAL_size_t(3, dsdl_bls_min(bls));
    TEST_ASSERT_EQUAL_size_t(163, dsdl_bls_max(bls));
    TEST_ASSERT_FALSE(dsdl_bls_is_fixed(bls));

    teardown_dsdl();
}

// ============================================================================
// Fixed port-ID tests
// ============================================================================

static void test_load_message_with_fixed_port_id(void)
{
    setup_dsdl();

    TEST_ASSERT_TRUE(dsdl_add_namespace(&g_dsdl, wkv_key("test_dsdl_root_namespaces")));

    // Load message type with fixed port-ID in filename: 7000.FixedPortMessage.1.0.dsdl
    const dsdl_type_composite_t* msg = dsdl_read(&g_dsdl, wkv_key("validation.FixedPortMessage.1.0"));
    TEST_ASSERT_NOT_NULL(msg);

    // Check that the fixed port-ID was extracted from the filename
    TEST_ASSERT_EQUAL_UINT16(7000, msg->fixed_port_id);

    // Check version
    TEST_ASSERT_EQUAL_UINT8(1, msg->version[0]);
    TEST_ASSERT_EQUAL_UINT8(0, msg->version[1]);

    teardown_dsdl();
}

static void test_load_service_with_fixed_port_id(void)
{
    setup_dsdl();

    TEST_ASSERT_TRUE(dsdl_add_namespace(&g_dsdl, wkv_key("test_dsdl_root_namespaces")));

    // Load service type with fixed port-ID in filename: 300.FixedPortService.0.1.dsdl
    const dsdl_type_composite_t* svc = dsdl_read(&g_dsdl, wkv_key("validation.FixedPortService.0.1"));
    TEST_ASSERT_NOT_NULL(svc);

    // Check that the fixed port-ID was extracted from the filename
    TEST_ASSERT_EQUAL_UINT16(300, svc->fixed_port_id);

    // Check version
    TEST_ASSERT_EQUAL_UINT8(0, svc->version[0]);
    TEST_ASSERT_EQUAL_UINT8(1, svc->version[1]);

    // Note: Service type handling (RPC vs UNION) is tested separately
    // This test focuses on fixed port-ID extraction from filename

    teardown_dsdl();
}

static void test_load_type_without_fixed_port_id(void)
{
    setup_dsdl();

    TEST_ASSERT_TRUE(dsdl_add_namespace(&g_dsdl, wkv_key("test_dsdl_root_namespaces")));

    // Load a type without fixed port-ID: Empty.0.1.dsdl
    const dsdl_type_composite_t* t = dsdl_read(&g_dsdl, wkv_key("validation.Empty.0.1"));
    TEST_ASSERT_NOT_NULL(t);

    // Check that fixed_port_id is NONE
    TEST_ASSERT_EQUAL_UINT16(DSDL_FIXED_PORT_ID_NONE, t->fixed_port_id);

    teardown_dsdl();
}

// ============================================================================
// Version resolution tests
// ============================================================================

static void test_version_resolution_exact(void)
{
    setup_dsdl();

    TEST_ASSERT_TRUE(dsdl_add_namespace(&g_dsdl, wkv_key("test_dsdl_root_namespaces")));

    // Request exact version 0.1
    const dsdl_type_composite_t* v01 = dsdl_read(&g_dsdl, wkv_key("validation.Versioned.0.1"));
    TEST_ASSERT_NOT_NULL(v01);
    TEST_ASSERT_EQUAL_UINT8(0, v01->version[0]);
    TEST_ASSERT_EQUAL_UINT8(1, v01->version[1]);

    // Request exact version 1.0
    const dsdl_type_composite_t* v10 = dsdl_read(&g_dsdl, wkv_key("validation.Versioned.1.0"));
    TEST_ASSERT_NOT_NULL(v10);
    TEST_ASSERT_EQUAL_UINT8(1, v10->version[0]);
    TEST_ASSERT_EQUAL_UINT8(0, v10->version[1]);

    teardown_dsdl();
}

static void test_version_resolution_major_only(void)
{
    setup_dsdl();

    TEST_ASSERT_TRUE(dsdl_add_namespace(&g_dsdl, wkv_key("test_dsdl_root_namespaces")));

    // Request major version 1 - should find the highest minor for major 1 (1.0 is the only one)
    const dsdl_type_composite_t* v1 = dsdl_read(&g_dsdl, wkv_key("validation.Versioned.1"));
    TEST_ASSERT_NOT_NULL(v1);
    TEST_ASSERT_EQUAL_UINT8(1, v1->version[0]);
    TEST_ASSERT_EQUAL_UINT8(0, v1->version[1]);

    // Request major version 0 - should find 0.1
    const dsdl_type_composite_t* v0 = dsdl_read(&g_dsdl, wkv_key("validation.Versioned.0"));
    TEST_ASSERT_NOT_NULL(v0);
    TEST_ASSERT_EQUAL_UINT8(0, v0->version[0]);
    TEST_ASSERT_EQUAL_UINT8(1, v0->version[1]);

    teardown_dsdl();
}

static void test_version_resolution_no_version(void)
{
    setup_dsdl();

    TEST_ASSERT_TRUE(dsdl_add_namespace(&g_dsdl, wkv_key("test_dsdl_root_namespaces")));

    // Request without version - should find the highest major.minor (255.255)
    const dsdl_type_composite_t* v = dsdl_read(&g_dsdl, wkv_key("validation.Versioned"));
    TEST_ASSERT_NOT_NULL(v);
    TEST_ASSERT_EQUAL_UINT8(255, v->version[0]);
    TEST_ASSERT_EQUAL_UINT8(255, v->version[1]);

    teardown_dsdl();
}

static void test_version_resolution_versioned_v2(void)
{
    setup_dsdl();

    TEST_ASSERT_TRUE(dsdl_add_namespace(&g_dsdl, wkv_key("test_dsdl_root_namespaces")));

    // VersionedV2 has 1.0 and 2.0, so requesting major 2 should give 2.0
    const dsdl_type_composite_t* v2 = dsdl_read(&g_dsdl, wkv_key("validation.VersionedV2.2"));
    TEST_ASSERT_NOT_NULL(v2);
    TEST_ASSERT_EQUAL_UINT8(2, v2->version[0]);
    TEST_ASSERT_EQUAL_UINT8(0, v2->version[1]);

    // Requesting without version should give the highest: 2.0
    const dsdl_type_composite_t* latest = dsdl_read(&g_dsdl, wkv_key("validation.VersionedV2"));
    TEST_ASSERT_NOT_NULL(latest);
    TEST_ASSERT_EQUAL_UINT8(2, latest->version[0]);
    TEST_ASSERT_EQUAL_UINT8(0, latest->version[1]);

    teardown_dsdl();
}

// ============================================================================
// Filename parsing tests (internal function)
// ============================================================================

static void test_parse_filename_basic(void)
{
    // Test parsing a basic filename
    dsdl_parsed_filename_t p = dsdl_parse_filename(wkv_key("Heartbeat.1.0.dsdl"));
    TEST_ASSERT_TRUE(p.valid);
    TEST_ASSERT_EQUAL_size_t(9, p.type_name.len);
    TEST_ASSERT_EQUAL_STRING_LEN("Heartbeat", p.type_name.str, 9);
    TEST_ASSERT_EQUAL_UINT8(1, p.major);
    TEST_ASSERT_EQUAL_UINT8(0, p.minor);
    TEST_ASSERT_EQUAL_UINT16(DSDL_FIXED_PORT_ID_NONE, p.fixed_port_id);
}

static void test_parse_filename_with_port_id(void)
{
    // Test parsing filename with fixed port-ID
    dsdl_parsed_filename_t p = dsdl_parse_filename(wkv_key("7000.FixedPortMessage.1.0.dsdl"));
    TEST_ASSERT_TRUE(p.valid);
    TEST_ASSERT_EQUAL_size_t(16, p.type_name.len);
    TEST_ASSERT_EQUAL_STRING_LEN("FixedPortMessage", p.type_name.str, 16);
    TEST_ASSERT_EQUAL_UINT8(1, p.major);
    TEST_ASSERT_EQUAL_UINT8(0, p.minor);
    TEST_ASSERT_EQUAL_UINT16(7000, p.fixed_port_id);
}

static void test_parse_filename_invalid(void)
{
    // Test various invalid filenames
    dsdl_parsed_filename_t p;

    // Missing .dsdl extension
    p = dsdl_parse_filename(wkv_key("Heartbeat.1.0"));
    TEST_ASSERT_FALSE(p.valid);

    // Too short
    p = dsdl_parse_filename(wkv_key("T.0.0.dsd"));
    TEST_ASSERT_FALSE(p.valid);

    // Type name doesn't start with uppercase
    p = dsdl_parse_filename(wkv_key("heartbeat.1.0.dsdl"));
    TEST_ASSERT_FALSE(p.valid);

    // Non-numeric version
    p = dsdl_parse_filename(wkv_key("Type.a.0.dsdl"));
    TEST_ASSERT_FALSE(p.valid);
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
    RUN_TEST(test_bit_length_set_simple);
    RUN_TEST(test_bit_length_set_variable_array);

    // Fixed port-ID tests
    RUN_TEST(test_load_message_with_fixed_port_id);
    RUN_TEST(test_load_service_with_fixed_port_id);
    RUN_TEST(test_load_type_without_fixed_port_id);

    // Version resolution tests
    RUN_TEST(test_version_resolution_exact);
    RUN_TEST(test_version_resolution_major_only);
    RUN_TEST(test_version_resolution_no_version);
    RUN_TEST(test_version_resolution_versioned_v2);

    // Filename parsing tests
    RUN_TEST(test_parse_filename_basic);
    RUN_TEST(test_parse_filename_with_port_id);
    RUN_TEST(test_parse_filename_invalid);

    return UNITY_END();
}
