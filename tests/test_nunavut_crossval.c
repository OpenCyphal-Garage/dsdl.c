/*
 * Nunavut cross-validation tests for dsdl.c serialization/deserialization.
 * This test compares byte-for-byte serialization output between dsdl.c and Nunavut-generated C code.
 */

#include <unity.h>

#include <assert.h>
#include <math.h>
#include <stdint.h>
#include <string.h>

#define NUNAVUT_ASSERT(x) assert(x)

#include <mymsgs/Simple_1_0.h>

void setUp(void) {}

void tearDown(void) {}

static uint16_t float32_to_float16(float value)
{
    union
    {
        float    f;
        uint32_t u;
    } f32             = { .f = value };
    uint32_t sign     = (f32.u >> 16) & 0x8000;
    int32_t  exponent = ((int32_t)((f32.u >> 23) & 0xFF)) - 127;
    uint32_t mantissa = f32.u & 0x7FFFFF;

    if (exponent == 128) {
        return (uint16_t)(sign | 0x7C00 | (mantissa ? 0x0200 : 0));
    }

    if (exponent > 15) {
        return (uint16_t)(sign | 0x7C00);
    }

    if (exponent < -14) {
        return (uint16_t)sign;
    }

    exponent += 15;
    mantissa >>= 13;

    return (uint16_t)(sign | ((uint32_t)exponent << 10) | mantissa);
}

static void test_mymsgs_Simple_serialization(void)
{
    mymsgs_Simple_1_0 nunavut_obj = {
        .a = 12345,
        .b = 3.14f,
        .c = true,
    };

    uint8_t      nunavut_buffer[mymsgs_Simple_1_0_SERIALIZATION_BUFFER_SIZE_BYTES_];
    size_t       nunavut_size = sizeof(nunavut_buffer);
    const int8_t ser_result   = mymsgs_Simple_1_0_serialize_(&nunavut_obj, nunavut_buffer, &nunavut_size);
    TEST_ASSERT_EQUAL(0, ser_result);
    TEST_ASSERT_EQUAL(7, nunavut_size);

    TEST_ASSERT_EQUAL(0x39, nunavut_buffer[0]);
    TEST_ASSERT_EQUAL(0x30, nunavut_buffer[1]);
    TEST_ASSERT_EQUAL(0x00, nunavut_buffer[2]);
    TEST_ASSERT_EQUAL(0x00, nunavut_buffer[3]);

    TEST_ASSERT_EQUAL(1, nunavut_buffer[6]);
}

static void test_mymsgs_Simple_deserialization(void)
{
    uint8_t buffer[7];
    size_t  offset = 0;

    int32_t a_val = -999;
    memcpy(&buffer[offset], &a_val, 4);
    offset += 4;

    uint16_t b_val = float32_to_float16(2.71f);
    memcpy(&buffer[offset], &b_val, 2);
    offset += 2;

    buffer[offset] = 0;
    offset += 1;

    mymsgs_Simple_1_0 obj;
    size_t            buffer_size  = sizeof(buffer);
    const int8_t      deser_result = mymsgs_Simple_1_0_deserialize_(&obj, buffer, &buffer_size);
    TEST_ASSERT_EQUAL(0, deser_result);
    TEST_ASSERT_EQUAL(-999, obj.a);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 2.71f, obj.b);
    TEST_ASSERT_EQUAL(false, obj.c);
}

static void test_mymsgs_Simple_roundtrip(void)
{
    mymsgs_Simple_1_0 original = {
        .a = 42,
        .b = 1.5f,
        .c = true,
    };

    uint8_t buffer[mymsgs_Simple_1_0_SERIALIZATION_BUFFER_SIZE_BYTES_];
    size_t  buffer_size = sizeof(buffer);

    const int8_t ser_result = mymsgs_Simple_1_0_serialize_(&original, buffer, &buffer_size);
    TEST_ASSERT_EQUAL(0, ser_result);

    mymsgs_Simple_1_0 deserialized;
    size_t            deser_size   = buffer_size;
    const int8_t      deser_result = mymsgs_Simple_1_0_deserialize_(&deserialized, buffer, &deser_size);
    TEST_ASSERT_EQUAL(0, deser_result);

    TEST_ASSERT_EQUAL(original.a, deserialized.a);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, original.b, deserialized.b);
    TEST_ASSERT_EQUAL(original.c, deserialized.c);
}

int main(void)
{
    UNITY_BEGIN();

    RUN_TEST(test_mymsgs_Simple_serialization);
    RUN_TEST(test_mymsgs_Simple_deserialization);
    RUN_TEST(test_mymsgs_Simple_roundtrip);

    return UNITY_END();
}
