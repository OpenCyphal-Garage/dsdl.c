/// Tests for DSDL serialization/deserialization
///
/// Tests bit-level serialization and deserialization of DSDL types.

// Include dsdl.c for access to internal functions
#include "dsdl.c"

#include "unity.h"

#include <dirent.h>
#include <stdio.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

// ============================================================================
// Test helpers
// ============================================================================

static dsdl_t g_dsdl;

#ifndef DSDL_TEST_ROOT
#define DSDL_TEST_ROOT "."
#endif

static void* test_realloc(dsdl_t* self, void* ptr, size_t new_size)
{
    (void)self;
    if (new_size == 0) {
        free(ptr);
        return NULL;
    }
    return realloc(ptr, new_size);
}

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
    struct dirent* entry;
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
    return add_namespace_rel("test_dsdl_root_namespaces/0") && add_namespace_rel("test_dsdl_root_namespaces/1");
}

// ============================================================================
// Bit buffer unit tests
// ============================================================================

void test_bitbuf_write_read_byte_aligned(void)
{
    uint8_t       buffer[8] = { 0 };
    dsdl_bitbuf_t buf       = { buffer, 64, 0, dsdl_error_none };

    // Write 8 bits (byte-aligned)
    dsdl_bitbuf_write(&buf, 0xAB, 8);
    TEST_ASSERT_EQUAL_UINT8(0xAB, buffer[0]);
    TEST_ASSERT_EQUAL_size_t(8, buf.offset_bits);

    // Write another 8 bits
    dsdl_bitbuf_write(&buf, 0xCD, 8);
    TEST_ASSERT_EQUAL_UINT8(0xCD, buffer[1]);
    TEST_ASSERT_EQUAL_size_t(16, buf.offset_bits);

    // Read back
    buf.offset_bits = 0;
    TEST_ASSERT_EQUAL_UINT32(0xAB, (uint32_t)dsdl_bitbuf_read(&buf, 8));
    TEST_ASSERT_EQUAL_UINT32(0xCD, (uint32_t)dsdl_bitbuf_read(&buf, 8));
}

void test_bitbuf_write_read_non_aligned(void)
{
    uint8_t       buffer[8] = { 0 };
    dsdl_bitbuf_t buf       = { buffer, 64, 0, dsdl_error_none };

    // Write 3 bits: 0b101 = 5
    dsdl_bitbuf_write(&buf, 5, 3);
    TEST_ASSERT_EQUAL_size_t(3, buf.offset_bits);
    TEST_ASSERT_EQUAL_UINT8(0x05, buffer[0]); // 00000101

    // Write 5 bits: 0b11010 = 26
    dsdl_bitbuf_write(&buf, 26, 5);
    TEST_ASSERT_EQUAL_size_t(8, buf.offset_bits);
    // Buffer[0] should be: 101 | 11010 (shifted left 3) = 101 | 10101000 >> 5 bits...
    // Actually: bits 0-2 = 101 (5), bits 3-7 = 11010 (26)
    // So buffer[0] = 0b11010101 = 0xD5
    TEST_ASSERT_EQUAL_UINT8(0xD5, buffer[0]);

    // Read back
    buf.offset_bits = 0;
    TEST_ASSERT_EQUAL_UINT32(5, (uint32_t)dsdl_bitbuf_read(&buf, 3));
    TEST_ASSERT_EQUAL_UINT32(26, (uint32_t)dsdl_bitbuf_read(&buf, 5));
}

void test_bitbuf_write_cross_byte(void)
{
    uint8_t       buffer[8] = { 0 };
    dsdl_bitbuf_t buf       = { buffer, 64, 0, dsdl_error_none };

    // Write 4 bits
    dsdl_bitbuf_write(&buf, 0xF, 4);
    TEST_ASSERT_EQUAL_size_t(4, buf.offset_bits);

    // Write 16 bits that cross byte boundaries
    dsdl_bitbuf_write(&buf, 0x1234, 16);
    TEST_ASSERT_EQUAL_size_t(20, buf.offset_bits);

    // Read back
    buf.offset_bits = 0;
    TEST_ASSERT_EQUAL_UINT32(0xF, (uint32_t)dsdl_bitbuf_read(&buf, 4));
    TEST_ASSERT_EQUAL_UINT32(0x1234, (uint32_t)dsdl_bitbuf_read(&buf, 16));
}

void test_bitbuf_read_implicit_zero_extension(void)
{
    uint8_t       buffer[2] = { 0xAB, 0xCD };
    dsdl_bitbuf_t buf       = { buffer, 16, 0, dsdl_error_none };

    // Read more bits than available - implicit zero extension per Cyphal spec
    // Should return 0 and advance offset, NOT set error
    buf.offset_bits = 8;
    TEST_ASSERT_EQUAL_UINT32(0, (uint32_t)dsdl_bitbuf_read(&buf, 16));
    TEST_ASSERT_EQUAL(dsdl_error_none, buf.error);
    TEST_ASSERT_EQUAL_size_t(24, buf.offset_bits);
}

// ============================================================================
// Primitive serialization tests
// ============================================================================

void test_serialize_uint8(void)
{
    uint8_t       buffer[8] = { 0 };
    dsdl_bitbuf_t buf       = { buffer, 64, 0, dsdl_error_none };

    uint8_t value = 0x42;
    dsdl_serialize_primitive(&buf, DSDL_UINT(8), &value);

    TEST_ASSERT_EQUAL_size_t(8, buf.offset_bits);
    TEST_ASSERT_EQUAL_UINT8(0x42, buffer[0]);

    // Deserialize
    buf.offset_bits = 0;
    uint8_t result  = 0;
    dsdl_deserialize_primitive(&buf, DSDL_UINT(8), &result);
    TEST_ASSERT_EQUAL_UINT8(0x42, result);
}

