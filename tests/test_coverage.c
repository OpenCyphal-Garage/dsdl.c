/// Comprehensive coverage tests for dsdl.c
///
/// This file targets uncovered code paths to bring coverage from 78.4% to ≥95%.
/// Covers: bigint operations, UTF-8 decoding, NFC composition, services, delimited serialization,
/// validation types, and null input handling.

#include "dsdl.c"

#include "unity.h"

#include <dirent.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ============================================================================
 * Test helpers
 * ============================================================================ */

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

void setUp(void) {}

void tearDown(void) {}

/* ============================================================================
 * Category 1.1: Bigint tests (5 functions)
 * ============================================================================ */

static void test_bigint_cmp_negative_vs_positive(void)
{
    dsdl_bigint_t neg;
    dsdl_bigint_t pos;
    TEST_ASSERT_TRUE(dsdl_bigint_from_intmax(&neg, -5));
    TEST_ASSERT_TRUE(dsdl_bigint_from_intmax(&pos, 5));
    TEST_ASSERT_EQUAL_INT(-1, dsdl_bigint_cmp(&neg, &pos));
}

static void test_bigint_cmp_both_negative(void)
{
    dsdl_bigint_t a;
    dsdl_bigint_t b;
    TEST_ASSERT_TRUE(dsdl_bigint_from_intmax(&a, -10));
    TEST_ASSERT_TRUE(dsdl_bigint_from_intmax(&b, -5));
    TEST_ASSERT_EQUAL_INT(-1, dsdl_bigint_cmp(&a, &b));
}

static void test_bigint_cmp_negative_zero(void)
{
    dsdl_bigint_t neg_zero;
    dsdl_bigint_t pos_zero;
    TEST_ASSERT_TRUE(dsdl_bigint_from_intmax(&neg_zero, 0));
    neg_zero.negative = true;
    TEST_ASSERT_TRUE(dsdl_bigint_from_intmax(&pos_zero, 0));
    TEST_ASSERT_EQUAL_INT(0, dsdl_bigint_cmp(&neg_zero, &pos_zero));
}

static void test_bigint_div_by_zero(void)
{
    dsdl_bigint_t a;
    dsdl_bigint_t zero;
    dsdl_bigint_t q;
    dsdl_bigint_t r;
    TEST_ASSERT_TRUE(dsdl_bigint_from_intmax(&a, 42));
    TEST_ASSERT_TRUE(dsdl_bigint_from_intmax(&zero, 0));
    TEST_ASSERT_FALSE(dsdl_bigint_div_mod_abs(&a, &zero, &q, &r));
}

static void test_bigint_div_negative(void)
{
    dsdl_bigint_t a;
    dsdl_bigint_t b;
    dsdl_bigint_t q;
    dsdl_bigint_t r;
    TEST_ASSERT_TRUE(dsdl_bigint_from_intmax(&a, -17));
    TEST_ASSERT_TRUE(dsdl_bigint_from_intmax(&b, 5));
    a.negative = false;
    TEST_ASSERT_TRUE(dsdl_bigint_div_mod_abs(&a, &b, &q, &r));
    q.negative     = true;
    r.negative     = true;
    intmax_t q_val = 0;
    intmax_t r_val = 0;
    TEST_ASSERT_TRUE(dsdl_bigint_to_intmax(&q, &q_val));
    TEST_ASSERT_TRUE(dsdl_bigint_to_intmax(&r, &r_val));
    TEST_ASSERT_EQUAL_INT(-3, q_val);
    TEST_ASSERT_EQUAL_INT(-2, r_val);
}

/* ============================================================================
 * Category 1.2: UTF-8 tests (15 functions)
 * ============================================================================ */

static void test_utf8_decode_2byte(void)
{
    const char str[] = "\xC3\xA9";
    size_t     pos   = 0;
    uint32_t   cp    = 0;
    TEST_ASSERT_TRUE(dsdl_utf8_decode(str, 2, &pos, &cp));
    TEST_ASSERT_EQUAL_UINT32(0x00E9, cp);
    TEST_ASSERT_EQUAL_size_t(2, pos);
}

