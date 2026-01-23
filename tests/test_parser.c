/// Tests for DSDL parser
///
/// This file tests the parser functionality including literals, expressions,
/// types, and statements.

#include "unity.h"

#include <stdlib.h>
#include <string.h>

// Include the implementation directly for internal access
#include "dsdl.c"

// ============================================================================
// Test helpers
// ============================================================================

static void* test_realloc(dsdl_t* self, void* ptr, size_t size)
{
    (void)self;
    if (size == 0) {
        free(ptr);
        return NULL;
    }
    return realloc(ptr, size);
}

static dsdl_t        g_dsdl;
static dsdl_parser_t g_parser;

static void init_parser(const char* input)
{
    g_dsdl.realloc = test_realloc;
    dsdl_parser_init_(&g_parser, &g_dsdl, input, strlen(input));
}

// ============================================================================
// Integer literal tests
// ============================================================================

static void test_parse_int_decimal(void)
{
    init_parser("42");
    dsdl_rational_t r = dsdl_parse_integer_(&g_parser);
    TEST_ASSERT_EQUAL_INT64(42, r.num);
    TEST_ASSERT_EQUAL_UINT64(1, r.den);
}

static void test_parse_int_decimal_with_underscores(void)
{
    init_parser("1_000_000");
    dsdl_rational_t r = dsdl_parse_integer_(&g_parser);
    TEST_ASSERT_EQUAL_INT64(1000000, r.num);
    TEST_ASSERT_EQUAL_UINT64(1, r.den);
}

static void test_parse_int_binary(void)
{
    init_parser("0b1010");
    dsdl_rational_t r = dsdl_parse_integer_(&g_parser);
    TEST_ASSERT_EQUAL_INT64(10, r.num);
    TEST_ASSERT_EQUAL_UINT64(1, r.den);
}

static void test_parse_int_binary_uppercase(void)
{
    init_parser("0B1111_0000");
    dsdl_rational_t r = dsdl_parse_integer_(&g_parser);
    TEST_ASSERT_EQUAL_INT64(0xF0, r.num);
    TEST_ASSERT_EQUAL_UINT64(1, r.den);
}

static void test_parse_int_octal(void)
{
    init_parser("0o755");
    dsdl_rational_t r = dsdl_parse_integer_(&g_parser);
    TEST_ASSERT_EQUAL_INT64(0755, r.num);
    TEST_ASSERT_EQUAL_UINT64(1, r.den);
}

static void test_parse_int_hex(void)
{
    init_parser("0xFF");
    dsdl_rational_t r = dsdl_parse_integer_(&g_parser);
    TEST_ASSERT_EQUAL_INT64(255, r.num);
    TEST_ASSERT_EQUAL_UINT64(1, r.den);
}

static void test_parse_int_hex_mixed_case(void)
{
    init_parser("0xDEAD_beef");
    dsdl_rational_t r = dsdl_parse_integer_(&g_parser);
    TEST_ASSERT_EQUAL_INT64(0xDEADBEEF, r.num);
    TEST_ASSERT_EQUAL_UINT64(1, r.den);
}

static void test_parse_int_zero(void)
{
    init_parser("0");
    dsdl_rational_t r = dsdl_parse_integer_(&g_parser);
    TEST_ASSERT_EQUAL_INT64(0, r.num);
    TEST_ASSERT_EQUAL_UINT64(1, r.den);
}

// ============================================================================
// Real literal tests
// ============================================================================

static void test_parse_real_simple(void)
{
    init_parser("3.14");
    dsdl_rational_t r = dsdl_parse_real_(&g_parser);
    // 3.14 = 314/100 = 157/50
    TEST_ASSERT_EQUAL_INT64(157, r.num);
    TEST_ASSERT_EQUAL_UINT64(50, r.den);
}

static void test_parse_real_trailing_dot(void)
{
    init_parser("42.");
    dsdl_rational_t r = dsdl_parse_real_(&g_parser);
    TEST_ASSERT_EQUAL_INT64(42, r.num);
    TEST_ASSERT_EQUAL_UINT64(1, r.den);
}

static void test_parse_real_leading_dot(void)
{
    init_parser(".5");
    dsdl_rational_t r = dsdl_parse_real_(&g_parser);
    // 0.5 = 5/10 = 1/2
    TEST_ASSERT_EQUAL_INT64(1, r.num);
    TEST_ASSERT_EQUAL_UINT64(2, r.den);
}

static void test_parse_real_exponent(void)
{
    init_parser("1e3");
    dsdl_rational_t r = dsdl_parse_real_(&g_parser);
    TEST_ASSERT_EQUAL_INT64(1000, r.num);
    TEST_ASSERT_EQUAL_UINT64(1, r.den);
}

static void test_parse_real_exponent_negative(void)
{
    init_parser("5e-2");
    dsdl_rational_t r = dsdl_parse_real_(&g_parser);
    // 5e-2 = 5/100 = 1/20
    TEST_ASSERT_EQUAL_INT64(1, r.num);
    TEST_ASSERT_EQUAL_UINT64(20, r.den);
}

static void test_parse_real_full(void)
{
    init_parser("1.5e2");
    dsdl_rational_t r = dsdl_parse_real_(&g_parser);
    // 1.5e2 = 150
    TEST_ASSERT_EQUAL_INT64(150, r.num);
    TEST_ASSERT_EQUAL_UINT64(1, r.den);
}