void test_serialize_uint32(void)
{
    uint8_t       buffer[8] = { 0 };
    dsdl_bitbuf_t buf       = { buffer, 64, 0, dsdl_error_none };

    uint32_t value = 0x12345678;
    dsdl_serialize_primitive(&buf, DSDL_UINT(32), &value);

    TEST_ASSERT_EQUAL_size_t(32, buf.offset_bits);
    // Little-endian: 0x78, 0x56, 0x34, 0x12
    TEST_ASSERT_EQUAL_UINT8(0x78, buffer[0]);
    TEST_ASSERT_EQUAL_UINT8(0x56, buffer[1]);
    TEST_ASSERT_EQUAL_UINT8(0x34, buffer[2]);
    TEST_ASSERT_EQUAL_UINT8(0x12, buffer[3]);

    // Deserialize
    buf.offset_bits = 0;
    uint32_t result = 0;
    dsdl_deserialize_primitive(&buf, DSDL_UINT(32), &result);
    TEST_ASSERT_EQUAL_UINT32(0x12345678, result);
}

void test_serialize_int16_negative(void)
{
    uint8_t       buffer[8] = { 0 };
    dsdl_bitbuf_t buf       = { buffer, 64, 0, dsdl_error_none };

    int16_t value = -1234; // 0xFB2E in two's complement
    dsdl_serialize_primitive(&buf, DSDL_INT(16), &value);

    TEST_ASSERT_EQUAL_size_t(16, buf.offset_bits);

    // Deserialize
    buf.offset_bits = 0;
    int16_t result  = 0;
    dsdl_deserialize_primitive(&buf, DSDL_INT(16), &result);
    TEST_ASSERT_EQUAL_INT16(-1234, result);
}

void test_serialize_bool(void)
{
    uint8_t       buffer[8] = { 0 };
    dsdl_bitbuf_t buf       = { buffer, 64, 0, dsdl_error_none };

    bool value_true  = true;
    bool value_false = false;

    dsdl_serialize_primitive(&buf, DSDL_BOOL, &value_true);
    TEST_ASSERT_EQUAL_size_t(1, buf.offset_bits);

    dsdl_serialize_primitive(&buf, DSDL_BOOL, &value_false);
    TEST_ASSERT_EQUAL_size_t(2, buf.offset_bits);

    // Buffer should have: bit0=1, bit1=0 -> 0x01
    TEST_ASSERT_EQUAL_UINT8(0x01, buffer[0]);

    // Deserialize
    buf.offset_bits   = 0;
    bool result_true  = false;
    bool result_false = true;
    dsdl_deserialize_primitive(&buf, DSDL_BOOL, &result_true);
    dsdl_deserialize_primitive(&buf, DSDL_BOOL, &result_false);
    TEST_ASSERT_TRUE(result_true);
    TEST_ASSERT_FALSE(result_false);
}

void test_serialize_float32(void)
{
    uint8_t       buffer[8] = { 0 };
    dsdl_bitbuf_t buf       = { buffer, 64, 0, dsdl_error_none };

    float value = 3.14159f;
    dsdl_serialize_primitive(&buf, DSDL_FLOAT(32), &value);

    TEST_ASSERT_EQUAL_size_t(32, buf.offset_bits);

    // Deserialize
    buf.offset_bits = 0;
    float result    = 0.0f;
    dsdl_deserialize_primitive(&buf, DSDL_FLOAT(32), &result);
    TEST_ASSERT_FLOAT_WITHIN(0.00001f, 3.14159f, result);
}

void test_serialize_cast_mode_uint(void)
{
    uint8_t       buffer[2] = { 0 };
    dsdl_bitbuf_t buf       = { buffer, 16, 0, dsdl_error_none };

    uint_least8_t value = 0x11U;
    dsdl_serialize_primitive(&buf, DSDL_UINT_TRUNC(2), &value);
    TEST_ASSERT_FALSE(buf.error);

    buf.offset_bits         = 0;
    uint_least8_t truncated = 0U;
    dsdl_deserialize_primitive(&buf, DSDL_UINT_TRUNC(2), &truncated);
    TEST_ASSERT_EQUAL_UINT8(1U, truncated);

    (void)memset(buffer, 0, sizeof(buffer));
    buf.offset_bits = 0;
    buf.error       = false;
    dsdl_serialize_primitive(&buf, DSDL_UINT(2), &value);
    TEST_ASSERT_FALSE(buf.error);

    buf.offset_bits         = 0;
    uint_least8_t saturated = 0U;
    dsdl_deserialize_primitive(&buf, DSDL_UINT(2), &saturated);
    TEST_ASSERT_EQUAL_UINT8(3U, saturated);
}

void test_serialize_cast_mode_int(void)
{
    uint8_t       buffer[2] = { 0 };
    dsdl_bitbuf_t buf       = { buffer, 16, 0, dsdl_error_none };

    int_least8_t value = 20;
    dsdl_serialize_primitive(&buf, DSDL_INT(5), &value);
    TEST_ASSERT_FALSE(buf.error);

    buf.offset_bits        = 0;
    int_least8_t saturated = 0;
    dsdl_deserialize_primitive(&buf, DSDL_INT(5), &saturated);
    TEST_ASSERT_EQUAL_INT8(15, saturated);

    (void)memset(buffer, 0, sizeof(buffer));
    buf.offset_bits = 0;
    buf.error       = false;
    value           = -20;
    dsdl_serialize_primitive(&buf, DSDL_INT(5), &value);
    TEST_ASSERT_FALSE(buf.error);

    buf.offset_bits            = 0;
    int_least8_t saturated_min = 0;
    dsdl_deserialize_primitive(&buf, DSDL_INT(5), &saturated_min);
    TEST_ASSERT_EQUAL_INT8(-16, saturated_min);
}