static void test_utf8_decode_3byte(void)
{
    const char str[] = "\xE2\x82\xAC";
    size_t     pos   = 0;
    uint32_t   cp    = 0;
    TEST_ASSERT_TRUE(dsdl_utf8_decode(str, 3, &pos, &cp));
    TEST_ASSERT_EQUAL_UINT32(0x20AC, cp);
    TEST_ASSERT_EQUAL_size_t(3, pos);
}

static void test_utf8_decode_4byte(void)
{
    const char str[] = "\xF0\x9F\x98\x80";
    size_t     pos   = 0;
    uint32_t   cp    = 0;
    TEST_ASSERT_TRUE(dsdl_utf8_decode(str, 4, &pos, &cp));
    TEST_ASSERT_EQUAL_UINT32(0x1F600, cp);
    TEST_ASSERT_EQUAL_size_t(4, pos);
}

static void test_utf8_decode_invalid_continuation(void)
{
    const char str[] = "\xC3\x00";
    size_t     pos   = 0;
    uint32_t   cp    = 0;
    TEST_ASSERT_FALSE(dsdl_utf8_decode(str, 2, &pos, &cp));
}

static void test_utf8_decode_overlong_2byte(void)
{
    const char str[] = "\xC1\xBF";
    size_t     pos   = 0;
    uint32_t   cp    = 0;
    TEST_ASSERT_FALSE(dsdl_utf8_decode(str, 2, &pos, &cp));
}

static void test_utf8_decode_overlong_3byte(void)
{
    const char str[] = "\xE0\x81\xBF";
    size_t     pos   = 0;
    uint32_t   cp    = 0;
    TEST_ASSERT_FALSE(dsdl_utf8_decode(str, 3, &pos, &cp));
}

static void test_utf8_decode_surrogate(void)
{
    const char str[] = "\xED\xA0\x80";
    size_t     pos   = 0;
    uint32_t   cp    = 0;
    TEST_ASSERT_FALSE(dsdl_utf8_decode(str, 3, &pos, &cp));
}

static void test_utf8_decode_overlong_4byte(void)
{
    const char str[] = "\xF0\x80\xA0\x80";
    size_t     pos   = 0;
    uint32_t   cp    = 0;
    TEST_ASSERT_FALSE(dsdl_utf8_decode(str, 4, &pos, &cp));
}

static void test_utf8_decode_out_of_range(void)
{
    const char str[] = "\xF4\x90\x80\x80";
    size_t     pos   = 0;
    uint32_t   cp    = 0;
    TEST_ASSERT_FALSE(dsdl_utf8_decode(str, 4, &pos, &cp));
}

static void test_utf8_decode_truncated_2byte(void)
{
    const char str[] = "\xC3";
    size_t     pos   = 0;
    uint32_t   cp    = 0;
    TEST_ASSERT_FALSE(dsdl_utf8_decode(str, 1, &pos, &cp));
}

static void test_utf8_decode_truncated_3byte(void)
{
    const char str[] = "\xE2\x82";
    size_t     pos   = 0;
    uint32_t   cp    = 0;
    TEST_ASSERT_FALSE(dsdl_utf8_decode(str, 2, &pos, &cp));
}

static void test_utf8_decode_truncated_4byte(void)
{
    const char str[] = "\xF0\x9F\x98";
    size_t     pos   = 0;
    uint32_t   cp    = 0;
    TEST_ASSERT_FALSE(dsdl_utf8_decode(str, 3, &pos, &cp));
}

static void test_utf8_decode_invalid_start_byte(void)
{
    const char str[] = "\x80";
    size_t     pos   = 0;
    uint32_t   cp    = 0;
    TEST_ASSERT_FALSE(dsdl_utf8_decode(str, 1, &pos, &cp));
}

static void test_utf8_decode_invalid_3byte_continuation(void)
{
    const char str[] = "\xE2\x00\xAC";
    size_t     pos   = 0;
    uint32_t   cp    = 0;
    TEST_ASSERT_FALSE(dsdl_utf8_decode(str, 3, &pos, &cp));
}

static void test_utf8_decode_invalid_4byte_continuation(void)
{
    const char str[] = "\xF0\x9F\x00\x80";
    size_t     pos   = 0;
    uint32_t   cp    = 0;
    TEST_ASSERT_FALSE(dsdl_utf8_decode(str, 4, &pos, &cp));
}

