/// Tests for DSDL serialization/deserialization
///
/// Tests bit-level serialization and deserialization of DSDL types.

#include "unity.h"
#include "dsdl.h"

#include <stdlib.h>
#include <string.h>

// Include dsdl.c for access to internal functions
#include "dsdl.c"

// ============================================================================
// Test helpers
// ============================================================================

static dsdl_t g_dsdl;

static void* test_realloc(dsdl_t* self, void* ptr, size_t new_size)
{
    (void)self;
    if (new_size == 0) {
        free(ptr);
        return NULL;
    }
    return realloc(ptr, new_size);
}

static void* test_read_file(dsdl_t* self, wkv_str_t path, size_t* out_size)
{
    (void)self;

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

    fseek(f, 0, SEEK_END);
    const long size = ftell(f);
    fseek(f, 0, SEEK_SET);

    if (size < 0) {
        fclose(f);
        return NULL;
    }

    void* buffer = self->realloc(self, NULL, (size_t)size);
    if (buffer == NULL) {
        fclose(f);
        return NULL;
    }

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
// Bit buffer unit tests
// ============================================================================

void test_bitbuf_write_read_byte_aligned(void)
{
    uint8_t buffer[8] = {0};
    _dsdl_bitbuf_t buf = {buffer, 64, 0};

    // Write 8 bits (byte-aligned)
    _dsdl_bitbuf_write(&buf, 0xAB, 8);
    TEST_ASSERT_EQUAL_UINT8(0xAB, buffer[0]);
    TEST_ASSERT_EQUAL_size_t(8, buf.offset_bits);

    // Write another 8 bits
    _dsdl_bitbuf_write(&buf, 0xCD, 8);
    TEST_ASSERT_EQUAL_UINT8(0xCD, buffer[1]);
    TEST_ASSERT_EQUAL_size_t(16, buf.offset_bits);

    // Read back
    buf.offset_bits = 0;
    TEST_ASSERT_EQUAL_UINT64(0xAB, _dsdl_bitbuf_read(&buf, 8));
    TEST_ASSERT_EQUAL_UINT64(0xCD, _dsdl_bitbuf_read(&buf, 8));
}

void test_bitbuf_write_read_non_aligned(void)
{
    uint8_t buffer[8] = {0};
    _dsdl_bitbuf_t buf = {buffer, 64, 0};

    // Write 3 bits: 0b101 = 5
    _dsdl_bitbuf_write(&buf, 5, 3);
    TEST_ASSERT_EQUAL_size_t(3, buf.offset_bits);
    TEST_ASSERT_EQUAL_UINT8(0x05, buffer[0]); // 00000101

    // Write 5 bits: 0b11010 = 26
    _dsdl_bitbuf_write(&buf, 26, 5);
    TEST_ASSERT_EQUAL_size_t(8, buf.offset_bits);
    // Buffer[0] should be: 101 | 11010 (shifted left 3) = 101 | 10101000 >> 5 bits...
    // Actually: bits 0-2 = 101 (5), bits 3-7 = 11010 (26)
    // So buffer[0] = 0b11010101 = 0xD5
    TEST_ASSERT_EQUAL_UINT8(0xD5, buffer[0]);

    // Read back
    buf.offset_bits = 0;
    TEST_ASSERT_EQUAL_UINT64(5, _dsdl_bitbuf_read(&buf, 3));
    TEST_ASSERT_EQUAL_UINT64(26, _dsdl_bitbuf_read(&buf, 5));
}

void test_bitbuf_write_cross_byte(void)
{
    uint8_t buffer[8] = {0};
    _dsdl_bitbuf_t buf = {buffer, 64, 0};

    // Write 4 bits
    _dsdl_bitbuf_write(&buf, 0xF, 4);
    TEST_ASSERT_EQUAL_size_t(4, buf.offset_bits);

    // Write 16 bits that cross byte boundaries
    _dsdl_bitbuf_write(&buf, 0x1234, 16);
    TEST_ASSERT_EQUAL_size_t(20, buf.offset_bits);

    // Read back
    buf.offset_bits = 0;
    TEST_ASSERT_EQUAL_UINT64(0xF, _dsdl_bitbuf_read(&buf, 4));
    TEST_ASSERT_EQUAL_UINT64(0x1234, _dsdl_bitbuf_read(&buf, 16));
}

void test_bitbuf_implicit_zero_extension(void)
{
    uint8_t buffer[2] = {0xAB, 0xCD};
    _dsdl_bitbuf_t buf = {buffer, 16, 0};

    // Read more bits than available - should zero-extend
    buf.offset_bits = 8;
    uint64_t value = _dsdl_bitbuf_read(&buf, 16);
    // First 8 bits from buffer[1] = 0xCD, remaining 8 bits = 0
    TEST_ASSERT_EQUAL_UINT64(0x00CD, value);
}

// ============================================================================
// Primitive serialization tests
// ============================================================================

void test_serialize_uint8(void)
{
    uint8_t buffer[8] = {0};
    _dsdl_bitbuf_t buf = {buffer, 64, 0};

    uint8_t value = 0x42;
    _dsdl_serialize_primitive(&buf, DSDL_UINT8, &value);

    TEST_ASSERT_EQUAL_size_t(8, buf.offset_bits);
    TEST_ASSERT_EQUAL_UINT8(0x42, buffer[0]);

    // Deserialize
    buf.offset_bits = 0;
    uint8_t result = 0;
    _dsdl_deserialize_primitive(&buf, DSDL_UINT8, &result);
    TEST_ASSERT_EQUAL_UINT8(0x42, result);
}

void test_serialize_uint32(void)
{
    uint8_t buffer[8] = {0};
    _dsdl_bitbuf_t buf = {buffer, 64, 0};

    uint32_t value = 0x12345678;
    _dsdl_serialize_primitive(&buf, DSDL_UINT32, &value);

    TEST_ASSERT_EQUAL_size_t(32, buf.offset_bits);
    // Little-endian: 0x78, 0x56, 0x34, 0x12
    TEST_ASSERT_EQUAL_UINT8(0x78, buffer[0]);
    TEST_ASSERT_EQUAL_UINT8(0x56, buffer[1]);
    TEST_ASSERT_EQUAL_UINT8(0x34, buffer[2]);
    TEST_ASSERT_EQUAL_UINT8(0x12, buffer[3]);

    // Deserialize
    buf.offset_bits = 0;
    uint32_t result = 0;
    _dsdl_deserialize_primitive(&buf, DSDL_UINT32, &result);
    TEST_ASSERT_EQUAL_UINT32(0x12345678, result);
}

void test_serialize_int16_negative(void)
{
    uint8_t buffer[8] = {0};
    _dsdl_bitbuf_t buf = {buffer, 64, 0};

    int16_t value = -1234; // 0xFB2E in two's complement
    _dsdl_serialize_primitive(&buf, DSDL_INT16, &value);

    TEST_ASSERT_EQUAL_size_t(16, buf.offset_bits);

    // Deserialize
    buf.offset_bits = 0;
    int16_t result = 0;
    _dsdl_deserialize_primitive(&buf, DSDL_INT16, &result);
    TEST_ASSERT_EQUAL_INT16(-1234, result);
}

void test_serialize_bool(void)
{
    uint8_t buffer[8] = {0};
    _dsdl_bitbuf_t buf = {buffer, 64, 0};

    bool value_true = true;
    bool value_false = false;

    _dsdl_serialize_primitive(&buf, DSDL_BOOL, &value_true);
    TEST_ASSERT_EQUAL_size_t(1, buf.offset_bits);

    _dsdl_serialize_primitive(&buf, DSDL_BOOL, &value_false);
    TEST_ASSERT_EQUAL_size_t(2, buf.offset_bits);

    // Buffer should have: bit0=1, bit1=0 -> 0x01
    TEST_ASSERT_EQUAL_UINT8(0x01, buffer[0]);

    // Deserialize
    buf.offset_bits = 0;
    bool result_true = false;
    bool result_false = true;
    _dsdl_deserialize_primitive(&buf, DSDL_BOOL, &result_true);
    _dsdl_deserialize_primitive(&buf, DSDL_BOOL, &result_false);
    TEST_ASSERT_TRUE(result_true);
    TEST_ASSERT_FALSE(result_false);
}

void test_serialize_float32(void)
{
    uint8_t buffer[8] = {0};
    _dsdl_bitbuf_t buf = {buffer, 64, 0};

    float value = 3.14159f;
    _dsdl_serialize_primitive(&buf, DSDL_FLOAT32, &value);

    TEST_ASSERT_EQUAL_size_t(32, buf.offset_bits);

    // Deserialize
    buf.offset_bits = 0;
    float result = 0.0f;
    _dsdl_deserialize_primitive(&buf, DSDL_FLOAT32, &result);
    TEST_ASSERT_FLOAT_WITHIN(0.00001f, 3.14159f, result);
}

// ============================================================================
// Integration test with real DSDL type
// ============================================================================

void test_serialize_simple_struct(void)
{
    setup_dsdl();

    TEST_ASSERT_TRUE(dsdl_add_namespace(&g_dsdl,
                                        wkv_key("test_dsdl_root_namespaces/nunavut_test_types/nested_array_types")));

    // Load Simple.1.0: int32 a, float16 b, bool c
    const dsdl_type_composite_t* simple = dsdl_read(&g_dsdl, wkv_key("mymsgs.Simple.1.0"));
    TEST_ASSERT_NOT_NULL(simple);
    TEST_ASSERT_EQUAL_size_t(3, simple->field_count);

    // Create values array (pointers to field values)
    int32_t  field_a = 0x12345678;
    uint16_t field_b = 0x3C00; // 1.0 in float16
    bool     field_c = true;

    const void* values[] = {&field_a, &field_b, &field_c};

    // Serialize
    uint8_t buffer[16] = {0};
    size_t  size       = dsdl_serialize(simple, values, sizeof(buffer), buffer);

    // Expected: 32 + 16 + 1 = 49 bits = 7 bytes
    TEST_ASSERT_EQUAL_size_t(7, size);

    // Verify int32 (little-endian): 0x78, 0x56, 0x34, 0x12
    TEST_ASSERT_EQUAL_UINT8(0x78, buffer[0]);
    TEST_ASSERT_EQUAL_UINT8(0x56, buffer[1]);
    TEST_ASSERT_EQUAL_UINT8(0x34, buffer[2]);
    TEST_ASSERT_EQUAL_UINT8(0x12, buffer[3]);

    // Verify float16: 0x00, 0x3C (little-endian)
    TEST_ASSERT_EQUAL_UINT8(0x00, buffer[4]);
    TEST_ASSERT_EQUAL_UINT8(0x3C, buffer[5]);

    // Verify bool: bit 0 of buffer[6] = 1
    TEST_ASSERT_EQUAL_UINT8(0x01, buffer[6]);

    // Deserialize
    int32_t  result_a = 0;
    uint16_t result_b = 0;
    bool     result_c = false;
    void*    result_values[] = {&result_a, &result_b, &result_c};

    size_t consumed = dsdl_deserialize(simple, result_values, size, buffer);
    TEST_ASSERT_EQUAL_size_t(7, consumed);
    TEST_ASSERT_EQUAL_INT32(0x12345678, result_a);
    TEST_ASSERT_EQUAL_UINT16(0x3C00, result_b);
    TEST_ASSERT_TRUE(result_c);

    teardown_dsdl();
}

void test_serialize_variable_array_struct(void)
{
    setup_dsdl();

    TEST_ASSERT_TRUE(dsdl_add_namespace(&g_dsdl,
                                        wkv_key("test_dsdl_root_namespaces/nunavut_test_types/nested_array_types")));

    // Load Inner.1.0: uint32[<=5] inner_items
    const dsdl_type_composite_t* inner = dsdl_read(&g_dsdl, wkv_key("mymsgs.Inner.1.0"));
    TEST_ASSERT_NOT_NULL(inner);
    TEST_ASSERT_EQUAL_size_t(1, inner->field_count);

    // The field type should be a variable array
    const dsdl_type_t* field_type = inner->field_types[0];
    TEST_ASSERT_EQUAL(DSDL_ARRAY_VARIABLE, *field_type);

    const dsdl_type_array_t* arr_type = (const dsdl_type_array_t*)field_type;
    TEST_ASSERT_EQUAL_size_t(5, arr_type->capacity);

    // Create a variable array value: { count, elements... }
    // For simplicity, we'll use a struct with count and inline elements
    struct {
        size_t   count;
        uint32_t elements[5];
    } array_value = {
        .count    = 3,
        .elements = {0x11111111, 0x22222222, 0x33333333, 0, 0}
    };

    // Values array points to the array struct
    const void* values[] = {&array_value};

    // Serialize
    uint8_t buffer[32] = {0};
    size_t  size       = dsdl_serialize(inner, values, sizeof(buffer), buffer);

    // Expected: 3 bits length prefix + 3 * 32 bits = 99 bits = 13 bytes
    TEST_ASSERT_EQUAL_size_t(13, size);

    // Length prefix: 3 encoded in 3 bits at bits 0-2
    // First element starts at bit 3
    // The first byte should have: bits 0-2 = 3 (length), bits 3-7 = low 5 bits of 0x11111111
    // 0x11111111 in binary: 00010001 00010001 00010001 00010001
    // Low 5 bits of 0x11 = 10001
    // So byte 0 = 011 (length=3) | 10001 (low 5 bits of first element byte) << 3
    // = 0b10001011 = 0x8B
    TEST_ASSERT_EQUAL_UINT8(0x8B, buffer[0]);

    teardown_dsdl();
}

void test_serialize_nested_struct(void)
{
    setup_dsdl();

    TEST_ASSERT_TRUE(dsdl_add_namespace(&g_dsdl,
                                        wkv_key("test_dsdl_root_namespaces/nunavut_test_types/nested_array_types")));

    // Load Outer.1.0: float32[<=8] outer_items, Inner.1.0 inner
    const dsdl_type_composite_t* outer = dsdl_read(&g_dsdl, wkv_key("mymsgs.Outer.1.0"));
    TEST_ASSERT_NOT_NULL(outer);
    TEST_ASSERT_EQUAL_size_t(2, outer->field_count);

    // First field: float32[<=8]
    struct {
        size_t count;
        float  elements[8];
    } outer_items = {
        .count    = 2,
        .elements = {1.0f, 2.0f, 0, 0, 0, 0, 0, 0}
    };

    // Second field: Inner.1.0 (uint32[<=5])
    struct {
        size_t   count;
        uint32_t elements[5];
    } inner_items = {
        .count    = 1,
        .elements = {0xDEADBEEF, 0, 0, 0, 0}
    };
    const void* inner_values[] = {&inner_items};

    // Outer values
    const void* values[] = {&outer_items, inner_values};

    // Serialize
    uint8_t buffer[64] = {0};
    size_t  size       = dsdl_serialize(outer, values, sizeof(buffer), buffer);

    // Expected size:
    // float32[<=8] with 2 elements: 4 prefix bits + 2*32 = 68 bits
    // Inner.1.0 (uint32[<=5] with 1 element): 3 prefix bits + 1*32 = 35 bits
    // Total: 68 + 35 = 103 bits = 13 bytes
    TEST_ASSERT_EQUAL_size_t(13, size);

    teardown_dsdl();
}

void test_roundtrip_simple_struct(void)
{
    setup_dsdl();

    TEST_ASSERT_TRUE(dsdl_add_namespace(&g_dsdl,
                                        wkv_key("test_dsdl_root_namespaces/nunavut_test_types/nested_array_types")));

    const dsdl_type_composite_t* simple = dsdl_read(&g_dsdl, wkv_key("mymsgs.Simple.1.0"));
    TEST_ASSERT_NOT_NULL(simple);

    // Original values: int32 a, float16 b, bool c
    int32_t  orig_a = -123456;
    uint16_t orig_b = 0x4248; // ~3.14 in float16
    bool     orig_c = true;

    const void* orig_values[] = {&orig_a, &orig_b, &orig_c};

    // Serialize
    uint8_t buffer[16] = {0};
    size_t  size       = dsdl_serialize(simple, orig_values, sizeof(buffer), buffer);
    TEST_ASSERT_EQUAL_size_t(7, size);

    // Deserialize into new values
    int32_t  result_a = 0;
    uint16_t result_b = 0;
    bool     result_c = false;
    void*    result_values[] = {&result_a, &result_b, &result_c};

    size_t consumed = dsdl_deserialize(simple, result_values, size, buffer);
    TEST_ASSERT_EQUAL_size_t(7, consumed);

    // Verify roundtrip
    TEST_ASSERT_EQUAL_INT32(orig_a, result_a);
    TEST_ASSERT_EQUAL_UINT16(orig_b, result_b);
    TEST_ASSERT_EQUAL(orig_c, result_c);

    teardown_dsdl();
}

void test_roundtrip_variable_array(void)
{
    setup_dsdl();

    TEST_ASSERT_TRUE(dsdl_add_namespace(&g_dsdl,
                                        wkv_key("test_dsdl_root_namespaces/nunavut_test_types/nested_array_types")));

    const dsdl_type_composite_t* inner = dsdl_read(&g_dsdl, wkv_key("mymsgs.Inner.1.0"));
    TEST_ASSERT_NOT_NULL(inner);

    // Original: uint32[<=5] with 3 elements
    struct {
        size_t   count;
        uint32_t elements[5];
    } orig_array = {
        .count    = 3,
        .elements = {0xAAAAAAAA, 0xBBBBBBBB, 0xCCCCCCCC, 0, 0}
    };

    const void* orig_values[] = {&orig_array};

    // Serialize
    uint8_t buffer[32] = {0};
    size_t  size       = dsdl_serialize(inner, orig_values, sizeof(buffer), buffer);
    // 3 prefix bits + 3*32 = 99 bits = 13 bytes
    TEST_ASSERT_EQUAL_size_t(13, size);

    // Deserialize
    struct {
        size_t   count;
        uint32_t elements[5];
    } result_array = {0};
    void* result_values[] = {&result_array};

    size_t consumed = dsdl_deserialize(inner, result_values, size, buffer);
    TEST_ASSERT_EQUAL_size_t(13, consumed);

    // Verify
    TEST_ASSERT_EQUAL_size_t(3, result_array.count);
    TEST_ASSERT_EQUAL_UINT32(0xAAAAAAAA, result_array.elements[0]);
    TEST_ASSERT_EQUAL_UINT32(0xBBBBBBBB, result_array.elements[1]);
    TEST_ASSERT_EQUAL_UINT32(0xCCCCCCCC, result_array.elements[2]);

    teardown_dsdl();
}

void test_roundtrip_nested_struct(void)
{
    setup_dsdl();

    TEST_ASSERT_TRUE(dsdl_add_namespace(&g_dsdl,
                                        wkv_key("test_dsdl_root_namespaces/nunavut_test_types/nested_array_types")));

    const dsdl_type_composite_t* outer = dsdl_read(&g_dsdl, wkv_key("mymsgs.Outer.1.0"));
    TEST_ASSERT_NOT_NULL(outer);

    // Original values
    struct {
        size_t count;
        float  elements[8];
    } orig_outer_items = {
        .count    = 3,
        .elements = {1.5f, 2.5f, 3.5f, 0, 0, 0, 0, 0}
    };

    struct {
        size_t   count;
        uint32_t elements[5];
    } orig_inner_items = {
        .count    = 2,
        .elements = {0x12345678, 0x9ABCDEF0, 0, 0, 0}
    };
    const void* orig_inner_values[] = {&orig_inner_items};
    const void* orig_values[]       = {&orig_outer_items, orig_inner_values};

    // Serialize
    uint8_t buffer[64] = {0};
    size_t  size       = dsdl_serialize(outer, orig_values, sizeof(buffer), buffer);
    // float32[<=8] with 3 elements: 4 + 96 = 100 bits
    // Inner (uint32[<=5] with 2 elements): 3 + 64 = 67 bits
    // Total: 167 bits = 21 bytes
    TEST_ASSERT_EQUAL_size_t(21, size);

    // Deserialize
    struct {
        size_t count;
        float  elements[8];
    } result_outer_items = {0};

    struct {
        size_t   count;
        uint32_t elements[5];
    } result_inner_items = {0};
    void* result_inner_values[] = {&result_inner_items};
    void* result_values[]       = {&result_outer_items, result_inner_values};

    size_t consumed = dsdl_deserialize(outer, result_values, size, buffer);
    TEST_ASSERT_EQUAL_size_t(21, consumed);

    // Verify outer array
    TEST_ASSERT_EQUAL_size_t(3, result_outer_items.count);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 1.5f, result_outer_items.elements[0]);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 2.5f, result_outer_items.elements[1]);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 3.5f, result_outer_items.elements[2]);

    // Verify inner array
    TEST_ASSERT_EQUAL_size_t(2, result_inner_items.count);
    TEST_ASSERT_EQUAL_UINT32(0x12345678, result_inner_items.elements[0]);
    TEST_ASSERT_EQUAL_UINT32(0x9ABCDEF0, result_inner_items.elements[1]);

    teardown_dsdl();
}