// ============================================================================
// Boolean literal tests
// ============================================================================

static void test_parse_boolean_true(void)
{
    init_parser("true");
    bool val = false;
    TEST_ASSERT_TRUE(dsdl_parse_boolean_(&g_parser, &val));
    TEST_ASSERT_TRUE(val);
}

static void test_parse_boolean_false(void)
{
    init_parser("false");
    bool val = true;
    TEST_ASSERT_TRUE(dsdl_parse_boolean_(&g_parser, &val));
    TEST_ASSERT_FALSE(val);
}

static void test_parse_boolean_not_keyword(void)
{
    // "trueX" should not parse as boolean
    init_parser("trueX");
    bool val = false;
    TEST_ASSERT_FALSE(dsdl_parse_boolean_(&g_parser, &val));
}

// ============================================================================
// String literal tests
// ============================================================================

static void test_parse_string_double_quoted(void)
{
    init_parser("\"hello\"");
    wkv_str_t s = dsdl_parse_string_(&g_parser);
    TEST_ASSERT_NOT_NULL(s.str);
    TEST_ASSERT_EQUAL_size_t(5, s.len);
    TEST_ASSERT_EQUAL_MEMORY("hello", s.str, 5);
}

static void test_parse_string_single_quoted(void)
{
    init_parser("'world'");
    wkv_str_t s = dsdl_parse_string_(&g_parser);
    TEST_ASSERT_NOT_NULL(s.str);
    TEST_ASSERT_EQUAL_size_t(5, s.len);
    TEST_ASSERT_EQUAL_MEMORY("world", s.str, 5);
}

static void test_parse_string_with_escape(void)
{
    init_parser("\"line1\\nline2\"");
    wkv_str_t s = dsdl_parse_string_(&g_parser);
    TEST_ASSERT_NOT_NULL(s.str);
    // The raw content includes the backslash (escapes are processed later)
    TEST_ASSERT_EQUAL_size_t(12, s.len);
}

static void test_parse_string_empty(void)
{
    init_parser("\"\"");
    wkv_str_t s = dsdl_parse_string_(&g_parser);
    TEST_ASSERT_NOT_NULL(s.str);
    TEST_ASSERT_EQUAL_size_t(0, s.len);
}

// ============================================================================
// Identifier tests
// ============================================================================

static void test_parse_identifier_simple(void)
{
    init_parser("foo");
    wkv_str_t id = dsdl_parse_identifier_(&g_parser);
    TEST_ASSERT_NOT_NULL(id.str);
    TEST_ASSERT_EQUAL_size_t(3, id.len);
    TEST_ASSERT_EQUAL_MEMORY("foo", id.str, 3);
}

static void test_parse_identifier_with_underscore(void)
{
    init_parser("_my_var123");
    wkv_str_t id = dsdl_parse_identifier_(&g_parser);
    TEST_ASSERT_NOT_NULL(id.str);
    TEST_ASSERT_EQUAL_size_t(10, id.len);
}

static void test_parse_identifier_starting_with_digit(void)
{
    init_parser("123abc");
    wkv_str_t id = dsdl_parse_identifier_(&g_parser);
    TEST_ASSERT_NULL(id.str);
}

// ============================================================================
// Expression tests
// ============================================================================

static void test_parse_expr_simple_integer(void)
{
    init_parser("42");
    dsdl_value_t val;
    TEST_ASSERT_TRUE(dsdl_parse_expression_(&g_parser, &val));
    TEST_ASSERT_EQUAL(dsdl_value_rational, val.kind);
    TEST_ASSERT_EQUAL_INT64(42, val.as.rational.num);
}

static void test_parse_expr_addition(void)
{
    init_parser("2 + 3");
    dsdl_value_t val;
    TEST_ASSERT_TRUE(dsdl_parse_expression_(&g_parser, &val));
    TEST_ASSERT_EQUAL(dsdl_value_rational, val.kind);
    TEST_ASSERT_EQUAL_INT64(5, val.as.rational.num);
}

static void test_parse_expr_subtraction(void)
{
    init_parser("10 - 4");
    dsdl_value_t val;
    TEST_ASSERT_TRUE(dsdl_parse_expression_(&g_parser, &val));
    TEST_ASSERT_EQUAL(dsdl_value_rational, val.kind);
    TEST_ASSERT_EQUAL_INT64(6, val.as.rational.num);
}

static void test_parse_expr_multiplication(void)
{
    init_parser("6 * 7");
    dsdl_value_t val;
    TEST_ASSERT_TRUE(dsdl_parse_expression_(&g_parser, &val));
    TEST_ASSERT_EQUAL(dsdl_value_rational, val.kind);
    TEST_ASSERT_EQUAL_INT64(42, val.as.rational.num);
}

static void test_parse_expr_division(void)
{
    init_parser("15 / 3");
    dsdl_value_t val;
    TEST_ASSERT_TRUE(dsdl_parse_expression_(&g_parser, &val));
    TEST_ASSERT_EQUAL(dsdl_value_rational, val.kind);
    TEST_ASSERT_EQUAL_INT64(5, val.as.rational.num);
    TEST_ASSERT_EQUAL_UINT64(1, val.as.rational.den);
}