/* ============================================================================
 * Category 1.3: NFC tests (7 functions)
 * ============================================================================ */

static void test_compose_acute_uppercase(void)
{
    TEST_ASSERT_EQUAL_UINT32(0x00C1, dsdl_compose_acute('A'));
    TEST_ASSERT_EQUAL_UINT32(0x00C9, dsdl_compose_acute('E'));
    TEST_ASSERT_EQUAL_UINT32(0x00CD, dsdl_compose_acute('I'));
    TEST_ASSERT_EQUAL_UINT32(0x00D3, dsdl_compose_acute('O'));
    TEST_ASSERT_EQUAL_UINT32(0x00DA, dsdl_compose_acute('U'));
    TEST_ASSERT_EQUAL_UINT32(0x00DD, dsdl_compose_acute('Y'));
}

static void test_compose_acute_lowercase(void)
{
    TEST_ASSERT_EQUAL_UINT32(0x00E1, dsdl_compose_acute('a'));
    TEST_ASSERT_EQUAL_UINT32(0x00E9, dsdl_compose_acute('e'));
    TEST_ASSERT_EQUAL_UINT32(0x00ED, dsdl_compose_acute('i'));
    TEST_ASSERT_EQUAL_UINT32(0x00F3, dsdl_compose_acute('o'));
    TEST_ASSERT_EQUAL_UINT32(0x00FA, dsdl_compose_acute('u'));
    TEST_ASSERT_EQUAL_UINT32(0x00FD, dsdl_compose_acute('y'));
}

static void test_compose_acute_non_composable(void)
{
    TEST_ASSERT_EQUAL_UINT32(0, dsdl_compose_acute('B'));
    TEST_ASSERT_EQUAL_UINT32(0, dsdl_compose_acute('X'));
    TEST_ASSERT_EQUAL_UINT32(0, dsdl_compose_acute('1'));
    TEST_ASSERT_EQUAL_UINT32(0, dsdl_compose_acute(' '));
}

static void test_nfc_next_basic(void)
{
    const char str[] = "ABC";
    size_t     pos   = 0;
    uint32_t   cp    = 0;
    TEST_ASSERT_TRUE(dsdl_nfc_next(str, 3, &pos, &cp));
    TEST_ASSERT_EQUAL_UINT32('A', cp);
    TEST_ASSERT_EQUAL_size_t(1, pos);
}

static void test_nfc_composition(void)
{
    const char str[] = "e\xCC\x81";
    size_t     pos   = 0;
    uint32_t   cp    = 0;
    TEST_ASSERT_TRUE(dsdl_nfc_next(str, 3, &pos, &cp));
    TEST_ASSERT_EQUAL_UINT32(0x00E9, cp);
    TEST_ASSERT_EQUAL_size_t(3, pos);
}

static void test_string_equal_nfc_precomposed(void)
{
    const wkv_str_t a = { 2, "\xC3\xA9" };
    const wkv_str_t b = { 3, "e\xCC\x81" };
    TEST_ASSERT_TRUE(dsdl_string_equal_nfc(a, b));
}

static void test_string_equal_nfc_empty(void)
{
    const wkv_str_t a = { 0, "" };
    const wkv_str_t b = { 0, "" };
    TEST_ASSERT_TRUE(dsdl_string_equal_nfc(a, b));
}

/* ============================================================================
 * Category 1.4: Service tests (5 functions)
 * ============================================================================ */

static void test_service_basic(void)
{
    setup_dsdl();
    TEST_ASSERT_TRUE(add_test_roots());

    const dsdl_type_composite_t* type = dsdl_read(&g_dsdl, wkv_key("validation.Service.0.1"));
    TEST_ASSERT_NOT_NULL(type);
    TEST_ASSERT_NOT_NULL(type->response);

    teardown_dsdl();
}

static void test_service_with_union_request(void)
{
    setup_dsdl();
    TEST_ASSERT_TRUE(add_test_roots());

    const dsdl_type_composite_t* type = dsdl_read(&g_dsdl, wkv_key("validation.ServiceUnion.0.1"));
    TEST_ASSERT_NOT_NULL(type);
    TEST_ASSERT_NOT_NULL(type->response);

    teardown_dsdl();
}