void test_serialize_cast_mode_float16(void)
{
    uint8_t       buffer[4] = { 0 };
    dsdl_bitbuf_t buf       = { buffer, 32, 0, dsdl_error_none };

    float value = 1e9f;
    dsdl_serialize_primitive(&buf, DSDL_FLOAT_TRUNC(16), &value);
    TEST_ASSERT_FALSE(buf.error);

    buf.offset_bits = 0;
    float truncated = 0.0f;
    dsdl_deserialize_primitive(&buf, DSDL_FLOAT_TRUNC(16), &truncated);
    TEST_ASSERT_TRUE(isinf(truncated));
    TEST_ASSERT_TRUE(truncated > 0.0f);

    (void)memset(buffer, 0, sizeof(buffer));
    buf.offset_bits = 0;
    buf.error       = false;
    dsdl_serialize_primitive(&buf, DSDL_FLOAT(16), &value);
    TEST_ASSERT_FALSE(buf.error);

    buf.offset_bits = 0;
    float saturated = 0.0f;
    dsdl_deserialize_primitive(&buf, DSDL_FLOAT(16), &saturated);
    TEST_ASSERT_FLOAT_WITHIN(0.1f, 65504.0f, saturated);
}

// ============================================================================
// Integration test with real DSDL type
// ============================================================================

void test_serialize_simple_struct(void)
{
    setup_dsdl();

    TEST_ASSERT_TRUE(add_test_roots());

    // Load Simple.1.0: int32 a, float16 b, bool c
    const dsdl_type_composite_t* simple = dsdl_read(&g_dsdl, wkv_key("mymsgs.Simple.1.0"));
    TEST_ASSERT_NOT_NULL(simple);
    TEST_ASSERT_EQUAL_size_t(3, simple->field_count);

    // Create field values
    // float16 is stored as native float, not uint16_t
    int32_t field_a = 0x12345678;
    float   field_b = 1.0f; // Will serialize as 0x3C00 in float16
    bool    field_c = true;

    // Create values array (pointers to field values) and wrap in struct
    void*               field_ptrs[] = { &field_a, &field_b, &field_c };
    dsdl_value_struct_t sval         = { .values = field_ptrs };

    // Serialize
    uint8_t buffer[16] = { 0 };
    size_t  size       = dsdl_serialize(simple, &sval, sizeof(buffer), buffer, NULL);

    // Expected: 32 + 16 + 1 = 49 bits = 7 bytes
    TEST_ASSERT_EQUAL_size_t(7, size);

    // Verify int32 (little-endian): 0x78, 0x56, 0x34, 0x12
    TEST_ASSERT_EQUAL_UINT8(0x78, buffer[0]);
    TEST_ASSERT_EQUAL_UINT8(0x56, buffer[1]);
    TEST_ASSERT_EQUAL_UINT8(0x34, buffer[2]);
    TEST_ASSERT_EQUAL_UINT8(0x12, buffer[3]);

    // Verify float16: 0x00, 0x3C (little-endian) for 1.0
    TEST_ASSERT_EQUAL_UINT8(0x00, buffer[4]);
    TEST_ASSERT_EQUAL_UINT8(0x3C, buffer[5]);

    // Verify bool: bit 0 of buffer[6] = 1
    TEST_ASSERT_EQUAL_UINT8(0x01, buffer[6]);

    // Deserialize
    int32_t             result_a      = 0;
    float               result_b      = 0.0f;
    bool                result_c      = false;
    void*               result_ptrs[] = { &result_a, &result_b, &result_c };
    dsdl_value_struct_t result_sval   = { .values = result_ptrs };

    size_t consumed = dsdl_deserialize(simple, &result_sval, size, buffer, NULL);
    TEST_ASSERT_EQUAL_size_t(7, consumed);
    TEST_ASSERT_EQUAL_INT32(0x12345678, result_a);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 1.0f, result_b);
    TEST_ASSERT_TRUE(result_c);

    teardown_dsdl();
}

void test_serialize_variable_array_struct(void)
{
    setup_dsdl();

    TEST_ASSERT_TRUE(add_test_roots());

    // Load Inner.1.0: uint32[<=5] inner_items
    const dsdl_type_composite_t* inner = dsdl_read(&g_dsdl, wkv_key("mymsgs.Inner.1.0"));
    TEST_ASSERT_NOT_NULL(inner);
    TEST_ASSERT_EQUAL_size_t(1, inner->field_count);

    // The field type should be a variable array
    const dsdl_type_t* field_type = inner->field_types[0];
    TEST_ASSERT_EQUAL(DSDL_ARRAY_VARIABLE, *field_type);

    const dsdl_type_array_t* arr_type = (const dsdl_type_array_t*)field_type;
    TEST_ASSERT_EQUAL_size_t(5, arr_type->capacity);

    // Create array elements
    uint32_t elements[5] = { 0x11111111, 0x22222222, 0x33333333, 0, 0 };

    // Create variable array value
    dsdl_value_array_variable_t array_val = { .count = 3, .members = elements };

    // Wrap in struct (the struct has one field which is the array)
    void*               field_ptrs[] = { &array_val };
    dsdl_value_struct_t sval         = { .values = field_ptrs };

    // Serialize
    uint8_t buffer[32] = { 0 };
    size_t  size       = dsdl_serialize(inner, &sval, sizeof(buffer), buffer, NULL);

    // Expected: 8 bits length prefix + 3 * 32 bits = 104 bits = 13 bytes
    TEST_ASSERT_EQUAL_size_t(13, size);

    // Length prefix: 3 encoded in 8 bits at byte 0. First element starts at byte 1.
    TEST_ASSERT_EQUAL_UINT8(0x03, buffer[0]);
    TEST_ASSERT_EQUAL_UINT8(0x11, buffer[1]);
    TEST_ASSERT_EQUAL_UINT8(0x22, buffer[5]);
    TEST_ASSERT_EQUAL_UINT8(0x33, buffer[9]);

    teardown_dsdl();
}