static void test_parse_expr_division_fraction(void)
{
    init_parser("1 / 3");
    dsdl_value_t val;
    TEST_ASSERT_TRUE(dsdl_parse_expression_(&g_parser, &val));
    TEST_ASSERT_EQUAL(dsdl_value_rational, val.kind);
    TEST_ASSERT_EQUAL_INT64(1, val.as.rational.num);
    TEST_ASSERT_EQUAL_UINT64(3, val.as.rational.den);
}

static void test_parse_expr_modulo(void)
{
    init_parser("17 % 5");
    dsdl_value_t val;
    TEST_ASSERT_TRUE(dsdl_parse_expression_(&g_parser, &val));
    TEST_ASSERT_EQUAL(dsdl_value_rational, val.kind);
    TEST_ASSERT_EQUAL_INT64(2, val.as.rational.num);
}

static void test_parse_expr_power(void)
{
    init_parser("2 ** 10");
    dsdl_value_t val;
    TEST_ASSERT_TRUE(dsdl_parse_expression_(&g_parser, &val));
    TEST_ASSERT_EQUAL(dsdl_value_rational, val.kind);
    TEST_ASSERT_EQUAL_INT64(1024, val.as.rational.num);
}

static void test_parse_expr_precedence(void)
{
    // 2 + 3 * 4 = 2 + 12 = 14
    init_parser("2 + 3 * 4");
    dsdl_value_t val;
    TEST_ASSERT_TRUE(dsdl_parse_expression_(&g_parser, &val));
    TEST_ASSERT_EQUAL(dsdl_value_rational, val.kind);
    TEST_ASSERT_EQUAL_INT64(14, val.as.rational.num);
}

static void test_parse_expr_parentheses(void)
{
    // (2 + 3) * 4 = 5 * 4 = 20
    init_parser("(2 + 3) * 4");
    dsdl_value_t val;
    TEST_ASSERT_TRUE(dsdl_parse_expression_(&g_parser, &val));
    TEST_ASSERT_EQUAL(dsdl_value_rational, val.kind);
    TEST_ASSERT_EQUAL_INT64(20, val.as.rational.num);
}

static void test_parse_expr_unary_minus(void)
{
    init_parser("-5");
    dsdl_value_t val;
    TEST_ASSERT_TRUE(dsdl_parse_expression_(&g_parser, &val));
    TEST_ASSERT_EQUAL(dsdl_value_rational, val.kind);
    TEST_ASSERT_EQUAL_INT64(-5, val.as.rational.num);
}

static void test_parse_expr_unary_plus(void)
{
    init_parser("+42");
    dsdl_value_t val;
    TEST_ASSERT_TRUE(dsdl_parse_expression_(&g_parser, &val));
    TEST_ASSERT_EQUAL(dsdl_value_rational, val.kind);
    TEST_ASSERT_EQUAL_INT64(42, val.as.rational.num);
}

static void test_parse_expr_bitwise_or(void)
{
    init_parser("0b1010 | 0b0101");
    dsdl_value_t val;
    TEST_ASSERT_TRUE(dsdl_parse_expression_(&g_parser, &val));
    TEST_ASSERT_EQUAL(dsdl_value_rational, val.kind);
    TEST_ASSERT_EQUAL_INT64(0xF, val.as.rational.num);
}

static void test_parse_expr_bitwise_and(void)
{
    init_parser("0xFF & 0x0F");
    dsdl_value_t val;
    TEST_ASSERT_TRUE(dsdl_parse_expression_(&g_parser, &val));
    TEST_ASSERT_EQUAL(dsdl_value_rational, val.kind);
    TEST_ASSERT_EQUAL_INT64(0x0F, val.as.rational.num);
}

static void test_parse_expr_bitwise_xor(void)
{
    init_parser("0b1111 ^ 0b1010");
    dsdl_value_t val;
    TEST_ASSERT_TRUE(dsdl_parse_expression_(&g_parser, &val));
    TEST_ASSERT_EQUAL(dsdl_value_rational, val.kind);
    TEST_ASSERT_EQUAL_INT64(5, val.as.rational.num); // 0b0101 = 5
}

static void test_parse_expr_comparison_eq(void)
{
    init_parser("5 == 5");
    dsdl_value_t val;
    TEST_ASSERT_TRUE(dsdl_parse_expression_(&g_parser, &val));
    TEST_ASSERT_EQUAL(dsdl_value_bool, val.kind);
    TEST_ASSERT_TRUE(val.as.boolean);
}

static void test_parse_expr_comparison_ne(void)
{
    init_parser("5 != 3");
    dsdl_value_t val;
    TEST_ASSERT_TRUE(dsdl_parse_expression_(&g_parser, &val));
    TEST_ASSERT_EQUAL(dsdl_value_bool, val.kind);
    TEST_ASSERT_TRUE(val.as.boolean);
}

static void test_parse_expr_comparison_lt(void)
{
    init_parser("3 < 5");
    dsdl_value_t val;
    TEST_ASSERT_TRUE(dsdl_parse_expression_(&g_parser, &val));
    TEST_ASSERT_EQUAL(dsdl_value_bool, val.kind);
    TEST_ASSERT_TRUE(val.as.boolean);
}