static void test_service_both_union(void)
{
    setup_dsdl();
    TEST_ASSERT_TRUE(add_test_roots());

    const dsdl_type_composite_t* type = dsdl_read(&g_dsdl, wkv_key("validation.ServiceBothUnion.0.1"));
    TEST_ASSERT_NOT_NULL(type);
    TEST_ASSERT_NOT_NULL(type->response);

    teardown_dsdl();
}

static void test_service_empty(void)
{
    setup_dsdl();
    TEST_ASSERT_TRUE(add_test_roots());

    const dsdl_type_composite_t* type = dsdl_read(&g_dsdl, wkv_key("validation.ServiceEmpty.0.1"));
    TEST_ASSERT_NOT_NULL(type);
    TEST_ASSERT_NOT_NULL(type->response);

    teardown_dsdl();
}

static void test_service_fixed_port(void)
{
    setup_dsdl();
    TEST_ASSERT_TRUE(add_test_roots());

    const dsdl_type_composite_t* type = dsdl_read(&g_dsdl, wkv_key("validation.FixedPortService.0.1"));
    TEST_ASSERT_NOT_NULL(type);
    TEST_ASSERT_NOT_NULL(type->response);
    TEST_ASSERT_EQUAL_UINT16(300, type->fixed_port_id);

    teardown_dsdl();
}

/* ============================================================================
 * Category 1.5: Delimited serialization tests (2 functions)
 * ============================================================================ */

static void test_delimited_serialize(void)
{
    setup_dsdl();
    TEST_ASSERT_TRUE(add_test_roots());

    const dsdl_type_composite_t* type = dsdl_read(&g_dsdl, wkv_key("validation.Delimited.0.1"));
    TEST_ASSERT_NOT_NULL(type);

    uint8_t buffer[256];
    memset(buffer, 0, sizeof(buffer));

    uint_least8_t       a        = 1;
    uint_least16_t      b        = 2;
    uint_least32_t      c        = 3;
    double              d        = 4.0;
    void*               values[] = { &a, &b, &c, &d };
    dsdl_value_struct_t msg      = { .values = values };

    dsdl_error_t err  = dsdl_error_none;
    const size_t size = dsdl_serialize(type, &msg, sizeof(buffer), buffer, &err);
    TEST_ASSERT_NOT_EQUAL(SIZE_MAX, size);
    TEST_ASSERT_GREATER_THAN(4, size);

    teardown_dsdl();
}

static void test_delimited_nested_in_sealed(void)
{
    setup_dsdl();
    TEST_ASSERT_TRUE(add_test_roots());

    const dsdl_type_composite_t* type = dsdl_read(&g_dsdl, wkv_key("validation.MixedSealing.0.1"));
    TEST_ASSERT_NOT_NULL(type);

    teardown_dsdl();
}

/* ============================================================================
 * Category 1.6: Validation types (30+ functions)
 * ============================================================================ */

static void test_huge_struct_loading(void)
{
    setup_dsdl();
    TEST_ASSERT_TRUE(add_test_roots());

    const dsdl_type_composite_t* type = dsdl_read(&g_dsdl, wkv_key("validation.HugeStruct.0.1"));
    TEST_ASSERT_NOT_NULL(type);
    TEST_ASSERT_EQUAL_size_t(1000, type->field_count);

    teardown_dsdl();
}

static void test_large_union_loading(void)
{
    setup_dsdl();
    TEST_ASSERT_TRUE(add_test_roots());

    const dsdl_type_composite_t* type = dsdl_read(&g_dsdl, wkv_key("validation.LargeUnion.0.1"));
    TEST_ASSERT_NOT_NULL(type);
    TEST_ASSERT_EQUAL_size_t(260, type->field_count);

    teardown_dsdl();
}

static void test_unicode_strings_dsdl(void)
{
    setup_dsdl();
    TEST_ASSERT_TRUE(add_test_roots());

    const dsdl_type_composite_t* type = dsdl_read(&g_dsdl, wkv_key("validation.UnicodeStrings.0.1"));
    TEST_ASSERT_NOT_NULL(type);

    teardown_dsdl();
}