// ============================================================================
// Union tests
// ============================================================================

void test_serialize_union(void)
{
    // Create a simple union type manually for testing
    // Union with 2 fields: uint8 a, uint32 b
    // Tag requires ceil(log2(2)) = 1 bit

    // Allocate type descriptor
    static dsdl_type_t field_a_type = DSDL_UINT8;
    static dsdl_type_t field_b_type = DSDL_UINT32;

    static dsdl_type_t* field_types[2];
    field_types[0] = &field_a_type;
    field_types[1] = &field_b_type;

    static wkv_str_t field_names[2] = {
        {1, "a"},
        {1, "b"}
    };

    dsdl_type_composite_t union_type = {
        .type        = DSDL_COMPOSITE_UNION,
        .name        = {10, "TestUnion"},
        .version     = {1, 0},
        .extent      = 8,
        .sealed      = true,
        .field_count = 2,
        .field_names = field_names,
        .field_types = field_types,
    };

    // Test case 1: Select variant 0 (uint8)
    {
        size_t  tag_0   = 0;
        uint8_t value_0 = 0xAB;
        const void* values_0[] = {&tag_0, &value_0};

        uint8_t buffer[8] = {0};
        size_t  size      = dsdl_serialize(&union_type, values_0, sizeof(buffer), buffer);

        // 1 tag bit + 8 value bits = 9 bits = 2 bytes
        TEST_ASSERT_EQUAL_size_t(2, size);

        // Tag 0 (bit 0) + value 0xAB (bits 1-8)
        // Byte 0: bit0=tag(0), bits1-7=0xAB[0:6] = 0b01010110 = 0x56
        // Byte 1: bit0=0xAB[7] = 1
        TEST_ASSERT_EQUAL_UINT8(0x56, buffer[0]); // 0xAB << 1 = 0x156, low byte = 0x56
        TEST_ASSERT_EQUAL_UINT8(0x01, buffer[1]); // High bit of 0xAB

        // Deserialize
        size_t  result_tag   = 99;
        uint8_t result_value = 0;
        void*   result_values[] = {&result_tag, &result_value};

        size_t consumed = dsdl_deserialize(&union_type, result_values, size, buffer);
        TEST_ASSERT_EQUAL_size_t(2, consumed);
        TEST_ASSERT_EQUAL_size_t(0, result_tag);
        TEST_ASSERT_EQUAL_UINT8(0xAB, result_value);
    }

    // Test case 2: Select variant 1 (uint32)
    {
        size_t   tag_1   = 1;
        uint32_t value_1 = 0x12345678;
        const void* values_1[] = {&tag_1, &value_1};

        uint8_t buffer[8] = {0};
        size_t  size      = dsdl_serialize(&union_type, values_1, sizeof(buffer), buffer);

        // 1 tag bit + 32 value bits = 33 bits = 5 bytes
        TEST_ASSERT_EQUAL_size_t(5, size);

        // Deserialize
        size_t   result_tag   = 99;
        uint32_t result_value = 0;
        void*    result_values[] = {&result_tag, &result_value};

        size_t consumed = dsdl_deserialize(&union_type, result_values, size, buffer);
        TEST_ASSERT_EQUAL_size_t(5, consumed);
        TEST_ASSERT_EQUAL_size_t(1, result_tag);
        TEST_ASSERT_EQUAL_UINT32(0x12345678, result_value);
    }
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
    RUN_TEST(test_bitbuf_implicit_zero_extension);

    // Primitive serialization tests
    RUN_TEST(test_serialize_uint8);
    RUN_TEST(test_serialize_uint32);
    RUN_TEST(test_serialize_int16_negative);
    RUN_TEST(test_serialize_bool);
    RUN_TEST(test_serialize_float32);

    // Integration tests
    RUN_TEST(test_serialize_simple_struct);
    RUN_TEST(test_serialize_variable_array_struct);
    RUN_TEST(test_serialize_nested_struct);

    // Roundtrip tests
    RUN_TEST(test_roundtrip_simple_struct);
    RUN_TEST(test_roundtrip_variable_array);
    RUN_TEST(test_roundtrip_nested_struct);

    // Union tests
    RUN_TEST(test_serialize_union);

    return UNITY_END();
}