static void test_parse_expr_comparison_le(void)
{
    init_parser("5 <= 5");
    dsdl_value_t val;
    TEST_ASSERT_TRUE(dsdl_parse_expression_(&g_parser, &val));
    TEST_ASSERT_EQUAL(dsdl_value_bool, val.kind);
    TEST_ASSERT_TRUE(val.as.boolean);
}

static void test_parse_expr_comparison_gt(void)
{
    init_parser("5 > 3");
    dsdl_value_t val;
    TEST_ASSERT_TRUE(dsdl_parse_expression_(&g_parser, &val));
    TEST_ASSERT_EQUAL(dsdl_value_bool, val.kind);
    TEST_ASSERT_TRUE(val.as.boolean);
}

static void test_parse_expr_comparison_ge(void)
{
    init_parser("5 >= 5");
    dsdl_value_t val;
    TEST_ASSERT_TRUE(dsdl_parse_expression_(&g_parser, &val));
    TEST_ASSERT_EQUAL(dsdl_value_bool, val.kind);
    TEST_ASSERT_TRUE(val.as.boolean);
}

static void test_parse_expr_logical_and(void)
{
    init_parser("true && false");
    dsdl_value_t val;
    TEST_ASSERT_TRUE(dsdl_parse_expression_(&g_parser, &val));
    TEST_ASSERT_EQUAL(dsdl_value_bool, val.kind);
    TEST_ASSERT_FALSE(val.as.boolean);
}

static void test_parse_expr_logical_or(void)
{
    init_parser("true || false");
    dsdl_value_t val;
    TEST_ASSERT_TRUE(dsdl_parse_expression_(&g_parser, &val));
    TEST_ASSERT_EQUAL(dsdl_value_bool, val.kind);
    TEST_ASSERT_TRUE(val.as.boolean);
}

static void test_parse_expr_logical_not(void)
{
    init_parser("!false");
    dsdl_value_t val;
    TEST_ASSERT_TRUE(dsdl_parse_expression_(&g_parser, &val));
    TEST_ASSERT_EQUAL(dsdl_value_bool, val.kind);
    TEST_ASSERT_TRUE(val.as.boolean);
}

static void test_parse_expr_complex(void)
{
    // 3 < 5 && 5 < 7
    init_parser("3 < 5 && 5 < 7");
    dsdl_value_t val;
    TEST_ASSERT_TRUE(dsdl_parse_expression_(&g_parser, &val));
    TEST_ASSERT_EQUAL(dsdl_value_bool, val.kind);
    TEST_ASSERT_TRUE(val.as.boolean);
}

static void test_parse_expr_power_associativity(void)
{
    // 2 ** 3 ** 2 = 2 ** 9 = 512 (right-associative)
    init_parser("2 ** 3 ** 2");
    dsdl_value_t val;
    TEST_ASSERT_TRUE(dsdl_parse_expression_(&g_parser, &val));
    TEST_ASSERT_EQUAL(dsdl_value_rational, val.kind);
    TEST_ASSERT_EQUAL_INT64(512, val.as.rational.num);
}

// ============================================================================
// Set literal tests
// ============================================================================

static void test_parse_set_empty(void)
{
    init_parser("{}");
    dsdl_value_t val;
    TEST_ASSERT_TRUE(dsdl_parse_literal_(&g_parser, &val));
    TEST_ASSERT_EQUAL(dsdl_value_set, val.kind);
    TEST_ASSERT_EQUAL_size_t(0, val.as.set.count);
}

static void test_parse_set_single(void)
{
    init_parser("{42}");
    dsdl_value_t val;
    TEST_ASSERT_TRUE(dsdl_parse_literal_(&g_parser, &val));
    TEST_ASSERT_EQUAL(dsdl_value_set, val.kind);
    TEST_ASSERT_EQUAL_size_t(1, val.as.set.count);
    TEST_ASSERT_EQUAL(dsdl_value_rational, val.as.set.elements[0].kind);
    TEST_ASSERT_EQUAL_INT64(42, val.as.set.elements[0].as.rational.num);
    dsdl_free_(&g_dsdl, val.as.set.elements);
}

static void test_parse_set_multiple(void)
{
    init_parser("{1, 2, 3}");
    dsdl_value_t val;
    TEST_ASSERT_TRUE(dsdl_parse_literal_(&g_parser, &val));
    TEST_ASSERT_EQUAL(dsdl_value_set, val.kind);
    TEST_ASSERT_EQUAL_size_t(3, val.as.set.count);
    TEST_ASSERT_EQUAL_INT64(1, val.as.set.elements[0].as.rational.num);
    TEST_ASSERT_EQUAL_INT64(2, val.as.set.elements[1].as.rational.num);
    TEST_ASSERT_EQUAL_INT64(3, val.as.set.elements[2].as.rational.num);
    dsdl_free_(&g_dsdl, val.as.set.elements);
}

// ============================================================================
// Type parsing tests
// ============================================================================

static void test_parse_type_void(void)
{
    init_parser("void8");
    dsdl_parsed_type_t type;
    TEST_ASSERT_TRUE(dsdl_parse_type_(&g_parser, &type));
    TEST_ASSERT_TRUE(dsdl_type_is_void(type.kind));
    TEST_ASSERT_EQUAL_UINT8(8, type.bit_width);
}