static void test_power_operations(void)
{
    setup_dsdl();
    TEST_ASSERT_TRUE(add_test_roots());

    const dsdl_type_composite_t* type = dsdl_read(&g_dsdl, wkv_key("validation.PowerOperations.0.1"));
    TEST_ASSERT_NOT_NULL(type);

    teardown_dsdl();
}

static void test_set_operations(void)
{
    setup_dsdl();
    TEST_ASSERT_TRUE(add_test_roots());

    const dsdl_type_composite_t* type = dsdl_read(&g_dsdl, wkv_key("validation.SetOperations.0.1"));
    TEST_ASSERT_NOT_NULL(type);

    teardown_dsdl();
}

static void test_type_attributes(void)
{
    setup_dsdl();
    TEST_ASSERT_TRUE(add_test_roots());

    const dsdl_type_composite_t* type = dsdl_read(&g_dsdl, wkv_key("validation.TypeAttributes.0.1"));
    TEST_ASSERT_NOT_NULL(type);

    teardown_dsdl();
}

static void test_versioned_types(void)
{
    setup_dsdl();
    TEST_ASSERT_TRUE(add_test_roots());

    const dsdl_type_composite_t* type1 = dsdl_read(&g_dsdl, wkv_key("validation.Versioned.0.1"));
    TEST_ASSERT_NOT_NULL(type1);

    const dsdl_type_composite_t* type2 = dsdl_read(&g_dsdl, wkv_key("validation.Versioned.1.0"));
    TEST_ASSERT_NOT_NULL(type2);

    const dsdl_type_composite_t* type3 = dsdl_read(&g_dsdl, wkv_key("validation.Versioned.255.255"));
    TEST_ASSERT_NOT_NULL(type3);

    teardown_dsdl();
}

static void test_deep_nesting(void)
{
    setup_dsdl();
    TEST_ASSERT_TRUE(add_test_roots());

    const dsdl_type_composite_t* type = dsdl_read(&g_dsdl, wkv_key("validation.DeepNesting.0.1"));
    TEST_ASSERT_NOT_NULL(type);

    teardown_dsdl();
}

static void test_bitwise_ops(void)
{
    setup_dsdl();
    TEST_ASSERT_TRUE(add_test_roots());

    const dsdl_type_composite_t* type = dsdl_read(&g_dsdl, wkv_key("validation.BitwiseOps.0.1"));
    TEST_ASSERT_NOT_NULL(type);

    teardown_dsdl();
}

static void test_rational_precision(void)
{
    setup_dsdl();
    TEST_ASSERT_TRUE(add_test_roots());

    const dsdl_type_composite_t* type = dsdl_read(&g_dsdl, wkv_key("validation.RationalPrecision.0.1"));
    TEST_ASSERT_NOT_NULL(type);

    teardown_dsdl();
}

static void test_coercion(void)
{
    setup_dsdl();
    TEST_ASSERT_TRUE(add_test_roots());

    const dsdl_type_composite_t* type = dsdl_read(&g_dsdl, wkv_key("validation.Coercion.0.1"));
    TEST_ASSERT_NOT_NULL(type);

    teardown_dsdl();
}

static void test_assertions(void)
{
    setup_dsdl();
    TEST_ASSERT_TRUE(add_test_roots());

    const dsdl_type_composite_t* type = dsdl_read(&g_dsdl, wkv_key("validation.Assertions.0.1"));
    TEST_ASSERT_NOT_NULL(type);

    teardown_dsdl();
}

static void test_print_directive(void)
{
    setup_dsdl();
    TEST_ASSERT_TRUE(add_test_roots());

    const dsdl_type_composite_t* type = dsdl_read(&g_dsdl, wkv_key("validation.Print.0.1"));
    TEST_ASSERT_NOT_NULL(type);

    teardown_dsdl();
}

static void test_deprecated(void)
{
    setup_dsdl();
    TEST_ASSERT_TRUE(add_test_roots());

    const dsdl_type_composite_t* type = dsdl_read(&g_dsdl, wkv_key("validation.Deprecated.0.1"));
    TEST_ASSERT_NOT_NULL(type);
    TEST_ASSERT_TRUE(type->deprecated);

    teardown_dsdl();
}