void test_serialize_nested_struct(void)
{
    setup_dsdl();

    TEST_ASSERT_TRUE(add_test_roots());

    // Load Outer.1.0: float32[<=8] outer_items, Inner.1.0 inner
    const dsdl_type_composite_t* outer = dsdl_read(&g_dsdl, wkv_key("mymsgs.Outer.1.0"));
    TEST_ASSERT_NOT_NULL(outer);
    TEST_ASSERT_EQUAL_size_t(2, outer->field_count);

    // First field: float32[<=8]
    float                       outer_elements[8] = { 1.0f, 2.0f, 0, 0, 0, 0, 0, 0 };
    dsdl_value_array_variable_t outer_items       = { .count = 2, .members = outer_elements };

    // Second field: Inner.1.0 (uint32[<=5])
    uint32_t                    inner_elements[5] = { 0xDEADBEEF, 0, 0, 0, 0 };
    dsdl_value_array_variable_t inner_items       = { .count = 1, .members = inner_elements };

    // Inner struct value (Inner.1.0 has one field: the array)
    void*               inner_field_ptrs[] = { &inner_items };
    dsdl_value_struct_t inner_sval         = { .values = inner_field_ptrs };

    // Outer struct value
    void*               outer_field_ptrs[] = { &outer_items, &inner_sval };
    dsdl_value_struct_t outer_sval         = { .values = outer_field_ptrs };

    // Serialize
    uint8_t buffer[64] = { 0 };
    size_t  size       = dsdl_serialize(outer, &outer_sval, sizeof(buffer), buffer, NULL);

    // Expected size:
    // float32[<=8] with 2 elements: 8 prefix bits + 2*32 = 72 bits
    // Inner.1.0 (uint32[<=5] with 1 element): 8 prefix bits + 1*32 = 40 bits
    // Total: 72 + 40 = 112 bits = 14 bytes
    TEST_ASSERT_EQUAL_size_t(14, size);

    teardown_dsdl();
}

void test_roundtrip_simple_struct(void)
{
    setup_dsdl();

    TEST_ASSERT_TRUE(add_test_roots());

    const dsdl_type_composite_t* simple = dsdl_read(&g_dsdl, wkv_key("mymsgs.Simple.1.0"));
    TEST_ASSERT_NOT_NULL(simple);

    // Original values: int32 a, float16 b, bool c
    // float16 is stored as native float
    int32_t orig_a = -123456;
    float   orig_b = 3.14f;
    bool    orig_c = true;

    void*               orig_ptrs[] = { &orig_a, &orig_b, &orig_c };
    dsdl_value_struct_t orig_sval   = { .values = orig_ptrs };

    // Serialize
    uint8_t buffer[16] = { 0 };
    size_t  size       = dsdl_serialize(simple, &orig_sval, sizeof(buffer), buffer, NULL);
    TEST_ASSERT_EQUAL_size_t(7, size);

    // Deserialize into new values
    int32_t             result_a      = 0;
    float               result_b      = 0.0f;
    bool                result_c      = false;
    void*               result_ptrs[] = { &result_a, &result_b, &result_c };
    dsdl_value_struct_t result_sval   = { .values = result_ptrs };

    size_t consumed = dsdl_deserialize(simple, &result_sval, size, buffer, NULL);
    TEST_ASSERT_EQUAL_size_t(7, consumed);

    // Verify roundtrip (float16 has limited precision)
    TEST_ASSERT_EQUAL_INT32(orig_a, result_a);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, orig_b, result_b);
    TEST_ASSERT_EQUAL(orig_c, result_c);

    teardown_dsdl();
}

void test_roundtrip_variable_array(void)
{
    setup_dsdl();

    TEST_ASSERT_TRUE(add_test_roots());

    const dsdl_type_composite_t* inner = dsdl_read(&g_dsdl, wkv_key("mymsgs.Inner.1.0"));
    TEST_ASSERT_NOT_NULL(inner);

    // Original: uint32[<=5] with 3 elements
    uint32_t                    orig_elements[5] = { 0xAAAAAAAA, 0xBBBBBBBB, 0xCCCCCCCC, 0, 0 };
    dsdl_value_array_variable_t orig_array       = { .count = 3, .members = orig_elements };

    void*               orig_ptrs[] = { &orig_array };
    dsdl_value_struct_t orig_sval   = { .values = orig_ptrs };

    // Serialize
    uint8_t buffer[32] = { 0 };
    size_t  size       = dsdl_serialize(inner, &orig_sval, sizeof(buffer), buffer, NULL);
    // 8 prefix bits + 3*32 = 104 bits = 13 bytes
    TEST_ASSERT_EQUAL_size_t(13, size);

    // Deserialize
    uint32_t                    result_elements[5] = { 0 };
    dsdl_value_array_variable_t result_array       = { .count = 5, .members = result_elements }; // capacity = 5

    void*               result_ptrs[] = { &result_array };
    dsdl_value_struct_t result_sval   = { .values = result_ptrs };

    size_t consumed = dsdl_deserialize(inner, &result_sval, size, buffer, NULL);
    TEST_ASSERT_EQUAL_size_t(13, consumed);

    // Verify
    TEST_ASSERT_EQUAL_size_t(3, result_array.count);
    TEST_ASSERT_EQUAL_UINT32(0xAAAAAAAA, result_elements[0]);
    TEST_ASSERT_EQUAL_UINT32(0xBBBBBBBB, result_elements[1]);
    TEST_ASSERT_EQUAL_UINT32(0xCCCCCCCC, result_elements[2]);

    teardown_dsdl();
}