static void test_parse_type_void_various(void)
{
    init_parser("void1");
    dsdl_parsed_type_t type;
    TEST_ASSERT_TRUE(dsdl_parse_type_(&g_parser, &type));
    TEST_ASSERT_EQUAL_UINT8(1, type.bit_width);

    init_parser("void64");
    TEST_ASSERT_TRUE(dsdl_parse_type_(&g_parser, &type));
    TEST_ASSERT_EQUAL_UINT8(64, type.bit_width);
}

static void test_parse_type_uint(void)
{
    init_parser("uint8");
    dsdl_parsed_type_t type;
    TEST_ASSERT_TRUE(dsdl_parse_type_(&g_parser, &type));
    TEST_ASSERT_TRUE(dsdl_type_is_uint(type.kind));
    TEST_ASSERT_EQUAL_UINT8(8, type.bit_width);
    TEST_ASSERT_TRUE(type.is_saturated);
}

static void test_parse_type_int(void)
{
    init_parser("int32");
    dsdl_parsed_type_t type;
    TEST_ASSERT_TRUE(dsdl_parse_type_(&g_parser, &type));
    TEST_ASSERT_TRUE(dsdl_type_is_int(type.kind));
    TEST_ASSERT_EQUAL_UINT8(32, type.bit_width);
}

static void test_parse_type_float(void)
{
    init_parser("float32");
    dsdl_parsed_type_t type;
    TEST_ASSERT_TRUE(dsdl_parse_type_(&g_parser, &type));
    TEST_ASSERT_TRUE(dsdl_type_is_float(type.kind));
    TEST_ASSERT_EQUAL_UINT8(32, type.bit_width);

    init_parser("float64");
    TEST_ASSERT_TRUE(dsdl_parse_type_(&g_parser, &type));
    TEST_ASSERT_TRUE(dsdl_type_is_float(type.kind));
    TEST_ASSERT_EQUAL_UINT8(64, type.bit_width);

    init_parser("float16");
    TEST_ASSERT_TRUE(dsdl_parse_type_(&g_parser, &type));
    TEST_ASSERT_TRUE(dsdl_type_is_float(type.kind));
    TEST_ASSERT_EQUAL_UINT8(16, type.bit_width);
}

static void test_parse_type_bool(void)
{
    init_parser("bool");
    dsdl_parsed_type_t type;
    TEST_ASSERT_TRUE(dsdl_parse_type_(&g_parser, &type));
    TEST_ASSERT_TRUE(dsdl_type_is_alias(type.kind));
    TEST_ASSERT_EQUAL_UINT8(1, type.bit_width);
}

static void test_parse_type_byte(void)
{
    init_parser("byte");
    dsdl_parsed_type_t type;
    TEST_ASSERT_TRUE(dsdl_parse_type_(&g_parser, &type));
    TEST_ASSERT_TRUE(dsdl_type_is_alias(type.kind));
    TEST_ASSERT_EQUAL_UINT8(8, type.bit_width);
}

static void test_parse_type_truncated(void)
{
    init_parser("truncated uint8");
    dsdl_parsed_type_t type;
    TEST_ASSERT_TRUE(dsdl_parse_type_(&g_parser, &type));
    TEST_ASSERT_TRUE(dsdl_type_is_uint(type.kind));
    TEST_ASSERT_FALSE(type.is_saturated);
}

static void test_parse_type_saturated(void)
{
    init_parser("saturated int16");
    dsdl_parsed_type_t type;
    TEST_ASSERT_TRUE(dsdl_parse_type_(&g_parser, &type));
    TEST_ASSERT_TRUE(dsdl_type_is_int(type.kind));
    TEST_ASSERT_TRUE(type.is_saturated);
}

static void test_parse_type_array_fixed(void)
{
    init_parser("uint8[10]");
    dsdl_parsed_type_t type;
    TEST_ASSERT_TRUE(dsdl_parse_type_(&g_parser, &type));
    TEST_ASSERT_TRUE(dsdl_type_is_array(type.kind));
    TEST_ASSERT_FALSE(type.is_variable);
    TEST_ASSERT_EQUAL_size_t(10, type.array_size);
}

static void test_parse_type_array_variable_inclusive(void)
{
    init_parser("uint8[<=256]");
    dsdl_parsed_type_t type;
    TEST_ASSERT_TRUE(dsdl_parse_type_(&g_parser, &type));
    TEST_ASSERT_TRUE(dsdl_type_is_array(type.kind));
    TEST_ASSERT_TRUE(type.is_variable);
    TEST_ASSERT_TRUE(type.is_inclusive);
    TEST_ASSERT_EQUAL_size_t(256, type.array_size);
}

static void test_parse_type_array_variable_exclusive(void)
{
    init_parser("uint8[<100]");
    dsdl_parsed_type_t type;
    TEST_ASSERT_TRUE(dsdl_parse_type_(&g_parser, &type));
    TEST_ASSERT_TRUE(dsdl_type_is_array(type.kind));
    TEST_ASSERT_TRUE(type.is_variable);
    TEST_ASSERT_FALSE(type.is_inclusive);
    TEST_ASSERT_EQUAL_size_t(100, type.array_size);
}

static void test_parse_type_array_expression(void)
{
    init_parser("uint8[2 * 3]");
    dsdl_parsed_type_t type;
    TEST_ASSERT_TRUE(dsdl_parse_type_(&g_parser, &type));
    TEST_ASSERT_TRUE(dsdl_type_is_array(type.kind));
    TEST_ASSERT_EQUAL_size_t(6, type.array_size);
}