static void test_expressions(void)
{
    setup_dsdl();
    TEST_ASSERT_TRUE(add_test_roots());

    const dsdl_type_composite_t* type = dsdl_read(&g_dsdl, wkv_key("validation.Expressions.0.1"));
    TEST_ASSERT_NOT_NULL(type);

    teardown_dsdl();
}

static void test_precedence(void)
{
    setup_dsdl();
    TEST_ASSERT_TRUE(add_test_roots());

    const dsdl_type_composite_t* type = dsdl_read(&g_dsdl, wkv_key("validation.Precedence.0.1"));
    TEST_ASSERT_NOT_NULL(type);

    teardown_dsdl();
}

static void test_literals(void)
{
    setup_dsdl();
    TEST_ASSERT_TRUE(add_test_roots());

    const dsdl_type_composite_t* type = dsdl_read(&g_dsdl, wkv_key("validation.Literals.0.1"));
    TEST_ASSERT_NOT_NULL(type);

    teardown_dsdl();
}

static void test_string_ops(void)
{
    setup_dsdl();
    TEST_ASSERT_TRUE(add_test_roots());

    const dsdl_type_composite_t* type = dsdl_read(&g_dsdl, wkv_key("validation.StringOps.0.1"));
    TEST_ASSERT_NOT_NULL(type);

    teardown_dsdl();
}

static void test_string_escapes(void)
{
    setup_dsdl();
    TEST_ASSERT_TRUE(add_test_roots());

    const dsdl_type_composite_t* type = dsdl_read(&g_dsdl, wkv_key("validation.StringEscapes.0.1"));
    TEST_ASSERT_NOT_NULL(type);

    teardown_dsdl();
}

static void test_offset_usage(void)
{
    setup_dsdl();
    TEST_ASSERT_TRUE(add_test_roots());

    const dsdl_type_composite_t* type = dsdl_read(&g_dsdl, wkv_key("validation.Offset.0.1"));
    TEST_ASSERT_NOT_NULL(type);

    teardown_dsdl();
}

static void test_union_offset(void)
{
    setup_dsdl();
    TEST_ASSERT_TRUE(add_test_roots());

    const dsdl_type_composite_t* type = dsdl_read(&g_dsdl, wkv_key("validation.UnionOffset.0.1"));
    TEST_ASSERT_NOT_NULL(type);

    teardown_dsdl();
}

static void test_extent_types(void)
{
    setup_dsdl();
    TEST_ASSERT_TRUE(add_test_roots());

    const dsdl_type_composite_t* type1 = dsdl_read(&g_dsdl, wkv_key("validation.ExtentOnly.0.1"));
    TEST_ASSERT_NOT_NULL(type1);

    const dsdl_type_composite_t* type2 = dsdl_read(&g_dsdl, wkv_key("validation.ZeroExtent.0.1"));
    TEST_ASSERT_NOT_NULL(type2);

    const dsdl_type_composite_t* type3 = dsdl_read(&g_dsdl, wkv_key("validation.DynamicExtent.0.1"));
    TEST_ASSERT_NOT_NULL(type3);

    const dsdl_type_composite_t* type4 = dsdl_read(&g_dsdl, wkv_key("validation.MaxExtent.0.1"));
    TEST_ASSERT_NOT_NULL(type4);

    teardown_dsdl();
}

static void test_doc_comments(void)
{
    setup_dsdl();
    TEST_ASSERT_TRUE(add_test_roots());

    const dsdl_type_composite_t* type = dsdl_read(&g_dsdl, wkv_key("validation.DocComments.0.1"));
    TEST_ASSERT_NOT_NULL(type);

    teardown_dsdl();
}

static void test_references(void)
{
    setup_dsdl();
    TEST_ASSERT_TRUE(add_test_roots());

    const dsdl_type_composite_t* type = dsdl_read(&g_dsdl, wkv_key("validation.References.0.1"));
    TEST_ASSERT_NOT_NULL(type);

    teardown_dsdl();
}