void test_deserialize_array_overflow_fails(void)
{
    setup_dsdl();

    TEST_ASSERT_TRUE(add_test_roots());

    const dsdl_type_composite_t* inner = dsdl_read(&g_dsdl, wkv_key("mymsgs.Inner.1.0"));
    TEST_ASSERT_NOT_NULL(inner);

    // Serialize 3 elements
    uint32_t                    orig_elements[5] = { 0xAAAAAAAA, 0xBBBBBBBB, 0xCCCCCCCC, 0, 0 };
    dsdl_value_array_variable_t orig_array       = { .count = 3, .members = orig_elements };

    void*               orig_ptrs[] = { &orig_array };
    dsdl_value_struct_t orig_sval   = { .values = orig_ptrs };

    uint8_t buffer[32] = { 0 };
    size_t  size       = dsdl_serialize(inner, &orig_sval, sizeof(buffer), buffer, NULL);
    TEST_ASSERT_EQUAL_size_t(13, size);

    // Try to deserialize into a buffer that can only hold 2 elements - should fail
    uint32_t                    small_elements[2] = { 0 };
    dsdl_value_array_variable_t small_array       = { .count = 2, .members = small_elements };

    void*               small_ptrs[] = { &small_array };
    dsdl_value_struct_t small_sval   = { .values = small_ptrs };

    size_t consumed = dsdl_deserialize(inner, &small_sval, size, buffer, NULL);
    TEST_ASSERT_EQUAL_size_t(SIZE_MAX, consumed); // Should fail

    teardown_dsdl();
}

void test_serialize_array_overflow_fails(void)
{
    setup_dsdl();

    TEST_ASSERT_TRUE(add_test_roots());

    const dsdl_type_composite_t* inner = dsdl_read(&g_dsdl, wkv_key("mymsgs.Inner.1.0"));
    TEST_ASSERT_NOT_NULL(inner);

    uint32_t                    elements[6] = { 1, 2, 3, 4, 5, 6 };
    dsdl_value_array_variable_t array_val   = { .count = 6, .members = elements };

    void*               field_ptrs[] = { &array_val };
    dsdl_value_struct_t sval         = { .values = field_ptrs };

    uint8_t buffer[64] = { 0 };
    size_t  size       = dsdl_serialize(inner, &sval, sizeof(buffer), buffer, NULL);
    TEST_ASSERT_EQUAL_size_t(SIZE_MAX, size);

    teardown_dsdl();
}

void test_deserialize_array_prefix_exceeds_capacity_fails(void)
{
    setup_dsdl();

    TEST_ASSERT_TRUE(add_test_roots());

    const dsdl_type_composite_t* inner = dsdl_read(&g_dsdl, wkv_key("mymsgs.Inner.1.0"));
    TEST_ASSERT_NOT_NULL(inner);

    // Prefix bits for capacity 5: 8 (byte-aligned). Encode count = 6.
    uint8_t buffer[1] = { 0x06 };

    uint32_t                    elements[5]  = { 0 };
    dsdl_value_array_variable_t array_val    = { .count = 5, .members = elements };
    void*                       field_ptrs[] = { &array_val };
    dsdl_value_struct_t         sval         = { .values = field_ptrs };

    size_t consumed = dsdl_deserialize(inner, &sval, sizeof(buffer), buffer, NULL);
    TEST_ASSERT_EQUAL_size_t(SIZE_MAX, consumed);

    teardown_dsdl();
}

void test_serialize_union_invalid_tag_fails(void)
{
    static dsdl_type_t field_a_type = DSDL_UINT(8);
    static dsdl_type_t field_b_type = DSDL_UINT(16);
    static dsdl_type_t field_c_type = DSDL_UINT(32);

    static dsdl_type_t* field_types[3];
    field_types[0] = &field_a_type;
    field_types[1] = &field_b_type;
    field_types[2] = &field_c_type;

    static wkv_str_t field_names[3] = { { 1, "a" }, { 1, "b" }, { 1, "c" } };

    dsdl_type_composite_t union_type = {
        .type        = DSDL_COMPOSITE_UNION,
        .name        = { 12, "BadTagUnion" },
        .version     = { 1, 0 },
        .extent      = 8,
        .sealed      = true,
        .field_count = 3,
        .field_names = field_names,
        .field_types = field_types,
    };

    uint32_t           value = 0x12345678;
    dsdl_value_union_t uval  = { .tag = 3, .value = &value }; // Invalid tag for 3 fields

    uint8_t buffer[8] = { 0 };
    size_t  size      = dsdl_serialize(&union_type, &uval, sizeof(buffer), buffer, NULL);
    TEST_ASSERT_EQUAL_size_t(SIZE_MAX, size);
}