static void test_parse_type_versioned(void)
{
    init_parser("uavcan.node.Heartbeat.1.0");
    dsdl_parsed_type_t type;
    TEST_ASSERT_TRUE(dsdl_parse_type_(&g_parser, &type));
    TEST_ASSERT_TRUE(dsdl_type_is_composite(type.kind));
    TEST_ASSERT_EQUAL_UINT8(1, type.version_major);
    TEST_ASSERT_EQUAL_UINT8(0, type.version_minor);
}

// ============================================================================
// Statement parsing tests
// ============================================================================

static void test_parse_stmt_field(void)
{
    init_parser("uint32 timestamp");
    dsdl_parsed_stmt_t stmt;
    TEST_ASSERT_TRUE(dsdl_parse_statement_(&g_parser, &stmt));
    TEST_ASSERT_EQUAL(dsdl_stmt_field, stmt.kind);
    TEST_ASSERT_TRUE(dsdl_type_is_uint(stmt.type.kind));
    TEST_ASSERT_EQUAL_UINT8(32, stmt.type.bit_width);
    TEST_ASSERT_EQUAL_size_t(9, stmt.name.len);
    TEST_ASSERT_EQUAL_MEMORY("timestamp", stmt.name.str, 9);
}

static void test_parse_stmt_constant(void)
{
    init_parser("uint8 MAX = 255");
    dsdl_parsed_stmt_t stmt;
    TEST_ASSERT_TRUE(dsdl_parse_statement_(&g_parser, &stmt));
    TEST_ASSERT_EQUAL(dsdl_stmt_constant, stmt.kind);
    TEST_ASSERT_TRUE(stmt.has_value);
    TEST_ASSERT_EQUAL(dsdl_value_rational, stmt.value.kind);
    TEST_ASSERT_EQUAL_INT64(255, stmt.value.as.rational.num);
}

static void test_parse_stmt_padding(void)
{
    init_parser("void7");
    dsdl_parsed_stmt_t stmt;
    TEST_ASSERT_TRUE(dsdl_parse_statement_(&g_parser, &stmt));
    TEST_ASSERT_EQUAL(dsdl_stmt_padding, stmt.kind);
    TEST_ASSERT_EQUAL_UINT8(7, stmt.type.bit_width);
}

static void test_parse_stmt_directive_simple(void)
{
    init_parser("@sealed");
    dsdl_parsed_stmt_t stmt;
    TEST_ASSERT_TRUE(dsdl_parse_statement_(&g_parser, &stmt));
    TEST_ASSERT_EQUAL(dsdl_stmt_directive, stmt.kind);
    TEST_ASSERT_FALSE(stmt.has_value);
    TEST_ASSERT_EQUAL_size_t(6, stmt.name.len);
    TEST_ASSERT_EQUAL_MEMORY("sealed", stmt.name.str, 6);
}

static void test_parse_stmt_directive_with_expr(void)
{
    init_parser("@extent 64 * 8");
    dsdl_parsed_stmt_t stmt;
    TEST_ASSERT_TRUE(dsdl_parse_statement_(&g_parser, &stmt));
    TEST_ASSERT_EQUAL(dsdl_stmt_directive, stmt.kind);
    TEST_ASSERT_TRUE(stmt.has_value);
    TEST_ASSERT_EQUAL_INT64(512, stmt.value.as.rational.num);
}

static void test_parse_stmt_service_marker(void)
{
    init_parser("---");
    dsdl_parsed_stmt_t stmt;
    TEST_ASSERT_TRUE(dsdl_parse_statement_(&g_parser, &stmt));
    TEST_ASSERT_EQUAL(dsdl_stmt_service_marker, stmt.kind);
}

static void test_parse_stmt_service_marker_long(void)
{
    init_parser("----------");
    dsdl_parsed_stmt_t stmt;
    TEST_ASSERT_TRUE(dsdl_parse_statement_(&g_parser, &stmt));
    TEST_ASSERT_EQUAL(dsdl_stmt_service_marker, stmt.kind);
}

static void test_parse_stmt_empty(void)
{
    init_parser("");
    dsdl_parsed_stmt_t stmt;
    TEST_ASSERT_TRUE(dsdl_parse_statement_(&g_parser, &stmt));
    TEST_ASSERT_EQUAL(dsdl_stmt_none, stmt.kind);
}

static void test_parse_stmt_comment_only(void)
{
    init_parser("# this is a comment");
    dsdl_parsed_stmt_t stmt;
    TEST_ASSERT_TRUE(dsdl_parse_statement_(&g_parser, &stmt));
    TEST_ASSERT_EQUAL(dsdl_stmt_none, stmt.kind);
}

// ============================================================================
// Definition parsing tests
// ============================================================================