static void test_relative_refs(void)
{
    setup_dsdl();
    TEST_ASSERT_TRUE(add_test_roots());

    const dsdl_type_composite_t* type = dsdl_read(&g_dsdl, wkv_key("validation.RelativeRefs.0.1"));
    TEST_ASSERT_NOT_NULL(type);

    teardown_dsdl();
}

static void test_complex_ref_expr(void)
{
    setup_dsdl();
    TEST_ASSERT_TRUE(add_test_roots());

    const dsdl_type_composite_t* type = dsdl_read(&g_dsdl, wkv_key("validation.ComplexRefExpr.0.1"));
    TEST_ASSERT_NOT_NULL(type);

    teardown_dsdl();
}

static void test_union_with_consts(void)
{
    setup_dsdl();
    TEST_ASSERT_TRUE(add_test_roots());

    const dsdl_type_composite_t* type = dsdl_read(&g_dsdl, wkv_key("validation.UnionWithConsts.0.1"));
    TEST_ASSERT_NOT_NULL(type);

    teardown_dsdl();
}

static void test_truncated_types(void)
{
    setup_dsdl();
    TEST_ASSERT_TRUE(add_test_roots());

    const dsdl_type_composite_t* type = dsdl_read(&g_dsdl, wkv_key("validation.TruncatedTypes.0.1"));
    TEST_ASSERT_NOT_NULL(type);

    teardown_dsdl();
}

static void test_fixed_port_message(void)
{
    setup_dsdl();
    TEST_ASSERT_TRUE(add_test_roots());

    const dsdl_type_composite_t* type = dsdl_read(&g_dsdl, wkv_key("validation.FixedPortMessage.1.0"));
    TEST_ASSERT_NOT_NULL(type);
    TEST_ASSERT_EQUAL_UINT16(7000, type->fixed_port_id);

    teardown_dsdl();
}

static void test_array_cap_expr(void)
{
    setup_dsdl();
    TEST_ASSERT_TRUE(add_test_roots());

    const dsdl_type_composite_t* type = dsdl_read(&g_dsdl, wkv_key("validation.ArrayCapExpr.0.1"));
    TEST_ASSERT_NOT_NULL(type);

    teardown_dsdl();
}

static void test_large_sets(void)
{
    setup_dsdl();
    TEST_ASSERT_TRUE(add_test_roots());

    const dsdl_type_composite_t* type = dsdl_read(&g_dsdl, wkv_key("validation.LargeSets.0.1"));
    TEST_ASSERT_NOT_NULL(type);

    teardown_dsdl();
}

static void test_large_strings(void)
{
    setup_dsdl();
    TEST_ASSERT_TRUE(add_test_roots());

    const dsdl_type_composite_t* type = dsdl_read(&g_dsdl, wkv_key("validation.LargeStrings.0.1"));
    TEST_ASSERT_NOT_NULL(type);

    teardown_dsdl();
}

static void test_delimited_offset(void)
{
    setup_dsdl();
    TEST_ASSERT_TRUE(add_test_roots());

    const dsdl_type_composite_t* type = dsdl_read(&g_dsdl, wkv_key("validation.DelimitedOffset.0.1"));
    TEST_ASSERT_NOT_NULL(type);

    teardown_dsdl();
}

static void test_delimited_alignment(void)
{
    setup_dsdl();
    TEST_ASSERT_TRUE(add_test_roots());

    const dsdl_type_composite_t* type = dsdl_read(&g_dsdl, wkv_key("validation.DelimitedAlignment.0.1"));
    TEST_ASSERT_NOT_NULL(type);

    teardown_dsdl();
}

/* ============================================================================
 * Category 1.7: Null input test (1 function)
 * ============================================================================ */

static void test_deserialize_null_value(void)
{
    setup_dsdl();
    TEST_ASSERT_TRUE(add_test_roots());

    const dsdl_type_composite_t* type = dsdl_read(&g_dsdl, wkv_key("validation.Simple.0.1"));
    TEST_ASSERT_NOT_NULL(type);

    uint8_t      buffer[64] = { 0 };
    dsdl_error_t err        = dsdl_error_none;
    const size_t consumed   = dsdl_deserialize(type, NULL, sizeof(buffer), buffer, &err);
    TEST_ASSERT_EQUAL_size_t(SIZE_MAX, consumed);

    teardown_dsdl();
}