void test_deserialize_union_invalid_tag_fails(void)
{
    static dsdl_type_t field_a_type = DSDL_UINT(8);
    static dsdl_type_t field_b_type = DSDL_UINT(16);
    static dsdl_type_t field_c_type = DSDL_UINT(32);

    static dsdl_type_t* field_types[3];
    field_types[0] = &field_a_type;
    field_types[1] = &field_b_type;
    field_types[2] = &field_c_type;

    static wkv_str_t field_names[3] = { { 1, "a" }, { 1, "b" }, { 1, "c" } };

    dsdl_type_composite_t union_type = {
        .type        = DSDL_COMPOSITE_UNION,
        .name        = { 12, "BadTagUnion" },
        .version     = { 1, 0 },
        .extent      = 8,
        .sealed      = true,
        .field_count = 3,
        .field_names = field_names,
        .field_types = field_types,
    };

    // Tag bits = 8, encode invalid tag 3.
    uint8_t buffer[1] = { 0x03 };

    uint8_t            value = 0;
    dsdl_value_union_t uval  = { .tag = 0, .value = &value };

    size_t consumed = dsdl_deserialize(&union_type, &uval, sizeof(buffer), buffer, NULL);
    TEST_ASSERT_EQUAL_size_t(SIZE_MAX, consumed);
}

void test_serialize_unsigned_saturates(void)
{
    static dsdl_type_t  field_type = DSDL_UINT(7);
    static dsdl_type_t* field_types[1];
    field_types[0]                  = &field_type;
    static wkv_str_t field_names[1] = { { 1, "a" } };

    dsdl_type_composite_t struct_type = {
        .type        = DSDL_COMPOSITE_STRUCT,
        .name        = { 12, "UInt7Struct" },
        .version     = { 1, 0 },
        .extent      = 1,
        .sealed      = true,
        .field_count = 1,
        .field_names = field_names,
        .field_types = field_types,
    };

    uint_least8_t       value        = 200; // Exceeds 7-bit range
    void*               field_ptrs[] = { &value };
    dsdl_value_struct_t sval         = { .values = field_ptrs };

    uint8_t buffer[4] = { 0 };
    size_t  size      = dsdl_serialize(&struct_type, &sval, sizeof(buffer), buffer, NULL);
    TEST_ASSERT_TRUE(size != SIZE_MAX);

    uint_least8_t       out_value  = 0;
    void*               out_ptrs[] = { &out_value };
    dsdl_value_struct_t out_sval   = { .values = out_ptrs };
    size_t              consumed   = dsdl_deserialize(&struct_type, &out_sval, size, buffer, NULL);
    TEST_ASSERT_TRUE(consumed != SIZE_MAX);
    TEST_ASSERT_EQUAL_UINT8(127, out_value);
}

void test_serialize_signed_saturates(void)
{
    static dsdl_type_t  field_type = DSDL_INT(5);
    static dsdl_type_t* field_types[1];
    field_types[0]                  = &field_type;
    static wkv_str_t field_names[1] = { { 1, "a" } };

    dsdl_type_composite_t struct_type = {
        .type        = DSDL_COMPOSITE_STRUCT,
        .name        = { 11, "Int5Struct" },
        .version     = { 1, 0 },
        .extent      = 1,
        .sealed      = true,
        .field_count = 1,
        .field_names = field_names,
        .field_types = field_types,
    };

    int_least8_t        value        = 20; // Exceeds 5-bit signed range [-16, 15]
    void*               field_ptrs[] = { &value };
    dsdl_value_struct_t sval         = { .values = field_ptrs };

    uint8_t buffer[4] = { 0 };
    size_t  size      = dsdl_serialize(&struct_type, &sval, sizeof(buffer), buffer, NULL);
    TEST_ASSERT_TRUE(size != SIZE_MAX);

    int_least8_t        out_value  = 0;
    void*               out_ptrs[] = { &out_value };
    dsdl_value_struct_t out_sval   = { .values = out_ptrs };
    size_t              consumed   = dsdl_deserialize(&struct_type, &out_sval, size, buffer, NULL);
    TEST_ASSERT_TRUE(consumed != SIZE_MAX);
    TEST_ASSERT_EQUAL_INT8(15, out_value);
}

void test_roundtrip_nested_struct(void)
{
    setup_dsdl();

    TEST_ASSERT_TRUE(add_test_roots());

    const dsdl_type_composite_t* outer = dsdl_read(&g_dsdl, wkv_key("mymsgs.Outer.1.0"));
    TEST_ASSERT_NOT_NULL(outer);

    // Original values
    float                       orig_outer_elements[8] = { 1.5f, 2.5f, 3.5f, 0, 0, 0, 0, 0 };
    dsdl_value_array_variable_t orig_outer_items       = { .count = 3, .members = orig_outer_elements };

    uint32_t                    orig_inner_elements[5] = { 0x12345678, 0x9ABCDEF0, 0, 0, 0 };
    dsdl_value_array_variable_t orig_inner_items       = { .count = 2, .members = orig_inner_elements };

    void*               orig_inner_ptrs[] = { &orig_inner_items };
    dsdl_value_struct_t orig_inner_sval   = { .values = orig_inner_ptrs };

    void*               orig_outer_ptrs[] = { &orig_outer_items, &orig_inner_sval };
    dsdl_value_struct_t orig_outer_sval   = { .values = orig_outer_ptrs };

    // Serialize
    uint8_t buffer[64] = { 0 };
    size_t  size       = dsdl_serialize(outer, &orig_outer_sval, sizeof(buffer), buffer, NULL);
    // float32[<=8] with 3 elements: 8 + 96 = 104 bits
    // Inner (uint32[<=5] with 2 elements): 8 + 64 = 72 bits
    // Total: 176 bits = 22 bytes
    TEST_ASSERT_EQUAL_size_t(22, size);

    // Deserialize
    float                       result_outer_elements[8] = { 0 };
    dsdl_value_array_variable_t result_outer_items       = { .count = 8, .members = result_outer_elements };

    uint32_t                    result_inner_elements[5] = { 0 };
    dsdl_value_array_variable_t result_inner_items       = { .count = 5, .members = result_inner_elements };

    void*               result_inner_ptrs[] = { &result_inner_items };
    dsdl_value_struct_t result_inner_sval   = { .values = result_inner_ptrs };

    void*               result_outer_ptrs[] = { &result_outer_items, &result_inner_sval };
    dsdl_value_struct_t result_outer_sval   = { .values = result_outer_ptrs };

    size_t consumed = dsdl_deserialize(outer, &result_outer_sval, size, buffer, NULL);
    TEST_ASSERT_EQUAL_size_t(22, consumed);

    // Verify outer array
    TEST_ASSERT_EQUAL_size_t(3, result_outer_items.count);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 1.5f, result_outer_elements[0]);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 2.5f, result_outer_elements[1]);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 3.5f, result_outer_elements[2]);

    // Verify inner array
    TEST_ASSERT_EQUAL_size_t(2, result_inner_items.count);
    TEST_ASSERT_EQUAL_UINT32(0x12345678, result_inner_elements[0]);
    TEST_ASSERT_EQUAL_UINT32(0x9ABCDEF0, result_inner_elements[1]);

    teardown_dsdl();
}

