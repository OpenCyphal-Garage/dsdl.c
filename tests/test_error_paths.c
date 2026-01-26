/// Tests for DSDL error handling paths
///
/// Tests each dsdl_error_t value to ensure proper error reporting.

// Include dsdl.c for access to internal functions
#include "dsdl.c"

#include "unity.h"

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// ============================================================================
// Test helpers
// ============================================================================

static dsdl_t g_dsdl;

#ifndef DSDL_TEST_ROOT
#define DSDL_TEST_ROOT "."
#endif

// Standard realloc callback
static void* test_realloc(dsdl_t* self, void* ptr, size_t new_size)
{
    (void)self;
    if (new_size == 0) {
        free(ptr);
        return NULL;
    }
    return realloc(ptr, new_size);
}

// OOM simulation - fails after g_oom_counter allocations
static int   g_oom_counter = 0;
static void* oom_realloc(dsdl_t* self, void* ptr, size_t size)
{
    (void)self;
    if (size == 0) {
        free(ptr);
        return NULL;
    }
    if (--g_oom_counter <= 0) {
        return NULL;
    }
    return realloc(ptr, size);
}

// File reader callback (same as test_serialization.c)
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

    fseek(f, 0, SEEK_END);
    const long size = ftell(f);
    fseek(f, 0, SEEK_SET);

    if (size < 0) {
        fclose(f);
        return result;
    }

    char* buffer = (char*)self->realloc(self, NULL, (size_t)size);
    if (buffer == NULL) {
        fclose(f);
        return result;
    }

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

// Directory listing callback
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

static void setup_dsdl(void) { dsdl_new(&g_dsdl, test_realloc, test_read_file, test_list_dir); }

static void setup_dsdl_with_oom(int fail_after)
{
    g_oom_counter = fail_after;
    dsdl_new(&g_dsdl, oom_realloc, test_read_file, test_list_dir);
}

static void teardown_dsdl(void) { dsdl_destroy(&g_dsdl); }

static bool add_test_roots(void)
{
    char      path0[512];
    char      path1[512];
    const int ret0 = snprintf(path0, sizeof(path0), "%s/test_dsdl_root_namespaces/0", DSDL_TEST_ROOT);
    const int ret1 = snprintf(path1, sizeof(path1), "%s/test_dsdl_root_namespaces/1", DSDL_TEST_ROOT);
    if ((ret0 < 0) || (ret0 >= (int)sizeof(path0)) || (ret1 < 0) || (ret1 >= (int)sizeof(path1))) {
        return false;
    }
    return dsdl_add_namespace(&g_dsdl, wkv_key(path0)) && dsdl_add_namespace(&g_dsdl, wkv_key(path1));
}

// ============================================================================
// Error Path Tests
// ============================================================================

void test_error_file_not_found(void)
{
    setup_dsdl();
    TEST_ASSERT_TRUE(add_test_roots());

    const dsdl_type_composite_t* result = dsdl_read(&g_dsdl, wkv_key("nonexistent.Type.1.0"));
    TEST_ASSERT_NULL(result);
    TEST_ASSERT_EQUAL(dsdl_error_file_not_found, g_dsdl.error);

    teardown_dsdl();
}

void test_error_oom_during_read(void)
{
    setup_dsdl_with_oom(20); // Allow some allocations, then fail
    TEST_ASSERT_TRUE(add_test_roots());

    const dsdl_type_composite_t* result = dsdl_read(&g_dsdl, wkv_key("mymsgs.Simple.1.0"));
    TEST_ASSERT_NULL(result);
    TEST_ASSERT_EQUAL(dsdl_error_out_of_memory, g_dsdl.error);

    teardown_dsdl();
}