/* ============================================================================
 * Main test runner
 * ============================================================================ */

int main(void)
{
    UNITY_BEGIN();

    /* Bigint tests */
    RUN_TEST(test_bigint_cmp_negative_vs_positive);
    RUN_TEST(test_bigint_cmp_both_negative);
    RUN_TEST(test_bigint_cmp_negative_zero);
    RUN_TEST(test_bigint_div_by_zero);
    RUN_TEST(test_bigint_div_negative);

    /* UTF-8 tests */
    RUN_TEST(test_utf8_decode_2byte);
    RUN_TEST(test_utf8_decode_3byte);
    RUN_TEST(test_utf8_decode_4byte);
    RUN_TEST(test_utf8_decode_invalid_continuation);
    RUN_TEST(test_utf8_decode_overlong_2byte);
    RUN_TEST(test_utf8_decode_overlong_3byte);
    RUN_TEST(test_utf8_decode_surrogate);
    RUN_TEST(test_utf8_decode_overlong_4byte);
    RUN_TEST(test_utf8_decode_out_of_range);
    RUN_TEST(test_utf8_decode_truncated_2byte);
    RUN_TEST(test_utf8_decode_truncated_3byte);
    RUN_TEST(test_utf8_decode_truncated_4byte);
    RUN_TEST(test_utf8_decode_invalid_start_byte);
    RUN_TEST(test_utf8_decode_invalid_3byte_continuation);
    RUN_TEST(test_utf8_decode_invalid_4byte_continuation);

    /* NFC tests */
    RUN_TEST(test_compose_acute_uppercase);
    RUN_TEST(test_compose_acute_lowercase);
    RUN_TEST(test_compose_acute_non_composable);
    RUN_TEST(test_nfc_next_basic);
    RUN_TEST(test_nfc_composition);
    RUN_TEST(test_string_equal_nfc_precomposed);
    RUN_TEST(test_string_equal_nfc_empty);

    /* Service tests */
    RUN_TEST(test_service_basic);
    RUN_TEST(test_service_with_union_request);
    RUN_TEST(test_service_both_union);
    RUN_TEST(test_service_empty);
    RUN_TEST(test_service_fixed_port);

    /* Delimited serialization tests */
    RUN_TEST(test_delimited_serialize);
    RUN_TEST(test_delimited_nested_in_sealed);

    /* Validation types */
    RUN_TEST(test_huge_struct_loading);
    RUN_TEST(test_large_union_loading);
    RUN_TEST(test_unicode_strings_dsdl);
    RUN_TEST(test_power_operations);
    RUN_TEST(test_set_operations);
    RUN_TEST(test_type_attributes);
    RUN_TEST(test_versioned_types);
    RUN_TEST(test_deep_nesting);
    RUN_TEST(test_bitwise_ops);
    RUN_TEST(test_rational_precision);
    RUN_TEST(test_coercion);
    RUN_TEST(test_assertions);
    RUN_TEST(test_print_directive);
    RUN_TEST(test_deprecated);
    RUN_TEST(test_expressions);
    RUN_TEST(test_precedence);
    RUN_TEST(test_literals);
    RUN_TEST(test_string_ops);
    RUN_TEST(test_string_escapes);
    RUN_TEST(test_offset_usage);
    RUN_TEST(test_union_offset);
    RUN_TEST(test_extent_types);
    RUN_TEST(test_doc_comments);
    RUN_TEST(test_references);
    RUN_TEST(test_relative_refs);
    RUN_TEST(test_complex_ref_expr);
    RUN_TEST(test_union_with_consts);
    RUN_TEST(test_truncated_types);
    RUN_TEST(test_fixed_port_message);
    RUN_TEST(test_array_cap_expr);
    RUN_TEST(test_large_sets);
    RUN_TEST(test_large_strings);
    RUN_TEST(test_delimited_offset);
    RUN_TEST(test_delimited_alignment);

    /* Null input test */
    RUN_TEST(test_deserialize_null_value);

    return UNITY_END();
}
