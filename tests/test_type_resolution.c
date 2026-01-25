/// Test type resolution and recursive loading
///
/// Tests for dsdl_read() and dependency resolution.

// Include the implementation directly for internal access
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

static bool add_namespace_rel(const char* const rel_path)
{
    if (rel_path == NULL) {
        return false;
    }
    char      path_buf[512];
    const int len = snprintf(path_buf, sizeof(path_buf), "%s/%s", DSDL_TEST_ROOT, rel_path);
    if ((len < 0) || ((size_t)len >= sizeof(path_buf))) {
        return false;
    }
    return dsdl_add_namespace(&g_dsdl, wkv_key(path_buf));
}

static bool add_test_roots(void)
{
    return add_namespace_rel("test_dsdl_root_namespaces/0") &&
           add_namespace_rel("test_dsdl_root_namespaces/1");
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

static const dsdl_value_t* find_constant(const dsdl_type_composite_t* const type, const char* const name)
{
    if ((type == NULL) || (name == NULL)) {
        return NULL;
    }
    const size_t name_len = strlen(name);
    for (size_t i = 0; i < type->constant_count; i++) {
        if ((type->constant_names[i].len == name_len) && (memcmp(type->constant_names[i].str, name, name_len) == 0)) {
            return &type->constant_values[i];
        }
    }
    return NULL;
}

static const dsdl_type_t* find_constant_type(const dsdl_type_composite_t* const type, const char* const name)
{
    if ((type == NULL) || (name == NULL) || (type->constant_types == NULL)) {
        return NULL;
    }
    const size_t name_len = strlen(name);
    for (size_t i = 0; i < type->constant_count; i++) {
        if ((type->constant_names[i].len == name_len) && (memcmp(type->constant_names[i].str, name, name_len) == 0)) {
            return type->constant_types[i];
        }
    }
    return NULL;
}

static const dsdl_type_t* find_field_type(const dsdl_type_composite_t* const type, const char* const name)
{
    if ((type == NULL) || (name == NULL)) {
        return NULL;
    }
    const size_t name_len = strlen(name);
    for (size_t i = 0; i < type->field_count; i++) {
        if ((type->field_names[i].len == name_len) && (memcmp(type->field_names[i].str, name, name_len) == 0)) {
            return type->field_types[i];
        }
    }
    return NULL;
}

// ============================================================================
// Type resolution tests
// ============================================================================

static void test_load_simple_type(void)
{
    setup_dsdl();

    // Add namespace root
    TEST_ASSERT_TRUE(add_test_roots());

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
    TEST_ASSERT_TRUE(add_test_roots());

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
    TEST_ASSERT_TRUE(add_test_roots());

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
    TEST_ASSERT_TRUE(add_test_roots());

    // Try to load non-existent type
    const dsdl_type_composite_t* type = dsdl_read(&g_dsdl, wkv_key("mymsgs.DoesNotExist.1.0"));
    TEST_ASSERT_NULL(type);

    teardown_dsdl();
}

static void test_serialized_footprint_simple(void)
{
    setup_dsdl();

    TEST_ASSERT_TRUE(add_test_roots());

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

    TEST_ASSERT_TRUE(add_test_roots());

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

    TEST_ASSERT_TRUE(add_test_roots());

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

    TEST_ASSERT_TRUE(add_test_roots());

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

    TEST_ASSERT_TRUE(add_test_roots());

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

    TEST_ASSERT_TRUE(add_test_roots());

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

    TEST_ASSERT_TRUE(add_test_roots());

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

    TEST_ASSERT_TRUE(add_test_roots());

    // Load a type without fixed port-ID: Empty.0.1.dsdl
    const dsdl_type_composite_t* t = dsdl_read(&g_dsdl, wkv_key("validation.Empty.0.1"));
    TEST_ASSERT_NOT_NULL(t);

    // Check that fixed_port_id is NONE
    TEST_ASSERT_EQUAL_UINT16(DSDL_FIXED_PORT_ID_NONE, t->fixed_port_id);

    teardown_dsdl();
}

static void test_constants_evaluated(void)
{
    setup_dsdl();

    TEST_ASSERT_TRUE(add_test_roots());

    const dsdl_type_composite_t* expr = dsdl_read(&g_dsdl, wkv_key("validation.Expressions.0.1"));
    TEST_ASSERT_NOT_NULL(expr);

    const dsdl_value_t* val = find_constant(expr, "ADD");
    TEST_ASSERT_NOT_NULL(val);
    TEST_ASSERT_EQUAL(dsdl_value_rational, val->kind);
    assert_intmax_eq(300, val->as.rational.num);

    val = find_constant(expr, "POW");
    TEST_ASSERT_NOT_NULL(val);
    TEST_ASSERT_EQUAL(dsdl_value_rational, val->kind);
    assert_intmax_eq(1024, val->as.rational.num);

    val = find_constant(expr, "BIT_OR");
    TEST_ASSERT_NOT_NULL(val);
    TEST_ASSERT_EQUAL(dsdl_value_rational, val->kind);
    assert_intmax_eq(255, val->as.rational.num);

    const dsdl_type_composite_t* str = dsdl_read(&g_dsdl, wkv_key("validation.StringOps.0.1"));
    TEST_ASSERT_NOT_NULL(str);

    val = find_constant(str, "CHAR_A");
    TEST_ASSERT_NOT_NULL(val);
    TEST_ASSERT_EQUAL(dsdl_value_rational, val->kind);
    assert_intmax_eq(65, val->as.rational.num);

    teardown_dsdl();
}

static void test_constant_types_exposed(void)
{
    setup_dsdl();

    TEST_ASSERT_TRUE(add_test_roots());

    const dsdl_type_composite_t* type = dsdl_read(&g_dsdl, wkv_key("validation.Literals.0.1"));
    TEST_ASSERT_NOT_NULL(type);
    TEST_ASSERT_NOT_NULL(type->constant_types);

    const dsdl_type_t* ctype = find_constant_type(type, "BOOL_TRUE");
    TEST_ASSERT_NOT_NULL(ctype);
    TEST_ASSERT_EQUAL(DSDL_BOOL, *ctype);

    ctype = find_constant_type(type, "DECIMAL");
    TEST_ASSERT_NOT_NULL(ctype);
    TEST_ASSERT_EQUAL(DSDL_UINT(32), *ctype);

    ctype = find_constant_type(type, "REAL_POINT");
    TEST_ASSERT_NOT_NULL(ctype);
    TEST_ASSERT_EQUAL(DSDL_FLOAT(64), *ctype);

    ctype = find_constant_type(type, "CHAR_UPPER_A");
    TEST_ASSERT_NOT_NULL(ctype);
    TEST_ASSERT_EQUAL(DSDL_UINT(8), *ctype);

    teardown_dsdl();
}

static void test_type_constant_and_attribute_access(void)
{
    setup_dsdl();

    TEST_ASSERT_TRUE(add_test_roots());

    const dsdl_type_composite_t* type = dsdl_read(&g_dsdl, wkv_key("validation.ComplexRefExpr.0.1"));
    TEST_ASSERT_NOT_NULL(type);

    const dsdl_type_t* field_type = find_field_type(type, "expr_capacity_array");
    TEST_ASSERT_NOT_NULL(field_type);
    TEST_ASSERT_TRUE(dsdl_type_is_array(*field_type));

    const dsdl_type_array_t* arr = (const dsdl_type_array_t*)field_type;
    TEST_ASSERT_EQUAL(DSDL_ARRAY_FIXED, arr->type);
    assert_uintmax_eq(6U, arr->capacity);
    TEST_ASSERT_EQUAL(DSDL_UINT(8), *arr->member_type);

    teardown_dsdl();
}

static void test_type_attributes_in_asserts(void)
{
    setup_dsdl();

    TEST_ASSERT_TRUE(add_test_roots());

    const dsdl_type_composite_t* type = dsdl_read(&g_dsdl, wkv_key("validation.TypeAttributes.0.1"));
    TEST_ASSERT_NOT_NULL(type);
    TEST_ASSERT_TRUE(type->sealed);

    teardown_dsdl();
}

static void test_array_capacity_expressions(void)
{
    setup_dsdl();

    TEST_ASSERT_TRUE(add_test_roots());

    const dsdl_type_composite_t* type = dsdl_read(&g_dsdl, wkv_key("validation.ArrayCapExpr.0.1"));
    TEST_ASSERT_NOT_NULL(type);

    const dsdl_type_t* fixed_type = find_field_type(type, "fixed_expr");
    TEST_ASSERT_NOT_NULL(fixed_type);
    TEST_ASSERT_TRUE(dsdl_type_is_array(*fixed_type));
    const dsdl_type_array_t* fixed_arr = (const dsdl_type_array_t*)fixed_type;
    TEST_ASSERT_EQUAL(DSDL_ARRAY_FIXED, fixed_arr->type);
    assert_uintmax_eq(15U, fixed_arr->capacity);

    const dsdl_type_t* var_type = find_field_type(type, "var_excl_expr");
    TEST_ASSERT_NOT_NULL(var_type);
    TEST_ASSERT_TRUE(dsdl_type_is_array(*var_type));
    const dsdl_type_array_t* var_arr = (const dsdl_type_array_t*)var_type;
    TEST_ASSERT_EQUAL(DSDL_ARRAY_VARIABLE, var_arr->type);
    assert_uintmax_eq(123U, var_arr->capacity);

    teardown_dsdl();
}

static void test_service_response_type(void)
{
    setup_dsdl();

    TEST_ASSERT_TRUE(add_test_roots());

    const dsdl_type_composite_t* svc = dsdl_read(&g_dsdl, wkv_key("validation.Service.0.1"));
    TEST_ASSERT_NOT_NULL(svc);
    TEST_ASSERT_NOT_NULL(svc->response);
    TEST_ASSERT_EQUAL_size_t(3, svc->response->field_count);
    TEST_ASSERT_TRUE(svc->response->sealed);

    teardown_dsdl();
}

static void test_deprecated_flag(void)
{
    setup_dsdl();

    TEST_ASSERT_TRUE(add_test_roots());

    const dsdl_type_composite_t* msg = dsdl_read(&g_dsdl, wkv_key("validation.Deprecated.0.1"));
    TEST_ASSERT_NOT_NULL(msg);
    TEST_ASSERT_TRUE(msg->deprecated);

    const dsdl_type_composite_t* svc = dsdl_read(&g_dsdl, wkv_key("validation.DeprecatedService.0.1"));
    TEST_ASSERT_NOT_NULL(svc);
    TEST_ASSERT_TRUE(svc->deprecated);
    TEST_ASSERT_NOT_NULL(svc->response);
    TEST_ASSERT_TRUE(svc->response->deprecated);

    teardown_dsdl();
}

static void test_cast_modes(void)
{
    setup_dsdl();

    TEST_ASSERT_TRUE(add_test_roots());

    const dsdl_type_composite_t* type = dsdl_read(&g_dsdl, wkv_key("validation.CastModes.0.1"));
    TEST_ASSERT_NOT_NULL(type);

    const dsdl_type_t* field = find_field_type(type, "saturated_implicit");
    TEST_ASSERT_NOT_NULL(field);
    TEST_ASSERT_TRUE(dsdl_type_is_uint(*field));
    TEST_ASSERT_FALSE(dsdl_type_is_truncated(*field));

    field = find_field_type(type, "saturated_signed");
    TEST_ASSERT_NOT_NULL(field);
    TEST_ASSERT_TRUE(dsdl_type_is_int(*field));
    TEST_ASSERT_FALSE(dsdl_type_is_truncated(*field));

    field = find_field_type(type, "saturated_float");
    TEST_ASSERT_NOT_NULL(field);
    TEST_ASSERT_TRUE(dsdl_type_is_float(*field));
    TEST_ASSERT_FALSE(dsdl_type_is_truncated(*field));

    field = find_field_type(type, "truncated_uint");
    TEST_ASSERT_NOT_NULL(field);
    TEST_ASSERT_TRUE(dsdl_type_is_uint(*field));
    TEST_ASSERT_TRUE(dsdl_type_is_truncated(*field));

    field = find_field_type(type, "truncated_float16");
    TEST_ASSERT_NOT_NULL(field);
    TEST_ASSERT_TRUE(dsdl_type_is_float(*field));
    TEST_ASSERT_TRUE(dsdl_type_is_truncated(*field));

    field = find_field_type(type, "truncated_float64");
    TEST_ASSERT_NOT_NULL(field);
    TEST_ASSERT_TRUE(dsdl_type_is_float(*field));
    TEST_ASSERT_TRUE(dsdl_type_is_truncated(*field));

    teardown_dsdl();
}

static void test_invalid_truncated_signed(void)
{
    setup_dsdl();

    TEST_ASSERT_TRUE(add_test_roots());

    const dsdl_type_composite_t* type = dsdl_read(&g_dsdl, wkv_key("invalid.TruncatedSigned.0.1"));
    TEST_ASSERT_NULL(type);

    teardown_dsdl();
}

// ============================================================================
// Version resolution tests
// ============================================================================

static void test_version_resolution_exact(void)
{
    setup_dsdl();

    TEST_ASSERT_TRUE(add_test_roots());

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

    TEST_ASSERT_TRUE(add_test_roots());

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

    TEST_ASSERT_TRUE(add_test_roots());

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

    TEST_ASSERT_TRUE(add_test_roots());

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
    RUN_TEST(test_constants_evaluated);
    RUN_TEST(test_constant_types_exposed);
    RUN_TEST(test_type_constant_and_attribute_access);
    RUN_TEST(test_type_attributes_in_asserts);
    RUN_TEST(test_array_capacity_expressions);
    RUN_TEST(test_service_response_type);
    RUN_TEST(test_deprecated_flag);
    RUN_TEST(test_cast_modes);
    RUN_TEST(test_invalid_truncated_signed);

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