// ============================================================================
// Union tests
// ============================================================================

void test_serialize_union(void)
{
    // Create a simple union type manually for testing
    // Union with 2 fields: uint8 a, uint32 b
    // Tag is byte-aligned (8 bits)

    // Allocate type descriptor
    static dsdl_type_t field_a_type = DSDL_UINT(8);
    static dsdl_type_t field_b_type = DSDL_UINT(32);

    static dsdl_type_t* field_types[2];
    field_types[0] = &field_a_type;
    field_types[1] = &field_b_type;

    static wkv_str_t field_names[2] = { { 1, "a" }, { 1, "b" } };

    dsdl_type_composite_t union_type = {
        .type        = DSDL_COMPOSITE_UNION,
        .name        = { 10, "TestUnion" },
        .version     = { 1, 0 },
        .extent      = 8,
        .sealed      = true,
        .field_count = 2,
        .field_names = field_names,
        .field_types = field_types,
    };

    // Test case 1: Select variant 0 (uint8)
    {
        uint8_t            value_0 = 0xAB;
        dsdl_value_union_t uval    = { .tag = 0, .value = &value_0 };

        uint8_t buffer[8] = { 0 };
        size_t  size      = dsdl_serialize(&union_type, &uval, sizeof(buffer), buffer, NULL);

        // 8 tag bits + 8 value bits = 16 bits = 2 bytes
        TEST_ASSERT_EQUAL_size_t(2, size);

        // Tag 0 in byte 0, value 0xAB in byte 1.
        TEST_ASSERT_EQUAL_UINT8(0x00, buffer[0]);
        TEST_ASSERT_EQUAL_UINT8(0xAB, buffer[1]);

        // Deserialize
        uint8_t            result_value = 0;
        dsdl_value_union_t result_uval  = { .tag = 99, .value = &result_value };

        size_t consumed = dsdl_deserialize(&union_type, &result_uval, size, buffer, NULL);
        TEST_ASSERT_EQUAL_size_t(2, consumed);
        TEST_ASSERT_EQUAL_size_t(0, result_uval.tag);
        TEST_ASSERT_EQUAL_UINT8(0xAB, result_value);
    }

    // Test case 2: Select variant 1 (uint32)
    {
        uint32_t           value_1 = 0x12345678;
        dsdl_value_union_t uval    = { .tag = 1, .value = &value_1 };

        uint8_t buffer[8] = { 0 };
        size_t  size      = dsdl_serialize(&union_type, &uval, sizeof(buffer), buffer, NULL);

        // 8 tag bits + 32 value bits = 40 bits = 5 bytes
        TEST_ASSERT_EQUAL_size_t(5, size);

        // Deserialize
        uint32_t           result_value = 0;
        dsdl_value_union_t result_uval  = { .tag = 99, .value = &result_value };

        size_t consumed = dsdl_deserialize(&union_type, &result_uval, size, buffer, NULL);
        TEST_ASSERT_EQUAL_size_t(5, consumed);
        TEST_ASSERT_EQUAL_size_t(1, result_uval.tag);
        TEST_ASSERT_EQUAL_UINT32(0x12345678, result_value);
    }
}

// ============================================================================
// Float16 conversion tests
// ============================================================================

static void test_float16_pack(void)
{
    // Test values from Nunavut test_support.c

    // 3.14f -> 0x4248
    TEST_ASSERT_EQUAL_HEX16(0x4248, dsdl_float16_pack(3.14f));

    // -3.14f -> 0xC248
    TEST_ASSERT_EQUAL_HEX16(0xC248, dsdl_float16_pack(-3.14f));

    // Large value (overflow to infinity) -> 0x7C00
    TEST_ASSERT_EQUAL_HEX16(0x7C00, dsdl_float16_pack(65536.0f));

    // Negative large value -> 0xFC00
    TEST_ASSERT_EQUAL_HEX16(0xFC00, dsdl_float16_pack(-65536.0f));

    // Zero -> 0x0000
    TEST_ASSERT_EQUAL_HEX16(0x0000, dsdl_float16_pack(0.0f));

    // Negative zero -> 0x8000
    TEST_ASSERT_EQUAL_HEX16(0x8000, dsdl_float16_pack(-0.0f));

    // Infinity -> 0x7C00
    TEST_ASSERT_EQUAL_HEX16(0x7C00, dsdl_float16_pack(INFINITY));

    // Negative infinity -> 0xFC00
    TEST_ASSERT_EQUAL_HEX16(0xFC00, dsdl_float16_pack(-INFINITY));
}