static void test_parse_def_simple(void)
{
    // Similar to Simple.1.0.dsdl
    const char* dsdl = "int32 a\n"
                       "float16 b\n"
                       "bool c\n"
                       "@sealed\n";

    init_parser(dsdl);
    dsdl_parsed_def_t def;
    TEST_ASSERT_TRUE(dsdl_parsed_def_init_(&def, &g_dsdl));
    TEST_ASSERT_TRUE(dsdl_parse_definition_(&g_parser, &def));

    TEST_ASSERT_FALSE(def.is_service);
    TEST_ASSERT_TRUE(def.is_sealed);
    TEST_ASSERT_EQUAL_size_t(3, def.field_count);
    TEST_ASSERT_EQUAL_size_t(0, def.const_count);

    // Check first field: int32 a
    TEST_ASSERT_TRUE(dsdl_type_is_int(def.field_types[0].kind));
    TEST_ASSERT_EQUAL_UINT8(32, def.field_types[0].bit_width);
    TEST_ASSERT_EQUAL_size_t(1, def.field_names[0].len);
    TEST_ASSERT_EQUAL_MEMORY("a", def.field_names[0].str, 1);

    // Check second field: float16 b
    TEST_ASSERT_TRUE(dsdl_type_is_float(def.field_types[1].kind));
    TEST_ASSERT_EQUAL_UINT8(16, def.field_types[1].bit_width);

    // Check third field: bool c
    TEST_ASSERT_TRUE(dsdl_type_is_alias(def.field_types[2].kind));

    dsdl_parsed_def_deinit_(&def);
}

static void test_parse_def_with_constants(void)
{
    const char* dsdl = "uint8 STATUS_GOOD = 0\n"
                       "uint8 STATUS_BAD = 1\n"
                       "uint8 status\n";

    init_parser(dsdl);
    dsdl_parsed_def_t def;
    TEST_ASSERT_TRUE(dsdl_parsed_def_init_(&def, &g_dsdl));
    TEST_ASSERT_TRUE(dsdl_parse_definition_(&g_parser, &def));

    TEST_ASSERT_EQUAL_size_t(1, def.field_count);
    TEST_ASSERT_EQUAL_size_t(2, def.const_count);

    // Check first constant
    TEST_ASSERT_EQUAL_INT64(0, def.const_values[0].as.rational.num);
    TEST_ASSERT_EQUAL_size_t(11, def.const_names[0].len);
    TEST_ASSERT_EQUAL_MEMORY("STATUS_GOOD", def.const_names[0].str, 11);

    // Check second constant
    TEST_ASSERT_EQUAL_INT64(1, def.const_values[1].as.rational.num);

    dsdl_parsed_def_deinit_(&def);
}

static void test_parse_def_with_extent(void)
{
    const char* dsdl = "@extent 64\n"
                       "uint8[<=8] data\n";

    init_parser(dsdl);
    dsdl_parsed_def_t def;
    TEST_ASSERT_TRUE(dsdl_parsed_def_init_(&def, &g_dsdl));
    TEST_ASSERT_TRUE(dsdl_parse_definition_(&g_parser, &def));

    TEST_ASSERT_TRUE(def.has_extent);
    TEST_ASSERT_EQUAL_size_t(64, def.extent_bits);
    TEST_ASSERT_EQUAL_size_t(1, def.field_count);

    dsdl_parsed_def_deinit_(&def);
}

static void test_parse_def_service(void)
{
    const char* dsdl = "uint32 request_id\n"
                       "---\n"
                       "uint32 response_id\n"
                       "bool success\n";

    init_parser(dsdl);
    dsdl_parsed_def_t def;
    TEST_ASSERT_TRUE(dsdl_parsed_def_init_(&def, &g_dsdl));
    TEST_ASSERT_TRUE(dsdl_parse_definition_(&g_parser, &def));

    TEST_ASSERT_TRUE(def.is_service);
    TEST_ASSERT_EQUAL_size_t(1, def.field_count);          // Request fields
    TEST_ASSERT_EQUAL_size_t(2, def.response_field_count); // Response fields

    dsdl_parsed_def_deinit_(&def);
}

static void test_parse_def_union(void)
{
    const char* dsdl = "@union\n"
                       "uint8 a\n"
                       "uint16 b\n"
                       "uint32 c\n";

    init_parser(dsdl);
    dsdl_parsed_def_t def;
    TEST_ASSERT_TRUE(dsdl_parsed_def_init_(&def, &g_dsdl));
    TEST_ASSERT_TRUE(dsdl_parse_definition_(&g_parser, &def));

    TEST_ASSERT_TRUE(def.is_union);
    TEST_ASSERT_EQUAL_size_t(3, def.field_count);

    dsdl_parsed_def_deinit_(&def);
}

static void test_parse_def_with_comments(void)
{
    const char* dsdl = "# This is a comment\n"
                       "uint32 value  # inline comment\n"
                       "\n"
                       "# Another comment\n";

    init_parser(dsdl);
    dsdl_parsed_def_t def;
    TEST_ASSERT_TRUE(dsdl_parsed_def_init_(&def, &g_dsdl));
    TEST_ASSERT_TRUE(dsdl_parse_definition_(&g_parser, &def));

    TEST_ASSERT_EQUAL_size_t(1, def.field_count);

    dsdl_parsed_def_deinit_(&def);
}

static void test_parse_def_with_padding(void)
{
    const char* dsdl = "uint8 a\n"
                       "void7\n"
                       "uint8 b\n";

    init_parser(dsdl);
    dsdl_parsed_def_t def;
    TEST_ASSERT_TRUE(dsdl_parsed_def_init_(&def, &g_dsdl));
    TEST_ASSERT_TRUE(dsdl_parse_definition_(&g_parser, &def));

    TEST_ASSERT_EQUAL_size_t(3, def.field_count); // Padding is also a field
    TEST_ASSERT_TRUE(dsdl_type_is_void(def.field_types[1].kind));

    dsdl_parsed_def_deinit_(&def);
}