void test_error_buffer_too_small(void)
{
    setup_dsdl();
    TEST_ASSERT_TRUE(add_test_roots());

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

void test_error_array_capacity_exceeded(void)
{
    setup_dsdl();
    TEST_ASSERT_TRUE(add_test_roots());

    const dsdl_type_composite_t* inner = dsdl_read(&g_dsdl, wkv_key("mymsgs.Inner.1.0"));
    TEST_ASSERT_NOT_NULL(inner);

    uint_least32_t              elements[6] = { 1, 2, 3, 4, 5, 6 };
    dsdl_value_array_variable_t arr         = { .count = 6, .members = elements };
    void*                       fields[]    = { &arr };
    dsdl_value_struct_t         val         = { .values = fields };

    uint8_t      buffer[64];
    dsdl_error_t err  = dsdl_error_none;
    size_t       size = dsdl_serialize(inner, &val, sizeof(buffer), buffer, &err);
    TEST_ASSERT_EQUAL_size_t(SIZE_MAX, size);
    TEST_ASSERT_NOT_EQUAL(dsdl_error_none, err);

    teardown_dsdl();
}

void test_error_union_tag_invalid(void)
{
    static dsdl_type_t  field_a  = DSDL_UINT(8);
    static dsdl_type_t  field_b  = DSDL_UINT(16);
    static dsdl_type_t* types[2] = { &field_a, &field_b };
    static wkv_str_t    names[2] = { { 1, "a" }, { 1, "b" } };

    dsdl_type_composite_t union_type = {
        .type        = DSDL_COMPOSITE_UNION,
        .name        = { 9, "TestUnion" },
        .version     = { 1, 0 },
        .extent      = 8,
        .sealed      = true,
        .field_count = 2,
        .field_names = names,
        .field_types = types,
    };

    uint_least32_t     value = 42;
    dsdl_value_union_t uval  = { .tag = 5, .value = &value };

    uint8_t      buffer[8];
    dsdl_error_t err  = dsdl_error_none;
    size_t       size = dsdl_serialize(&union_type, &uval, sizeof(buffer), buffer, &err);
    TEST_ASSERT_EQUAL_size_t(SIZE_MAX, size);
    TEST_ASSERT_NOT_EQUAL(dsdl_error_none, err);
}

void test_error_parse_invalid_syntax(void)
{
    setup_dsdl();

    // Create a temporary invalid DSDL file
    char      temp_path[512];
    const int ret = snprintf(temp_path, sizeof(temp_path), "%s/test_dsdl_root_namespaces/0/mymsgs", DSDL_TEST_ROOT);
    TEST_ASSERT_TRUE((ret > 0) && (ret < (int)sizeof(temp_path)));

    char      temp_file[512];
    const int ret2 = snprintf(temp_file, sizeof(temp_file), "%s/InvalidSyntax.1.0.dsdl", temp_path);
    TEST_ASSERT_TRUE((ret2 > 0) && (ret2 < (int)sizeof(temp_file)));

    FILE* f = fopen(temp_file, "w");
    TEST_ASSERT_NOT_NULL(f);
    fprintf(f, "this is not valid DSDL syntax @#$%%\n");
    fclose(f);

    TEST_ASSERT_TRUE(add_test_roots());

    const dsdl_type_composite_t* result = dsdl_read(&g_dsdl, wkv_key("mymsgs.InvalidSyntax.1.0"));
    TEST_ASSERT_NULL(result);
    TEST_ASSERT_EQUAL(dsdl_error_parse, g_dsdl.error);

    // Cleanup
    remove(temp_file);
    teardown_dsdl();
}

void test_error_semantic_undefined_type(void)
{
    setup_dsdl();
    TEST_ASSERT_TRUE(add_test_roots());

    const dsdl_type_composite_t* result = dsdl_read(&g_dsdl, wkv_key("mymsgs.WithUndefinedRef.1.0"));
    TEST_ASSERT_NULL(result);
    TEST_ASSERT_TRUE((g_dsdl.error == dsdl_error_file_not_found) || (g_dsdl.error == dsdl_error_semantic));

    teardown_dsdl();
}

void test_error_representation_null_value(void)
{
    setup_dsdl();
    TEST_ASSERT_TRUE(add_test_roots());

    const dsdl_type_composite_t* type = dsdl_read(&g_dsdl, wkv_key("mymsgs.Simple.1.0"));
    TEST_ASSERT_NOT_NULL(type);

    uint8_t      buffer[64];
    dsdl_error_t err  = dsdl_error_none;
    size_t       size = dsdl_serialize(type, NULL, sizeof(buffer), buffer, &err);
    TEST_ASSERT_EQUAL_size_t(SIZE_MAX, size);
    TEST_ASSERT_EQUAL(dsdl_error_representation, err);

    teardown_dsdl();
}

void test_error_oom_add_namespace(void)
{
    g_oom_counter = 1;
    dsdl_new(&g_dsdl, oom_realloc, test_read_file, test_list_dir);

    bool result = dsdl_add_namespace(&g_dsdl, wkv_key("test_dsdl_root_namespaces/0"));
    TEST_ASSERT_FALSE(result);
    TEST_ASSERT_EQUAL(dsdl_error_out_of_memory, g_dsdl.error);

    dsdl_destroy(&g_dsdl);
}

void test_error_deserialize_truncated(void)
{
    setup_dsdl();
    TEST_ASSERT_TRUE(add_test_roots());

    const dsdl_type_composite_t* type = dsdl_read(&g_dsdl, wkv_key("mymsgs.Simple.1.0"));
    TEST_ASSERT_NOT_NULL(type);

    uint8_t             buffer[1] = { 0 };
    uint_least32_t      a         = 0;
    float               b         = 0.0F;
    uint_least8_t       c         = 0;
    void*               fields[]  = { &a, &b, &c };
    dsdl_value_struct_t value     = { .values = fields };

    dsdl_error_t err  = dsdl_error_none;
    size_t       size = dsdl_deserialize(type, &value, sizeof(buffer), buffer, &err);
    TEST_ASSERT_EQUAL_size_t(7, size);
    TEST_ASSERT_EQUAL(dsdl_error_none, err);

    teardown_dsdl();
}

// ============================================================================
// Unity test runner
// ============================================================================

void setUp(void) {}

void tearDown(void) {}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_error_file_not_found);
    RUN_TEST(test_error_oom_during_read);
    RUN_TEST(test_error_buffer_too_small);
    RUN_TEST(test_error_array_capacity_exceeded);
    RUN_TEST(test_error_union_tag_invalid);
    RUN_TEST(test_error_parse_invalid_syntax);
    RUN_TEST(test_error_semantic_undefined_type);
    RUN_TEST(test_error_representation_null_value);
    RUN_TEST(test_error_oom_add_namespace);
    RUN_TEST(test_error_deserialize_truncated);
    return UNITY_END();
}