static void test_float16_unpack(void)
{
    // Test values from Nunavut test_support.c

    // 0xC248 -> -3.14f
    TEST_ASSERT_FLOAT_WITHIN(0.01f, -3.14f, dsdl_float16_unpack(0xC248));

    // 0x4248 -> 3.14f
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 3.14f, dsdl_float16_unpack(0x4248));

    // 0x7e00 -> NaN
    TEST_ASSERT_FLOAT_IS_NAN(dsdl_float16_unpack(0x7E00));

    // 0xfe00 -> -NaN
    TEST_ASSERT_FLOAT_IS_NAN(dsdl_float16_unpack(0xFE00));

    // 0x7c00 -> +Inf
    TEST_ASSERT_FLOAT_IS_INF(dsdl_float16_unpack(0x7C00));

    // 0xfc00 -> -Inf
    TEST_ASSERT_FLOAT_IS_NEG_INF(dsdl_float16_unpack(0xFC00));

    // 0x0000 -> 0.0f
    TEST_ASSERT_EQUAL_FLOAT(0.0f, dsdl_float16_unpack(0x0000));
}

static void test_float16_roundtrip(void)
{
    // Test roundtrip: pack then unpack should preserve value (within float16 precision)
    const float test_values[] = { 0.0f, 1.0f, -1.0f, 3.14f, -3.14f, 100.0f, -100.0f, 0.5f, -0.5f };
    for (size_t i = 0; i < sizeof(test_values) / sizeof(test_values[0]); i++) {
        const float    original = test_values[i];
        const uint16_t packed   = dsdl_float16_pack(original);
        const float    unpacked = dsdl_float16_unpack(packed);
        // Float16 has limited precision, so we allow some tolerance
        TEST_ASSERT_FLOAT_WITHIN(0.01f * (1.0f + fabsf(original)), original, unpacked);
    }
}

static void test_serialize_float16(void)
{
    // Test float16 serialization through the primitive serializer
    uint8_t       buffer[4] = { 0 };
    dsdl_bitbuf_t buf       = { buffer, sizeof(buffer) * 8, 0, dsdl_error_none }; // capacity_bits in bits!

    const float value = 3.14f;
    dsdl_serialize_primitive(&buf, DSDL_FLOAT(16), &value);

    // Expected: 0x4248 (little-endian: 0x48, 0x42)
    TEST_ASSERT_EQUAL_HEX8(0x48, buffer[0]);
    TEST_ASSERT_EQUAL_HEX8(0x42, buffer[1]);
}

static void test_deserialize_float16(void)
{
    // Test float16 deserialization through the primitive deserializer
    // 0x4248 in little-endian
    const uint8_t buffer[4] = { 0x48, 0x42, 0x00, 0x00 };
    dsdl_bitbuf_t buf       = { (uint8_t*)buffer, sizeof(buffer) * 8, 0, dsdl_error_none }; // capacity_bits in bits!

    float value = 0.0f;
    dsdl_deserialize_primitive(&buf, DSDL_FLOAT(16), &value);

    TEST_ASSERT_FLOAT_WITHIN(0.01f, 3.14f, value);
}

// ============================================================================
// Main
// ============================================================================

void setUp(void) {}
void tearDown(void) {}

int main(void)
{
    UNITY_BEGIN();

    // Bit buffer tests
    RUN_TEST(test_bitbuf_write_read_byte_aligned);
    RUN_TEST(test_bitbuf_write_read_non_aligned);
    RUN_TEST(test_bitbuf_write_cross_byte);
    RUN_TEST(test_bitbuf_read_implicit_zero_extension);

    // Primitive serialization tests
    RUN_TEST(test_serialize_uint8);
    RUN_TEST(test_serialize_uint32);
    RUN_TEST(test_serialize_int16_negative);
    RUN_TEST(test_serialize_bool);
    RUN_TEST(test_serialize_float32);
    RUN_TEST(test_serialize_cast_mode_uint);
    RUN_TEST(test_serialize_cast_mode_int);
    RUN_TEST(test_serialize_cast_mode_float16);

    // Float16 conversion tests
    RUN_TEST(test_float16_pack);
    RUN_TEST(test_float16_unpack);
    RUN_TEST(test_float16_roundtrip);
    RUN_TEST(test_serialize_float16);
    RUN_TEST(test_deserialize_float16);

    // Integration tests
    RUN_TEST(test_serialize_simple_struct);
    RUN_TEST(test_serialize_variable_array_struct);
    RUN_TEST(test_serialize_nested_struct);

    // Roundtrip tests
    RUN_TEST(test_roundtrip_simple_struct);
    RUN_TEST(test_roundtrip_variable_array);
    RUN_TEST(test_deserialize_array_overflow_fails);
    RUN_TEST(test_serialize_array_overflow_fails);
    RUN_TEST(test_deserialize_array_prefix_exceeds_capacity_fails);
    RUN_TEST(test_roundtrip_nested_struct);

    // Union tests
    RUN_TEST(test_serialize_union);
    RUN_TEST(test_serialize_union_invalid_tag_fails);
    RUN_TEST(test_deserialize_union_invalid_tag_fails);
    RUN_TEST(test_serialize_unsigned_saturates);
    RUN_TEST(test_serialize_signed_saturates);

    return UNITY_END();
}
