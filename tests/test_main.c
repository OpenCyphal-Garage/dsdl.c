/// Test runner for dsdl.c public API test suite
///
/// Uses ThrowTheSwitch Unity framework.
/// This executable tests public API functions only.
/// Internal function tests are in separate executables.

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "dsdl.h"
#include "unity.h"

// ============================================================================
// Test helpers
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

#ifndef DSDL_TEST_ROOT
#define DSDL_TEST_ROOT "."
#endif

static bool add_namespace_rel(dsdl_t* const dsdl, const char* const rel_path)
{
    if ((dsdl == NULL) || (rel_path == NULL)) {
        return false;
    }
    char      path_buf[512];
    const int len = snprintf(path_buf, sizeof(path_buf), "%s/%s", DSDL_TEST_ROOT, rel_path);
    if ((len < 0) || ((size_t)len >= sizeof(path_buf))) {
        return false;
    }
    return dsdl_add_namespace(dsdl, wkv_key(path_buf));
}

static bool add_test_roots(dsdl_t* const dsdl)
{
    return add_namespace_rel(dsdl, "test_dsdl_root_namespaces/0") &&
           add_namespace_rel(dsdl, "test_dsdl_root_namespaces/1");
}

// File reader callback (from test_error_paths.c)
static wkv_str_t test_read_file(dsdl_t* self, wkv_str_t path)
{
    wkv_str_t result = { 0, NULL };

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

    (void)fseek(f, 0, SEEK_END);
    const long size = ftell(f);
    (void)fseek(f, 0, SEEK_SET);

    if (size < 0) {
        (void)fclose(f);
        return result;
    }

    char* buffer = (char*)self->realloc(self, NULL, (size_t)size);
    if (buffer == NULL) {
        (void)fclose(f);
        return result;
    }

    const size_t read_count = fread(buffer, 1, (size_t)size, f);
    (void)fclose(f);

    if (read_count != (size_t)size) {
        self->realloc(self, buffer, 0);
        return result;
    }

    result.len = (size_t)size;
    result.str = buffer;
    return result;
}