// ============================================================================
// Main
// ============================================================================

void setUp(void) {}
void tearDown(void) {}

int main(void)
{
    UNITY_BEGIN();

    // Integer literal tests
    RUN_TEST(test_parse_int_decimal);
    RUN_TEST(test_parse_int_decimal_with_underscores);
    RUN_TEST(test_parse_int_binary);
    RUN_TEST(test_parse_int_binary_uppercase);
    RUN_TEST(test_parse_int_octal);
    RUN_TEST(test_parse_int_hex);
    RUN_TEST(test_parse_int_hex_mixed_case);
    RUN_TEST(test_parse_int_zero);

    // Real literal tests
    RUN_TEST(test_parse_real_simple);
    RUN_TEST(test_parse_real_trailing_dot);
    RUN_TEST(test_parse_real_leading_dot);
    RUN_TEST(test_parse_real_exponent);
    RUN_TEST(test_parse_real_exponent_negative);
    RUN_TEST(test_parse_real_full);

    // Boolean literal tests
    RUN_TEST(test_parse_boolean_true);
    RUN_TEST(test_parse_boolean_false);
    RUN_TEST(test_parse_boolean_not_keyword);

    // String literal tests
    RUN_TEST(test_parse_string_double_quoted);
    RUN_TEST(test_parse_string_single_quoted);
    RUN_TEST(test_parse_string_with_escape);
    RUN_TEST(test_parse_string_empty);

    // Identifier tests
    RUN_TEST(test_parse_identifier_simple);
    RUN_TEST(test_parse_identifier_with_underscore);
    RUN_TEST(test_parse_identifier_starting_with_digit);

    // Expression tests
    RUN_TEST(test_parse_expr_simple_integer);
    RUN_TEST(test_parse_expr_addition);
    RUN_TEST(test_parse_expr_subtraction);
    RUN_TEST(test_parse_expr_multiplication);
    RUN_TEST(test_parse_expr_division);
    RUN_TEST(test_parse_expr_division_fraction);
    RUN_TEST(test_parse_expr_modulo);
    RUN_TEST(test_parse_expr_power);
    RUN_TEST(test_parse_expr_precedence);
    RUN_TEST(test_parse_expr_parentheses);
    RUN_TEST(test_parse_expr_unary_minus);
    RUN_TEST(test_parse_expr_unary_plus);
    RUN_TEST(test_parse_expr_bitwise_or);
    RUN_TEST(test_parse_expr_bitwise_and);
    RUN_TEST(test_parse_expr_bitwise_xor);
    RUN_TEST(test_parse_expr_comparison_eq);
    RUN_TEST(test_parse_expr_comparison_ne);
    RUN_TEST(test_parse_expr_comparison_lt);
    RUN_TEST(test_parse_expr_comparison_le);
    RUN_TEST(test_parse_expr_comparison_gt);
    RUN_TEST(test_parse_expr_comparison_ge);
    RUN_TEST(test_parse_expr_logical_and);
    RUN_TEST(test_parse_expr_logical_or);
    RUN_TEST(test_parse_expr_logical_not);
    RUN_TEST(test_parse_expr_complex);
    RUN_TEST(test_parse_expr_power_associativity);

    // Set literal tests
    RUN_TEST(test_parse_set_empty);
    RUN_TEST(test_parse_set_single);
    RUN_TEST(test_parse_set_multiple);

    // Type parsing tests
    RUN_TEST(test_parse_type_void);
    RUN_TEST(test_parse_type_void_various);
    RUN_TEST(test_parse_type_uint);
    RUN_TEST(test_parse_type_int);
    RUN_TEST(test_parse_type_float);
    RUN_TEST(test_parse_type_bool);
    RUN_TEST(test_parse_type_byte);
    RUN_TEST(test_parse_type_truncated);
    RUN_TEST(test_parse_type_saturated);
    RUN_TEST(test_parse_type_array_fixed);
    RUN_TEST(test_parse_type_array_variable_inclusive);
    RUN_TEST(test_parse_type_array_variable_exclusive);
    RUN_TEST(test_parse_type_array_expression);
    RUN_TEST(test_parse_type_versioned);

    // Statement parsing tests
    RUN_TEST(test_parse_stmt_field);
    RUN_TEST(test_parse_stmt_constant);
    RUN_TEST(test_parse_stmt_padding);
    RUN_TEST(test_parse_stmt_directive_simple);
    RUN_TEST(test_parse_stmt_directive_with_expr);
    RUN_TEST(test_parse_stmt_service_marker);
    RUN_TEST(test_parse_stmt_service_marker_long);
    RUN_TEST(test_parse_stmt_empty);
    RUN_TEST(test_parse_stmt_comment_only);

    // Definition parsing tests
    RUN_TEST(test_parse_def_simple);
    RUN_TEST(test_parse_def_with_constants);
    RUN_TEST(test_parse_def_with_extent);
    RUN_TEST(test_parse_def_service);
    RUN_TEST(test_parse_def_union);
    RUN_TEST(test_parse_def_with_comments);
    RUN_TEST(test_parse_def_with_padding);

    return UNITY_END();
}