// Directory listing callback (from test_error_paths.c)
static wkv_str_t* test_list_dir(dsdl_t* self, wkv_str_t path)
{
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

    size_t         count = 0;
    struct dirent* entry = NULL;
    while ((entry = readdir(dir)) != NULL) {
        if ((strcmp(entry->d_name, ".") == 0) || (strcmp(entry->d_name, "..") == 0)) {
            continue;
        }
        count++;
    }

    wkv_str_t* result = (wkv_str_t*)self->realloc(self, NULL, (count + 1) * sizeof(wkv_str_t));
    if (result == NULL) {
        closedir(dir);
        return NULL;
    }

    rewinddir(dir);
    size_t idx = 0;
    while ((entry = readdir(dir)) != NULL) {
        if ((strcmp(entry->d_name, ".") == 0) || (strcmp(entry->d_name, "..") == 0)) {
            continue;
        }
        const size_t name_len  = strlen(entry->d_name);
        char*        name_copy = (char*)self->realloc(self, NULL, name_len);
        if (name_copy == NULL) {
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

    result[idx].len = 0;
    result[idx].str = NULL;

    closedir(dir);
    return result;
}

// Global DSDL state for tests
static dsdl_t g_dsdl;

static void setup_dsdl(void) { dsdl_new(&g_dsdl, test_realloc, test_read_file, test_list_dir); }

static void teardown_dsdl(void) { dsdl_destroy(&g_dsdl); }

// ============================================================================
// Public API tests
// ============================================================================

void test_dsdl_new_destroy(void)
{
    dsdl_t dsdl;
    dsdl_new(&dsdl, test_realloc, NULL, NULL);
    TEST_ASSERT_NOT_NULL(dsdl.realloc);
    dsdl_destroy(&dsdl);
    TEST_PASS();
}

void test_dsdl_add_namespace(void)
{
    dsdl_t dsdl;
    dsdl_new(&dsdl, test_realloc, test_read_file, test_list_dir);

    bool result = add_test_roots(&dsdl);
    TEST_ASSERT_TRUE(result);

    dsdl_destroy(&dsdl);
}

void test_dsdl_read_exact_version(void)
{
    setup_dsdl();
    TEST_ASSERT_TRUE(add_test_roots(&g_dsdl));

    const dsdl_type_composite_t* simple = dsdl_read(&g_dsdl, wkv_key("mymsgs.Simple.1.0"));
    TEST_ASSERT_NOT_NULL(simple);
    TEST_ASSERT_EQUAL_UINT8(1, simple->version[0]);
    TEST_ASSERT_EQUAL_UINT8(0, simple->version[1]);

    teardown_dsdl();
}

void test_dsdl_read_partial_version(void)
{
    setup_dsdl();
    TEST_ASSERT_TRUE(add_test_roots(&g_dsdl));

    const dsdl_type_composite_t* simple = dsdl_read(&g_dsdl, wkv_key("mymsgs.Simple.1"));
    TEST_ASSERT_NOT_NULL(simple);
    TEST_ASSERT_EQUAL_UINT8(1, simple->version[0]);

    teardown_dsdl();
}

void test_dsdl_read_no_version(void)
{
    setup_dsdl();
    TEST_ASSERT_TRUE(add_test_roots(&g_dsdl));

    const dsdl_type_composite_t* simple = dsdl_read(&g_dsdl, wkv_key("mymsgs.Simple"));
    TEST_ASSERT_NOT_NULL(simple);

    teardown_dsdl();
}

void test_dsdl_serialized_footprint_primitive_uint8(void)
{
    setup_dsdl();
    TEST_ASSERT_TRUE(add_test_roots(&g_dsdl));

    const dsdl_type_composite_t* simple = dsdl_read(&g_dsdl, wkv_key("mymsgs.Simple.1.0"));
    TEST_ASSERT_NOT_NULL(simple);

    // Simple.1.0: int32 a (4 bytes) + float16 b (2 bytes) + bool c (1 bit, padded to 1 byte) = 7 bytes
    // Actually: 32 + 16 + 1 = 49 bits = 7 bytes (ceil(49/8))
    TEST_ASSERT_EQUAL_size_t(7, (size_t)dsdl_serialized_footprint(simple));

    teardown_dsdl();
}

void test_dsdl_serialized_footprint_array(void)
{
    setup_dsdl();
    TEST_ASSERT_TRUE(add_test_roots(&g_dsdl));

    const dsdl_type_composite_t* inner = dsdl_read(&g_dsdl, wkv_key("mymsgs.Inner.1.0"));
    TEST_ASSERT_NOT_NULL(inner);

    // Inner.1.0: uint32[<=5] items
    // Max size: 8-bit count + 5 * 32 bits = 8 + 160 = 168 bits = 21 bytes
    TEST_ASSERT_EQUAL_size_t(21, (size_t)dsdl_serialized_footprint(inner));

    teardown_dsdl();
}

void test_dsdl_serialize_with_error_param(void)
{
    setup_dsdl();
    TEST_ASSERT_TRUE(add_test_roots(&g_dsdl));

    const dsdl_type_composite_t* simple = dsdl_read(&g_dsdl, wkv_key("mymsgs.Simple.1.0"));
    TEST_ASSERT_NOT_NULL(simple);

    int_least32_t       a        = 42;
    float               b        = 1.0F;
    bool                c        = true;
    void*               fields[] = { &a, &b, &c };
    dsdl_value_struct_t val      = { .values = fields };

    uint8_t      buffer[256];
    dsdl_error_t err  = dsdl_error_none;
    size_t       size = dsdl_serialize(simple, &val, sizeof(buffer), buffer, &err);

    TEST_ASSERT_NOT_EQUAL_size_t(SIZE_MAX, size);
    TEST_ASSERT_EQUAL(dsdl_error_none, err);

    teardown_dsdl();
}

void test_dsdl_serialize_buffer_too_small(void)
{
    setup_dsdl();
    TEST_ASSERT_TRUE(add_test_roots(&g_dsdl));

    const dsdl_type_composite_t* simple = dsdl_read(&g_dsdl, wkv_key("mymsgs.Simple.1.0"));
    TEST_ASSERT_NOT_NULL(simple);

    int_least32_t       a        = 42;
    float               b        = 1.0F;
    bool                c        = true;
    void*               fields[] = { &a, &b, &c };
    dsdl_value_struct_t val      = { .values = fields };

    uint8_t      tiny_buffer[4];
    dsdl_error_t err  = dsdl_error_none;
    size_t       size = dsdl_serialize(simple, &val, sizeof(tiny_buffer), tiny_buffer, &err);

    TEST_ASSERT_EQUAL_size_t(SIZE_MAX, size);
    TEST_ASSERT_NOT_EQUAL(dsdl_error_none, err);

    teardown_dsdl();
}

void test_dsdl_deserialize_with_error_param(void)
{
    setup_dsdl();
    TEST_ASSERT_TRUE(add_test_roots(&g_dsdl));

    const dsdl_type_composite_t* simple = dsdl_read(&g_dsdl, wkv_key("mymsgs.Simple.1.0"));
    TEST_ASSERT_NOT_NULL(simple);

    int_least32_t       a        = 42;
    float               b        = 1.0F;
    bool                c        = true;
    void*               fields[] = { &a, &b, &c };
    dsdl_value_struct_t val      = { .values = fields };

    uint8_t buffer[256];
    size_t  serialized = dsdl_serialize(simple, &val, sizeof(buffer), buffer, NULL);
    TEST_ASSERT_NOT_EQUAL_size_t(SIZE_MAX, serialized);

    int_least32_t       a_out        = 0;
    float               b_out        = 0.0F;
    bool                c_out        = false;
    void*               fields_out[] = { &a_out, &b_out, &c_out };
    dsdl_value_struct_t val_out      = { .values = fields_out };

    dsdl_error_t err      = dsdl_error_none;
    size_t       consumed = dsdl_deserialize(simple, &val_out, serialized, buffer, &err);

    TEST_ASSERT_NOT_EQUAL_size_t(SIZE_MAX, consumed);
    TEST_ASSERT_EQUAL(dsdl_error_none, err);
    TEST_ASSERT_EQUAL_INT32(42, a_out);

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

    RUN_TEST(test_dsdl_new_destroy);
    RUN_TEST(test_dsdl_add_namespace);
    RUN_TEST(test_dsdl_read_exact_version);
    RUN_TEST(test_dsdl_read_partial_version);
    RUN_TEST(test_dsdl_read_no_version);
    RUN_TEST(test_dsdl_serialized_footprint_primitive_uint8);
    RUN_TEST(test_dsdl_serialized_footprint_array);
    RUN_TEST(test_dsdl_serialize_with_error_param);
    RUN_TEST(test_dsdl_serialize_buffer_too_small);
    RUN_TEST(test_dsdl_deserialize_with_error_param);

    return UNITY_END();
}
