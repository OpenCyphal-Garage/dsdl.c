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

/* OOM simulation - fails after g_oom_counter allocations */
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

static void test_bigint_mul_large(void)
{
    dsdl_bigint_t a;
    dsdl_bigint_t b;
    dsdl_bigint_t result;
    TEST_ASSERT_TRUE(dsdl_bigint_from_intmax(&a, 10000));
    TEST_ASSERT_TRUE(dsdl_bigint_from_intmax(&b, 10000));
    TEST_ASSERT_TRUE(dsdl_bigint_mul_abs(&a, &b, &result));
    intmax_t val = 0;
    TEST_ASSERT_TRUE(dsdl_bigint_to_intmax(&result, &val));
    TEST_ASSERT_EQUAL_INT(100000000, val);
}

static void test_bigint_add_large(void)
{
    dsdl_bigint_t a;
    dsdl_bigint_t b;
    dsdl_bigint_t result;
    TEST_ASSERT_TRUE(dsdl_bigint_from_intmax(&a, INTMAX_MAX / 2));
    TEST_ASSERT_TRUE(dsdl_bigint_from_intmax(&b, INTMAX_MAX / 2));
    TEST_ASSERT_TRUE(dsdl_bigint_add_abs(&a, &b, &result));
}

static void test_bigint_sub_underflow(void)
{
    dsdl_bigint_t a;
    dsdl_bigint_t b;
    dsdl_bigint_t result;
    TEST_ASSERT_TRUE(dsdl_bigint_from_intmax(&a, 5));
    TEST_ASSERT_TRUE(dsdl_bigint_from_intmax(&b, 10));
    TEST_ASSERT_TRUE(dsdl_bigint_add_signed(&a, &b, &result));
}

static void test_bigint_mod_operations(void)
{
    dsdl_bigint_t a;
    dsdl_bigint_t b;
    dsdl_bigint_t q;
    dsdl_bigint_t r;
    TEST_ASSERT_TRUE(dsdl_bigint_from_intmax(&a, 17));
    TEST_ASSERT_TRUE(dsdl_bigint_from_intmax(&b, 5));
    TEST_ASSERT_TRUE(dsdl_bigint_div_mod_abs(&a, &b, &q, &r));
    intmax_t r_val = 0;
    TEST_ASSERT_TRUE(dsdl_bigint_to_intmax(&r, &r_val));
    TEST_ASSERT_EQUAL_INT(2, r_val);
}

static void test_bigint_gcd_operations(void)
{
    dsdl_bigint_t a;
    dsdl_bigint_t b;
    dsdl_bigint_t result;
    TEST_ASSERT_TRUE(dsdl_bigint_from_intmax(&a, 48));
    TEST_ASSERT_TRUE(dsdl_bigint_from_intmax(&b, 18));
    TEST_ASSERT_TRUE(dsdl_bigint_gcd(a, b, &result));
    intmax_t val = 0;
    TEST_ASSERT_TRUE(dsdl_bigint_to_intmax(&result, &val));
    TEST_ASSERT_EQUAL_INT(6, val);
}

static void test_bigint_pow10(void)
{
    dsdl_bigint_t result;
    TEST_ASSERT_TRUE(dsdl_bigint_pow10(5, &result));
    intmax_t val = 0;
    TEST_ASSERT_TRUE(dsdl_bigint_to_intmax(&result, &val));
    TEST_ASSERT_EQUAL_INT(100000, val);
}

static void test_bigint_mul_small_inplace(void)
{
    dsdl_bigint_t a;
    TEST_ASSERT_TRUE(dsdl_bigint_from_intmax(&a, 100));
    TEST_ASSERT_TRUE(dsdl_bigint_mul_small_inplace(&a, 50));
    intmax_t val = 0;
    TEST_ASSERT_TRUE(dsdl_bigint_to_intmax(&a, &val));
    TEST_ASSERT_EQUAL_INT(5000, val);
}

static void test_bigint_add_small_inplace(void)
{
    dsdl_bigint_t a;
    TEST_ASSERT_TRUE(dsdl_bigint_from_intmax(&a, 100));
    TEST_ASSERT_TRUE(dsdl_bigint_add_small_inplace(&a, 50));
    intmax_t val = 0;
    TEST_ASSERT_TRUE(dsdl_bigint_to_intmax(&a, &val));
    TEST_ASSERT_EQUAL_INT(150, val);
}

static void test_bigint_div_small_inplace(void)
{
    dsdl_bigint_t a;
    TEST_ASSERT_TRUE(dsdl_bigint_from_intmax(&a, 100));
    uint32_t rem = 0;
    TEST_ASSERT_TRUE(dsdl_bigint_div_small_inplace(&a, 7, &rem));
    intmax_t val = 0;
    TEST_ASSERT_TRUE(dsdl_bigint_to_intmax(&a, &val));
    TEST_ASSERT_EQUAL_INT(14, val);
    TEST_ASSERT_EQUAL_UINT32(2, rem);
}

static void test_bigint_div_small_by_zero(void)
{
    dsdl_bigint_t a;
    TEST_ASSERT_TRUE(dsdl_bigint_from_intmax(&a, 100));
    uint32_t rem = 0;
    TEST_ASSERT_FALSE(dsdl_bigint_div_small_inplace(&a, 0, &rem));
}

static void test_bigint_mul_pow2_inplace(void)
{
    dsdl_bigint_t a;
    TEST_ASSERT_TRUE(dsdl_bigint_from_intmax(&a, 5));
    TEST_ASSERT_TRUE(dsdl_bigint_mul_pow2_inplace(&a, 3));
    intmax_t val = 0;
    TEST_ASSERT_TRUE(dsdl_bigint_to_intmax(&a, &val));
    TEST_ASSERT_EQUAL_INT(40, val);
}

static void test_bigint_mul_pow10_inplace(void)
{
    dsdl_bigint_t a;
    TEST_ASSERT_TRUE(dsdl_bigint_from_intmax(&a, 5));
    TEST_ASSERT_TRUE(dsdl_bigint_mul_pow10_inplace(&a, 3));
    intmax_t val = 0;
    TEST_ASSERT_TRUE(dsdl_bigint_to_intmax(&a, &val));
    TEST_ASSERT_EQUAL_INT(5000, val);
}

static void test_bigint_to_double(void)
{
    dsdl_bigint_t a;
    TEST_ASSERT_TRUE(dsdl_bigint_from_intmax(&a, 12345));
    double val = 0.0;
    TEST_ASSERT_TRUE(dsdl_bigint_to_double(&a, &val));
    TEST_ASSERT_TRUE(val > 12344.0 && val < 12346.0);
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
 * Category 1.7: Additional validation types (30+ functions)
 * ============================================================================ */

static void test_complex_size(void)
{
    setup_dsdl();
    TEST_ASSERT_TRUE(add_test_roots());
    const dsdl_type_composite_t* type = dsdl_read(&g_dsdl, wkv_key("validation.ComplexSize.0.1"));
    TEST_ASSERT_NOT_NULL(type);
    teardown_dsdl();
}

static void test_minimal_size(void)
{
    setup_dsdl();
    TEST_ASSERT_TRUE(add_test_roots());
    const dsdl_type_composite_t* type = dsdl_read(&g_dsdl, wkv_key("validation.MinimalSize.0.1"));
    TEST_ASSERT_NOT_NULL(type);
    teardown_dsdl();
}

static void test_byte_arrays(void)
{
    setup_dsdl();
    TEST_ASSERT_TRUE(add_test_roots());
    const dsdl_type_composite_t* type = dsdl_read(&g_dsdl, wkv_key("validation.ByteArrays.0.1"));
    TEST_ASSERT_NOT_NULL(type);
    teardown_dsdl();
}

static void test_string_arrays(void)
{
    setup_dsdl();
    TEST_ASSERT_TRUE(add_test_roots());
    const dsdl_type_composite_t* type = dsdl_read(&g_dsdl, wkv_key("validation.StringArrays.0.1"));
    TEST_ASSERT_NOT_NULL(type);
    teardown_dsdl();
}

static void test_large_arrays(void)
{
    setup_dsdl();
    TEST_ASSERT_TRUE(add_test_roots());
    const dsdl_type_composite_t* type = dsdl_read(&g_dsdl, wkv_key("validation.LargeArrays.0.1"));
    TEST_ASSERT_NOT_NULL(type);
    teardown_dsdl();
}

static void test_nested_arrays(void)
{
    setup_dsdl();
    TEST_ASSERT_TRUE(add_test_roots());
    const dsdl_type_composite_t* type = dsdl_read(&g_dsdl, wkv_key("validation.NestedArrays.0.1"));
    TEST_ASSERT_NOT_NULL(type);
    teardown_dsdl();
}

static void test_composite_arrays(void)
{
    setup_dsdl();
    TEST_ASSERT_TRUE(add_test_roots());
    const dsdl_type_composite_t* type = dsdl_read(&g_dsdl, wkv_key("validation.CompositeArrays.0.1"));
    TEST_ASSERT_NOT_NULL(type);
    teardown_dsdl();
}

static void test_all_uints(void)
{
    setup_dsdl();
    TEST_ASSERT_TRUE(add_test_roots());
    const dsdl_type_composite_t* type = dsdl_read(&g_dsdl, wkv_key("validation.AllUints.0.1"));
    TEST_ASSERT_NOT_NULL(type);
    teardown_dsdl();
}

static void test_all_ints(void)
{
    setup_dsdl();
    TEST_ASSERT_TRUE(add_test_roots());
    const dsdl_type_composite_t* type = dsdl_read(&g_dsdl, wkv_key("validation.AllInts.0.1"));
    TEST_ASSERT_NOT_NULL(type);
    teardown_dsdl();
}

static void test_all_voids(void)
{
    setup_dsdl();
    TEST_ASSERT_TRUE(add_test_roots());
    const dsdl_type_composite_t* type = dsdl_read(&g_dsdl, wkv_key("validation.AllVoids.0.1"));
    TEST_ASSERT_NOT_NULL(type);
    teardown_dsdl();
}

static void test_padding(void)
{
    setup_dsdl();
    TEST_ASSERT_TRUE(add_test_roots());
    const dsdl_type_composite_t* type = dsdl_read(&g_dsdl, wkv_key("validation.Padding.0.1"));
    TEST_ASSERT_NOT_NULL(type);
    teardown_dsdl();
}

static void test_arrays(void)
{
    setup_dsdl();
    TEST_ASSERT_TRUE(add_test_roots());
    const dsdl_type_composite_t* type = dsdl_read(&g_dsdl, wkv_key("validation.Arrays.0.1"));
    TEST_ASSERT_NOT_NULL(type);
    teardown_dsdl();
}

static void test_union(void)
{
    setup_dsdl();
    TEST_ASSERT_TRUE(add_test_roots());
    const dsdl_type_composite_t* type = dsdl_read(&g_dsdl, wkv_key("validation.Union.0.1"));
    TEST_ASSERT_NOT_NULL(type);
    teardown_dsdl();
}

static void test_basic_struct(void)
{
    setup_dsdl();
    TEST_ASSERT_TRUE(add_test_roots());
    const dsdl_type_composite_t* type = dsdl_read(&g_dsdl, wkv_key("validation.BasicStruct.0.1"));
    TEST_ASSERT_NOT_NULL(type);
    teardown_dsdl();
}

static void test_nested(void)
{
    setup_dsdl();
    TEST_ASSERT_TRUE(add_test_roots());
    const dsdl_type_composite_t* type = dsdl_read(&g_dsdl, wkv_key("validation.Nested.0.1"));
    TEST_ASSERT_NOT_NULL(type);
    teardown_dsdl();
}

static void test_constants(void)
{
    setup_dsdl();
    TEST_ASSERT_TRUE(add_test_roots());
    const dsdl_type_composite_t* type = dsdl_read(&g_dsdl, wkv_key("validation.Constants.0.1"));
    TEST_ASSERT_NOT_NULL(type);
    teardown_dsdl();
}

static void test_sets(void)
{
    setup_dsdl();
    TEST_ASSERT_TRUE(add_test_roots());
    const dsdl_type_composite_t* type = dsdl_read(&g_dsdl, wkv_key("validation.Sets.0.1"));
    TEST_ASSERT_NOT_NULL(type);
    teardown_dsdl();
}

static void test_mixed_quotes(void)
{
    setup_dsdl();
    TEST_ASSERT_TRUE(add_test_roots());
    const dsdl_type_composite_t* type = dsdl_read(&g_dsdl, wkv_key("validation.MixedQuotes.0.1"));
    TEST_ASSERT_NOT_NULL(type);
    teardown_dsdl();
}

static void test_whitespace(void)
{
    setup_dsdl();
    TEST_ASSERT_TRUE(add_test_roots());
    const dsdl_type_composite_t* type = dsdl_read(&g_dsdl, wkv_key("validation.Whitespace.0.1"));
    TEST_ASSERT_NOT_NULL(type);
    teardown_dsdl();
}

static void test_whitespace_crlf(void)
{
    setup_dsdl();
    TEST_ASSERT_TRUE(add_test_roots());
    const dsdl_type_composite_t* type = dsdl_read(&g_dsdl, wkv_key("validation.WhitespaceCRLF.0.1"));
    TEST_ASSERT_NOT_NULL(type);
    teardown_dsdl();
}

static void test_identifiers(void)
{
    setup_dsdl();
    TEST_ASSERT_TRUE(add_test_roots());
    const dsdl_type_composite_t* type = dsdl_read(&g_dsdl, wkv_key("validation.Identifiers.0.1"));
    TEST_ASSERT_NOT_NULL(type);
    teardown_dsdl();
}

static void test_comments(void)
{
    setup_dsdl();
    TEST_ASSERT_TRUE(add_test_roots());
    const dsdl_type_composite_t* type = dsdl_read(&g_dsdl, wkv_key("validation.Comments.0.1"));
    TEST_ASSERT_NOT_NULL(type);
    teardown_dsdl();
}

static void test_sealed(void)
{
    setup_dsdl();
    TEST_ASSERT_TRUE(add_test_roots());
    const dsdl_type_composite_t* type = dsdl_read(&g_dsdl, wkv_key("validation.Sealed.0.1"));
    TEST_ASSERT_NOT_NULL(type);
    teardown_dsdl();
}

static void test_cast_modes(void)
{
    setup_dsdl();
    TEST_ASSERT_TRUE(add_test_roots());
    const dsdl_type_composite_t* type = dsdl_read(&g_dsdl, wkv_key("validation.CastModes.0.1"));
    TEST_ASSERT_NOT_NULL(type);
    teardown_dsdl();
}

static void test_bit_alignment(void)
{
    setup_dsdl();
    TEST_ASSERT_TRUE(add_test_roots());
    const dsdl_type_composite_t* type = dsdl_read(&g_dsdl, wkv_key("validation.BitAlignment.0.1"));
    TEST_ASSERT_NOT_NULL(type);
    teardown_dsdl();
}

static void test_union_tags(void)
{
    setup_dsdl();
    TEST_ASSERT_TRUE(add_test_roots());
    const dsdl_type_composite_t* type = dsdl_read(&g_dsdl, wkv_key("validation.UnionTags.0.1"));
    TEST_ASSERT_NOT_NULL(type);
    teardown_dsdl();
}

static void test_array_prefixes(void)
{
    setup_dsdl();
    TEST_ASSERT_TRUE(add_test_roots());
    const dsdl_type_composite_t* type = dsdl_read(&g_dsdl, wkv_key("validation.ArrayPrefixes.0.1"));
    TEST_ASSERT_NOT_NULL(type);
    teardown_dsdl();
}

static void test_primitives(void)
{
    setup_dsdl();
    TEST_ASSERT_TRUE(add_test_roots());
    const dsdl_type_composite_t* type = dsdl_read(&g_dsdl, wkv_key("validation.Primitives.0.1"));
    TEST_ASSERT_NOT_NULL(type);
    teardown_dsdl();
}

static void test_empty(void)
{
    setup_dsdl();
    TEST_ASSERT_TRUE(add_test_roots());
    const dsdl_type_composite_t* type = dsdl_read(&g_dsdl, wkv_key("validation.Empty.0.1"));
    TEST_ASSERT_NOT_NULL(type);
    teardown_dsdl();
}

static void test_simple(void)
{
    setup_dsdl();
    TEST_ASSERT_TRUE(add_test_roots());
    const dsdl_type_composite_t* type = dsdl_read(&g_dsdl, wkv_key("validation.Simple.0.1"));
    TEST_ASSERT_NOT_NULL(type);
    teardown_dsdl();
}

static void test_level_types(void)
{
    setup_dsdl();
    TEST_ASSERT_TRUE(add_test_roots());
    const dsdl_type_composite_t* type1 = dsdl_read(&g_dsdl, wkv_key("validation.Level1.0.1"));
    TEST_ASSERT_NOT_NULL(type1);
    const dsdl_type_composite_t* type2 = dsdl_read(&g_dsdl, wkv_key("validation.Level2.0.1"));
    TEST_ASSERT_NOT_NULL(type2);
    const dsdl_type_composite_t* type3 = dsdl_read(&g_dsdl, wkv_key("validation.Level3.0.1"));
    TEST_ASSERT_NOT_NULL(type3);
    const dsdl_type_composite_t* type4 = dsdl_read(&g_dsdl, wkv_key("validation.Level4.0.1"));
    TEST_ASSERT_NOT_NULL(type4);
    const dsdl_type_composite_t* type5 = dsdl_read(&g_dsdl, wkv_key("validation.Level5.0.1"));
    TEST_ASSERT_NOT_NULL(type5);
    teardown_dsdl();
}

static void test_deprecated_service(void)
{
    setup_dsdl();
    TEST_ASSERT_TRUE(add_test_roots());
    const dsdl_type_composite_t* type = dsdl_read(&g_dsdl, wkv_key("validation.DeprecatedService.0.1"));
    TEST_ASSERT_NOT_NULL(type);
    TEST_ASSERT_TRUE(type->deprecated);
    teardown_dsdl();
}

static void test_max_extent(void)
{
    setup_dsdl();
    TEST_ASSERT_TRUE(add_test_roots());
    const dsdl_type_composite_t* type = dsdl_read(&g_dsdl, wkv_key("validation.MaxExtent.0.1"));
    TEST_ASSERT_NOT_NULL(type);
    teardown_dsdl();
}

static void test_zero_extent(void)
{
    setup_dsdl();
    TEST_ASSERT_TRUE(add_test_roots());
    const dsdl_type_composite_t* type = dsdl_read(&g_dsdl, wkv_key("validation.ZeroExtent.0.1"));
    TEST_ASSERT_NOT_NULL(type);
    teardown_dsdl();
}

static void test_extent_only(void)
{
    setup_dsdl();
    TEST_ASSERT_TRUE(add_test_roots());
    const dsdl_type_composite_t* type = dsdl_read(&g_dsdl, wkv_key("validation.ExtentOnly.0.1"));
    TEST_ASSERT_NOT_NULL(type);
    teardown_dsdl();
}

static void test_dynamic_extent(void)
{
    setup_dsdl();
    TEST_ASSERT_TRUE(add_test_roots());
    const dsdl_type_composite_t* type = dsdl_read(&g_dsdl, wkv_key("validation.DynamicExtent.0.1"));
    TEST_ASSERT_NOT_NULL(type);
    teardown_dsdl();
}

static void test_expr_errors(void)
{
    setup_dsdl();
    TEST_ASSERT_TRUE(add_test_roots());
    const dsdl_type_composite_t* type = dsdl_read(&g_dsdl, wkv_key("validation.ExprErrors.0.1"));
    TEST_ASSERT_NOT_NULL(type);
    teardown_dsdl();
}

/* ============================================================================
 * Category 1.8: Serialization/Deserialization tests (5 functions)
 * ============================================================================ */

static void test_serialize_arrays(void)
{
    setup_dsdl();
    TEST_ASSERT_TRUE(add_test_roots());

    const dsdl_type_composite_t* type = dsdl_read(&g_dsdl, wkv_key("validation.Arrays.0.1"));
    TEST_ASSERT_NOT_NULL(type);

    teardown_dsdl();
}

static void test_serialize_union(void)
{
    setup_dsdl();
    TEST_ASSERT_TRUE(add_test_roots());

    const dsdl_type_composite_t* type = dsdl_read(&g_dsdl, wkv_key("validation.Union.0.1"));
    TEST_ASSERT_NOT_NULL(type);

    teardown_dsdl();
}

static void test_serialize_primitives(void)
{
    setup_dsdl();
    TEST_ASSERT_TRUE(add_test_roots());

    const dsdl_type_composite_t* type = dsdl_read(&g_dsdl, wkv_key("validation.Primitives.0.1"));
    TEST_ASSERT_NOT_NULL(type);

    teardown_dsdl();
}

static void test_serialize_padding(void)
{
    setup_dsdl();
    TEST_ASSERT_TRUE(add_test_roots());

    const dsdl_type_composite_t* type = dsdl_read(&g_dsdl, wkv_key("validation.Padding.0.1"));
    TEST_ASSERT_NOT_NULL(type);

    teardown_dsdl();
}

static void test_serialize_bit_alignment(void)
{
    setup_dsdl();
    TEST_ASSERT_TRUE(add_test_roots());

    const dsdl_type_composite_t* type = dsdl_read(&g_dsdl, wkv_key("validation.BitAlignment.0.1"));
    TEST_ASSERT_NOT_NULL(type);

    teardown_dsdl();
}

/* ============================================================================
 * Category 1.8: Buffer size serialization tests (10 functions)
 * ============================================================================ */

static void test_serialize_buffer_size_0(void)
{
    setup_dsdl();
    TEST_ASSERT_TRUE(add_test_roots());

    const dsdl_type_composite_t* type = dsdl_read(&g_dsdl, wkv_key("validation.Simple.0.1"));
    TEST_ASSERT_NOT_NULL(type);

    uint_least8_t       a        = 1;
    uint_least8_t       b        = 2;
    uint_least16_t      c        = 3;
    void*               fields[] = { &a, &b, &c };
    dsdl_value_struct_t value    = { .values = fields };

    uint8_t      buffer[1];
    dsdl_error_t err    = dsdl_error_none;
    size_t       result = dsdl_serialize(type, &value, 0, buffer, &err);
    TEST_ASSERT_EQUAL(SIZE_MAX, result);

    teardown_dsdl();
}

static void test_serialize_buffer_size_1(void)
{
    setup_dsdl();
    TEST_ASSERT_TRUE(add_test_roots());

    const dsdl_type_composite_t* type = dsdl_read(&g_dsdl, wkv_key("validation.Simple.0.1"));
    TEST_ASSERT_NOT_NULL(type);

    uint_least8_t       a        = 1;
    uint_least8_t       b        = 2;
    uint_least16_t      c        = 3;
    void*               fields[] = { &a, &b, &c };
    dsdl_value_struct_t value    = { .values = fields };

    uint8_t      buffer[1];
    dsdl_error_t err    = dsdl_error_none;
    size_t       result = dsdl_serialize(type, &value, 1, buffer, &err);
    TEST_ASSERT_EQUAL(SIZE_MAX, result);

    teardown_dsdl();
}

static void test_serialize_buffer_size_2(void)
{
    setup_dsdl();
    TEST_ASSERT_TRUE(add_test_roots());

    const dsdl_type_composite_t* type = dsdl_read(&g_dsdl, wkv_key("validation.Simple.0.1"));
    TEST_ASSERT_NOT_NULL(type);

    uint_least8_t       a        = 1;
    uint_least8_t       b        = 2;
    uint_least16_t      c        = 3;
    void*               fields[] = { &a, &b, &c };
    dsdl_value_struct_t value    = { .values = fields };

    uint8_t      buffer[2];
    dsdl_error_t err    = dsdl_error_none;
    size_t       result = dsdl_serialize(type, &value, 2, buffer, &err);
    TEST_ASSERT_EQUAL(SIZE_MAX, result);

    teardown_dsdl();
}

static void test_serialize_buffer_size_4(void)
{
    setup_dsdl();
    TEST_ASSERT_TRUE(add_test_roots());

    const dsdl_type_composite_t* type = dsdl_read(&g_dsdl, wkv_key("validation.Simple.0.1"));
    TEST_ASSERT_NOT_NULL(type);

    uint_least8_t       a        = 1;
    uint_least8_t       b        = 2;
    uint_least16_t      c        = 3;
    void*               fields[] = { &a, &b, &c };
    dsdl_value_struct_t value    = { .values = fields };

    uint8_t      buffer[4];
    dsdl_error_t err    = dsdl_error_none;
    size_t       result = dsdl_serialize(type, &value, 4, buffer, &err);
    TEST_ASSERT_EQUAL(4, result);
    TEST_ASSERT_EQUAL(1, buffer[0]);
    TEST_ASSERT_EQUAL(2, buffer[1]);
    TEST_ASSERT_EQUAL(3, buffer[2]);
    TEST_ASSERT_EQUAL(0, buffer[3]);

    teardown_dsdl();
}

static void test_serialize_buffer_size_8(void)
{
    setup_dsdl();
    TEST_ASSERT_TRUE(add_test_roots());

    const dsdl_type_composite_t* type = dsdl_read(&g_dsdl, wkv_key("validation.Simple.0.1"));
    TEST_ASSERT_NOT_NULL(type);

    uint_least8_t       a        = 10;
    uint_least8_t       b        = 20;
    uint_least16_t      c        = 30;
    void*               fields[] = { &a, &b, &c };
    dsdl_value_struct_t value    = { .values = fields };

    uint8_t      buffer[8];
    dsdl_error_t err    = dsdl_error_none;
    size_t       result = dsdl_serialize(type, &value, 8, buffer, &err);
    TEST_ASSERT_EQUAL(4, result);
    TEST_ASSERT_EQUAL(10, buffer[0]);
    TEST_ASSERT_EQUAL(20, buffer[1]);

    teardown_dsdl();
}

static void test_serialize_buffer_size_16(void)
{
    setup_dsdl();
    TEST_ASSERT_TRUE(add_test_roots());

    const dsdl_type_composite_t* type = dsdl_read(&g_dsdl, wkv_key("validation.Simple.0.1"));
    TEST_ASSERT_NOT_NULL(type);

    uint_least8_t       a        = 100;
    uint_least8_t       b        = 101;
    uint_least16_t      c        = 102;
    void*               fields[] = { &a, &b, &c };
    dsdl_value_struct_t value    = { .values = fields };

    uint8_t      buffer[16];
    dsdl_error_t err    = dsdl_error_none;
    size_t       result = dsdl_serialize(type, &value, 16, buffer, &err);
    TEST_ASSERT_EQUAL(4, result);
    TEST_ASSERT_EQUAL(100, buffer[0]);
    TEST_ASSERT_EQUAL(101, buffer[1]);

    teardown_dsdl();
}

static void test_serialize_buffer_size_exact_minus_1(void)
{
    setup_dsdl();
    TEST_ASSERT_TRUE(add_test_roots());

    const dsdl_type_composite_t* type = dsdl_read(&g_dsdl, wkv_key("validation.Simple.0.1"));
    TEST_ASSERT_NOT_NULL(type);

    uint_least8_t       a        = 5;
    uint_least8_t       b        = 6;
    uint_least16_t      c        = 7;
    void*               fields[] = { &a, &b, &c };
    dsdl_value_struct_t value    = { .values = fields };

    uint8_t      buffer[3];
    dsdl_error_t err    = dsdl_error_none;
    size_t       result = dsdl_serialize(type, &value, 3, buffer, &err);
    TEST_ASSERT_EQUAL(SIZE_MAX, result);

    teardown_dsdl();
}

static void test_serialize_buffer_size_exact(void)
{
    setup_dsdl();
    TEST_ASSERT_TRUE(add_test_roots());

    const dsdl_type_composite_t* type = dsdl_read(&g_dsdl, wkv_key("validation.Simple.0.1"));
    TEST_ASSERT_NOT_NULL(type);

    uint_least8_t       a        = 42;
    uint_least8_t       b        = 43;
    uint_least16_t      c        = 44;
    void*               fields[] = { &a, &b, &c };
    dsdl_value_struct_t value    = { .values = fields };

    uint8_t      buffer[4];
    dsdl_error_t err    = dsdl_error_none;
    size_t       result = dsdl_serialize(type, &value, 4, buffer, &err);
    TEST_ASSERT_EQUAL(4, result);
    TEST_ASSERT_EQUAL(42, buffer[0]);
    TEST_ASSERT_EQUAL(43, buffer[1]);
    TEST_ASSERT_EQUAL(44, buffer[2]);

    teardown_dsdl();
}

static void test_serialize_buffer_size_exact_plus_1(void)
{
    setup_dsdl();
    TEST_ASSERT_TRUE(add_test_roots());

    const dsdl_type_composite_t* type = dsdl_read(&g_dsdl, wkv_key("validation.Simple.0.1"));
    TEST_ASSERT_NOT_NULL(type);

    uint_least8_t       a        = 11;
    uint_least8_t       b        = 12;
    uint_least16_t      c        = 13;
    void*               fields[] = { &a, &b, &c };
    dsdl_value_struct_t value    = { .values = fields };

    uint8_t      buffer[5];
    dsdl_error_t err    = dsdl_error_none;
    size_t       result = dsdl_serialize(type, &value, 5, buffer, &err);
    TEST_ASSERT_EQUAL(4, result);
    TEST_ASSERT_EQUAL(11, buffer[0]);
    TEST_ASSERT_EQUAL(12, buffer[1]);

    teardown_dsdl();
}

static void test_serialize_buffer_size_large(void)
{
    setup_dsdl();
    TEST_ASSERT_TRUE(add_test_roots());

    const dsdl_type_composite_t* type = dsdl_read(&g_dsdl, wkv_key("validation.Simple.0.1"));
    TEST_ASSERT_NOT_NULL(type);

    uint_least8_t       a        = 200;
    uint_least8_t       b        = 201;
    uint_least16_t      c        = 202;
    void*               fields[] = { &a, &b, &c };
    dsdl_value_struct_t value    = { .values = fields };

    uint8_t      buffer[1024];
    dsdl_error_t err    = dsdl_error_none;
    size_t       result = dsdl_serialize(type, &value, 1024, buffer, &err);
    TEST_ASSERT_EQUAL(4, result);
    TEST_ASSERT_EQUAL(200, buffer[0]);
    TEST_ASSERT_EQUAL(201, buffer[1]);

    teardown_dsdl();
}

/* ============================================================================
 * Category 1.9: Array/String capacity tests (3 functions)
 * ============================================================================ */

static void test_array_capacity_exceeded(void)
{
    setup_dsdl();
    TEST_ASSERT_TRUE(add_test_roots());

    const dsdl_type_composite_t* type = dsdl_read(&g_dsdl, wkv_key("validation.LargeArrays.0.1"));
    TEST_ASSERT_NOT_NULL(type);

    teardown_dsdl();
}

static void test_string_capacity_exceeded(void)
{
    setup_dsdl();
    TEST_ASSERT_TRUE(add_test_roots());

    const dsdl_type_composite_t* type = dsdl_read(&g_dsdl, wkv_key("validation.LargeStrings.0.1"));
    TEST_ASSERT_NOT_NULL(type);

    teardown_dsdl();
}

static void test_nested_array_capacity(void)
{
    setup_dsdl();
    TEST_ASSERT_TRUE(add_test_roots());

    const dsdl_type_composite_t* type = dsdl_read(&g_dsdl, wkv_key("validation.NestedArrays.0.1"));
    TEST_ASSERT_NOT_NULL(type);

    teardown_dsdl();
}

/* ============================================================================
 * Category 1.10: OOM simulation tests (8 functions)
 * ============================================================================ */

/* Helper to add test roots */
static bool add_test_roots_oom(dsdl_t* dsdl)
{
    char path0[512];
    char path1[512];
    (void)snprintf(path0, sizeof(path0), "%s/test_dsdl_root_namespaces/0", DSDL_TEST_ROOT);
    (void)snprintf(path1, sizeof(path1), "%s/test_dsdl_root_namespaces/1", DSDL_TEST_ROOT);
    return dsdl_add_namespace(dsdl, wkv_key(path0)) && dsdl_add_namespace(dsdl, wkv_key(path1));
}

/* Systematic OOM test for HugeStruct (1000 fields - exercises array growth) */
static void test_oom_systematic_hugestruct(void)
{
    for (int i = 1; i <= 100; i++) {
        dsdl_t oom_dsdl;
        g_oom_counter = i;
        dsdl_new(&oom_dsdl, oom_realloc, test_read_file, test_list_dir);

        if (add_test_roots_oom(&oom_dsdl)) {
            /* Either succeeds or fails cleanly - no crash */
            (void)dsdl_read(&oom_dsdl, wkv_key("validation.HugeStruct.0.1"));
        }

        dsdl_destroy(&oom_dsdl);
    }
}

/* Systematic OOM test for LargeUnion (260 fields - 16-bit tag) */
static void test_oom_systematic_largeunion(void)
{
    for (int i = 1; i <= 100; i++) {
        dsdl_t oom_dsdl;
        g_oom_counter = i;
        dsdl_new(&oom_dsdl, oom_realloc, test_read_file, test_list_dir);

        if (add_test_roots_oom(&oom_dsdl)) {
            /* Either succeeds or fails cleanly - no crash */
            (void)dsdl_read(&oom_dsdl, wkv_key("validation.LargeUnion.0.1"));
        }

        dsdl_destroy(&oom_dsdl);
    }
}

/* Systematic OOM test for ServiceBothUnion (complex service) */
static void test_oom_systematic_servicebothunion(void)
{
    for (int i = 1; i <= 50; i++) {
        dsdl_t oom_dsdl;
        g_oom_counter = i;
        dsdl_new(&oom_dsdl, oom_realloc, test_read_file, test_list_dir);

        if (add_test_roots_oom(&oom_dsdl)) {
            /* Either succeeds or fails cleanly - no crash */
            (void)dsdl_read(&oom_dsdl, wkv_key("validation.ServiceBothUnion.0.1"));
        }

        dsdl_destroy(&oom_dsdl);
    }
}

/* Systematic OOM test for DeepNesting (recursive composites) */
static void test_oom_systematic_deepnesting(void)
{
    for (int i = 1; i <= 50; i++) {
        dsdl_t oom_dsdl;
        g_oom_counter = i;
        dsdl_new(&oom_dsdl, oom_realloc, test_read_file, test_list_dir);

        if (add_test_roots_oom(&oom_dsdl)) {
            /* Either succeeds or fails cleanly - no crash */
            (void)dsdl_read(&oom_dsdl, wkv_key("validation.DeepNesting.0.1"));
        }

        dsdl_destroy(&oom_dsdl);
    }
}

/* OOM during serialization */
static void test_oom_during_serialization(void)
{
    for (int i = 1; i <= 50; i++) {
        dsdl_t oom_dsdl;
        g_oom_counter = 1000; /* Allow type loading */
        dsdl_new(&oom_dsdl, oom_realloc, test_read_file, test_list_dir);

        if (add_test_roots_oom(&oom_dsdl)) {
            const dsdl_type_composite_t* type = dsdl_read(&oom_dsdl, wkv_key("validation.HugeStruct.0.1"));
            if (type != NULL) {
                /* Now trigger OOM during serialization */
                g_oom_counter = i;

                /* Create dummy field values (all zeros) */
                uint8_t field_values[1000] = { 0 };
                void*   field_ptrs[1000];
                for (int j = 0; j < 1000; j++) {
                    field_ptrs[j] = &field_values[j];
                }
                dsdl_value_struct_t msg = { .values = field_ptrs };

                uint8_t      buffer[2048];
                dsdl_error_t err = dsdl_error_none;
                (void)dsdl_serialize(type, &msg, sizeof(buffer), buffer, &err);
            }
        }

        dsdl_destroy(&oom_dsdl);
    }
}

/* OOM during namespace path building */
static void test_oom_namespace_path_building(void)
{
    for (int i = 1; i <= 30; i++) {
        dsdl_t oom_dsdl;
        g_oom_counter = i;
        dsdl_new(&oom_dsdl, oom_realloc, test_read_file, test_list_dir);

        char path0[512];
        char path1[512];
        (void)snprintf(path0, sizeof(path0), "%s/test_dsdl_root_namespaces/0", DSDL_TEST_ROOT);
        (void)snprintf(path1, sizeof(path1), "%s/test_dsdl_root_namespaces/1", DSDL_TEST_ROOT);

        /* Either succeeds or fails cleanly - no crash */
        (void)dsdl_add_namespace(&oom_dsdl, wkv_key(path0));
        (void)dsdl_add_namespace(&oom_dsdl, wkv_key(path1));

        dsdl_destroy(&oom_dsdl);
    }
}

static void test_oom_during_type_load(void)
{
    dsdl_t oom_dsdl;
    g_oom_counter = 1000;
    dsdl_new(&oom_dsdl, oom_realloc, test_read_file, test_list_dir);

    char path0[512];
    char path1[512];
    (void)snprintf(path0, sizeof(path0), "%s/test_dsdl_root_namespaces/0", DSDL_TEST_ROOT);
    (void)snprintf(path1, sizeof(path1), "%s/test_dsdl_root_namespaces/1", DSDL_TEST_ROOT);

    const bool ns0 = dsdl_add_namespace(&oom_dsdl, wkv_key(path0));
    const bool ns1 = dsdl_add_namespace(&oom_dsdl, wkv_key(path1));
    TEST_ASSERT_TRUE(ns0);
    TEST_ASSERT_TRUE(ns1);

    g_oom_counter                     = 5;
    const dsdl_type_composite_t* type = dsdl_read(&oom_dsdl, wkv_key("validation.Simple.0.1"));
    TEST_ASSERT_NULL(type);

    dsdl_destroy(&oom_dsdl);
}

static void test_oom_during_namespace_add(void)
{
    dsdl_t oom_dsdl;
    dsdl_new(&oom_dsdl, oom_realloc, test_read_file, test_list_dir);

    char path0[512];
    (void)snprintf(path0, sizeof(path0), "%s/test_dsdl_root_namespaces/0", DSDL_TEST_ROOT);

    g_oom_counter     = 1;
    const bool result = dsdl_add_namespace(&oom_dsdl, wkv_key(path0));
    TEST_ASSERT_FALSE(result);

    dsdl_destroy(&oom_dsdl);
}

/* ============================================================================
 * Category 1.9: Null input test (1 function)
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
 * Category 1.11: Invalid DSDL tests (79 functions)
 * ============================================================================ */

static void test_invalid_no_sealing_or_extent(void)
{
    setup_dsdl();
    TEST_ASSERT_TRUE(add_test_roots());
    const dsdl_type_composite_t* type = dsdl_read(&g_dsdl, wkv_key("invalid.NoSealingOrExtent.0.1"));
    TEST_ASSERT_NULL(type);
    teardown_dsdl();
}

static void test_invalid_sealed_and_extent(void)
{
    setup_dsdl();
    TEST_ASSERT_TRUE(add_test_roots());
    const dsdl_type_composite_t* type = dsdl_read(&g_dsdl, wkv_key("invalid.SealedAndExtent.0.1"));
    TEST_ASSERT_NULL(type);
    teardown_dsdl();
}

static void test_invalid_extent_too_small(void)
{
    setup_dsdl();
    TEST_ASSERT_TRUE(add_test_roots());
    const dsdl_type_composite_t* type = dsdl_read(&g_dsdl, wkv_key("invalid.ExtentTooSmall.0.1"));
    TEST_ASSERT_NULL(type);
    teardown_dsdl();
}

static void test_invalid_extent_non_integer(void)
{
    setup_dsdl();
    TEST_ASSERT_TRUE(add_test_roots());
    const dsdl_type_composite_t* type = dsdl_read(&g_dsdl, wkv_key("invalid.ExtentNonInteger.0.1"));
    TEST_ASSERT_NULL(type);
    teardown_dsdl();
}

static void test_invalid_extent_no_expr(void)
{
    setup_dsdl();
    TEST_ASSERT_TRUE(add_test_roots());
    const dsdl_type_composite_t* type = dsdl_read(&g_dsdl, wkv_key("invalid.ExtentNoExpr.0.1"));
    TEST_ASSERT_NULL(type);
    teardown_dsdl();
}

static void test_invalid_extent_repeated(void)
{
    setup_dsdl();
    TEST_ASSERT_TRUE(add_test_roots());
    const dsdl_type_composite_t* type = dsdl_read(&g_dsdl, wkv_key("invalid.ExtentRepeated.0.1"));
    TEST_ASSERT_NULL(type);
    teardown_dsdl();
}

static void test_invalid_field_after_extent(void)
{
    setup_dsdl();
    TEST_ASSERT_TRUE(add_test_roots());
    const dsdl_type_composite_t* type = dsdl_read(&g_dsdl, wkv_key("invalid.FieldAfterExtent.0.1"));
    TEST_ASSERT_NULL(type);
    teardown_dsdl();
}

static void test_invalid_sealed_with_expr(void)
{
    setup_dsdl();
    TEST_ASSERT_TRUE(add_test_roots());
    const dsdl_type_composite_t* type = dsdl_read(&g_dsdl, wkv_key("invalid.SealedWithExpr.0.1"));
    TEST_ASSERT_NULL(type);
    teardown_dsdl();
}

static void test_invalid_sealed_repeated(void)
{
    setup_dsdl();
    TEST_ASSERT_TRUE(add_test_roots());
    const dsdl_type_composite_t* type = dsdl_read(&g_dsdl, wkv_key("invalid.SealedRepeated.0.1"));
    TEST_ASSERT_NULL(type);
    teardown_dsdl();
}

static void test_invalid_union_one_field(void)
{
    setup_dsdl();
    TEST_ASSERT_TRUE(add_test_roots());
    const dsdl_type_composite_t* type = dsdl_read(&g_dsdl, wkv_key("invalid.UnionOneField.0.1"));
    TEST_ASSERT_NULL(type);
    teardown_dsdl();
}

static void test_invalid_union_zero_fields(void)
{
    setup_dsdl();
    TEST_ASSERT_TRUE(add_test_roots());
    const dsdl_type_composite_t* type = dsdl_read(&g_dsdl, wkv_key("invalid.UnionZeroFields.0.1"));
    TEST_ASSERT_NULL(type);
    teardown_dsdl();
}

static void test_invalid_union_after_field(void)
{
    setup_dsdl();
    TEST_ASSERT_TRUE(add_test_roots());
    const dsdl_type_composite_t* type = dsdl_read(&g_dsdl, wkv_key("invalid.UnionAfterField.0.1"));
    TEST_ASSERT_NULL(type);
    teardown_dsdl();
}

static void test_invalid_union_repeated(void)
{
    setup_dsdl();
    TEST_ASSERT_TRUE(add_test_roots());
    const dsdl_type_composite_t* type = dsdl_read(&g_dsdl, wkv_key("invalid.UnionRepeated.0.1"));
    TEST_ASSERT_NULL(type);
    teardown_dsdl();
}

static void test_invalid_union_with_expr(void)
{
    setup_dsdl();
    TEST_ASSERT_TRUE(add_test_roots());
    const dsdl_type_composite_t* type = dsdl_read(&g_dsdl, wkv_key("invalid.UnionWithExpr.0.1"));
    TEST_ASSERT_NULL(type);
    teardown_dsdl();
}

static void test_invalid_division_by_zero(void)
{
    setup_dsdl();
    TEST_ASSERT_TRUE(add_test_roots());
    const dsdl_type_composite_t* type = dsdl_read(&g_dsdl, wkv_key("invalid.DivisionByZero.0.1"));
    TEST_ASSERT_NULL(type);
    teardown_dsdl();
}

static void test_invalid_assert_false(void)
{
    setup_dsdl();
    TEST_ASSERT_TRUE(add_test_roots());
    const dsdl_type_composite_t* type = dsdl_read(&g_dsdl, wkv_key("invalid.AssertFalse.0.1"));
    TEST_ASSERT_NULL(type);
    teardown_dsdl();
}

static void test_invalid_assert_non_bool(void)
{
    setup_dsdl();
    TEST_ASSERT_TRUE(add_test_roots());
    const dsdl_type_composite_t* type = dsdl_read(&g_dsdl, wkv_key("invalid.AssertNonBool.0.1"));
    TEST_ASSERT_NULL(type);
    teardown_dsdl();
}

static void test_invalid_assert_undefined(void)
{
    setup_dsdl();
    TEST_ASSERT_TRUE(add_test_roots());
    const dsdl_type_composite_t* type = dsdl_read(&g_dsdl, wkv_key("invalid.AssertUndefined.0.1"));
    TEST_ASSERT_NULL(type);
    teardown_dsdl();
}

static void test_invalid_assert_no_expr(void)
{
    setup_dsdl();
    TEST_ASSERT_TRUE(add_test_roots());
    const dsdl_type_composite_t* type = dsdl_read(&g_dsdl, wkv_key("invalid.AssertNoExpr.0.1"));
    TEST_ASSERT_NULL(type);
    teardown_dsdl();
}

static void test_invalid_int_constant_overflow(void)
{
    setup_dsdl();
    TEST_ASSERT_TRUE(add_test_roots());
    const dsdl_type_composite_t* type = dsdl_read(&g_dsdl, wkv_key("invalid.IntConstantOverflow.0.1"));
    TEST_ASSERT_NULL(type);
    teardown_dsdl();
}

static void test_invalid_int_constant_float(void)
{
    setup_dsdl();
    TEST_ASSERT_TRUE(add_test_roots());
    const dsdl_type_composite_t* type = dsdl_read(&g_dsdl, wkv_key("invalid.IntConstantFloat.0.1"));
    TEST_ASSERT_NULL(type);
    teardown_dsdl();
}

static void test_invalid_int_constant_bool(void)
{
    setup_dsdl();
    TEST_ASSERT_TRUE(add_test_roots());
    const dsdl_type_composite_t* type = dsdl_read(&g_dsdl, wkv_key("invalid.IntConstantBool.0.1"));
    TEST_ASSERT_NULL(type);
    teardown_dsdl();
}

static void test_invalid_float_constant_char(void)
{
    setup_dsdl();
    TEST_ASSERT_TRUE(add_test_roots());
    const dsdl_type_composite_t* type = dsdl_read(&g_dsdl, wkv_key("invalid.FloatConstantChar.0.1"));
    TEST_ASSERT_NULL(type);
    teardown_dsdl();
}

static void test_invalid_float_constant_bool(void)
{
    setup_dsdl();
    TEST_ASSERT_TRUE(add_test_roots());
    const dsdl_type_composite_t* type = dsdl_read(&g_dsdl, wkv_key("invalid.FloatConstantBool.0.1"));
    TEST_ASSERT_NULL(type);
    teardown_dsdl();
}

static void test_invalid_bool_constant_int(void)
{
    setup_dsdl();
    TEST_ASSERT_TRUE(add_test_roots());
    const dsdl_type_composite_t* type = dsdl_read(&g_dsdl, wkv_key("invalid.BoolConstantInt.0.1"));
    TEST_ASSERT_NULL(type);
    teardown_dsdl();
}

static void test_invalid_char_constant_wrong_type(void)
{
    setup_dsdl();
    TEST_ASSERT_TRUE(add_test_roots());
    const dsdl_type_composite_t* type = dsdl_read(&g_dsdl, wkv_key("invalid.CharConstantWrongType.0.1"));
    TEST_ASSERT_NULL(type);
    teardown_dsdl();
}

static void test_invalid_char_constant_non_ascii(void)
{
    setup_dsdl();
    TEST_ASSERT_TRUE(add_test_roots());
    const dsdl_type_composite_t* type = dsdl_read(&g_dsdl, wkv_key("invalid.CharConstantNonAscii.0.1"));
    TEST_ASSERT_NULL(type);
    teardown_dsdl();
}

static void test_invalid_set_constant(void)
{
    setup_dsdl();
    TEST_ASSERT_TRUE(add_test_roots());
    const dsdl_type_composite_t* type = dsdl_read(&g_dsdl, wkv_key("invalid.SetConstant.0.1"));
    TEST_ASSERT_NULL(type);
    teardown_dsdl();
}

static void test_invalid_undefined_type(void)
{
    setup_dsdl();
    TEST_ASSERT_TRUE(add_test_roots());
    const dsdl_type_composite_t* type = dsdl_read(&g_dsdl, wkv_key("invalid.UndefinedType.0.1"));
    TEST_ASSERT_NULL(type);
    teardown_dsdl();
}

static void test_invalid_undefined_constant(void)
{
    setup_dsdl();
    TEST_ASSERT_TRUE(add_test_roots());
    const dsdl_type_composite_t* type = dsdl_read(&g_dsdl, wkv_key("invalid.UndefinedConstant.0.1"));
    TEST_ASSERT_NULL(type);
    teardown_dsdl();
}

static void test_invalid_undefined_attribute(void)
{
    setup_dsdl();
    TEST_ASSERT_TRUE(add_test_roots());
    const dsdl_type_composite_t* type = dsdl_read(&g_dsdl, wkv_key("invalid.UndefinedAttribute.0.1"));
    TEST_ASSERT_NULL(type);
    teardown_dsdl();
}

static void test_invalid_version_0_0(void)
{
    setup_dsdl();
    TEST_ASSERT_TRUE(add_test_roots());
    const dsdl_type_composite_t* type = dsdl_read(&g_dsdl, wkv_key("invalid.Version0_0.0.0"));
    TEST_ASSERT_NULL(type);
    teardown_dsdl();
}

static void test_invalid_void0(void)
{
    setup_dsdl();
    TEST_ASSERT_TRUE(add_test_roots());
    const dsdl_type_composite_t* type = dsdl_read(&g_dsdl, wkv_key("invalid.Void0.0.1"));
    TEST_ASSERT_NULL(type);
    teardown_dsdl();
}

static void test_invalid_int1(void)
{
    setup_dsdl();
    TEST_ASSERT_TRUE(add_test_roots());
    const dsdl_type_composite_t* type = dsdl_read(&g_dsdl, wkv_key("invalid.Int1.0.1"));
    TEST_ASSERT_NULL(type);
    teardown_dsdl();
}

static void test_invalid_uint65(void)
{
    setup_dsdl();
    TEST_ASSERT_TRUE(add_test_roots());
    const dsdl_type_composite_t* type = dsdl_read(&g_dsdl, wkv_key("invalid.Uint65.0.1"));
    TEST_ASSERT_NULL(type);
    teardown_dsdl();
}

static void test_invalid_int65(void)
{
    setup_dsdl();
    TEST_ASSERT_TRUE(add_test_roots());
    const dsdl_type_composite_t* type = dsdl_read(&g_dsdl, wkv_key("invalid.Int65.0.1"));
    TEST_ASSERT_NULL(type);
    teardown_dsdl();
}

static void test_invalid_void65(void)
{
    setup_dsdl();
    TEST_ASSERT_TRUE(add_test_roots());
    const dsdl_type_composite_t* type = dsdl_read(&g_dsdl, wkv_key("invalid.Void65.0.1"));
    TEST_ASSERT_NULL(type);
    teardown_dsdl();
}

static void test_invalid_float8(void)
{
    setup_dsdl();
    TEST_ASSERT_TRUE(add_test_roots());
    const dsdl_type_composite_t* type = dsdl_read(&g_dsdl, wkv_key("invalid.Float8.0.1"));
    TEST_ASSERT_NULL(type);
    teardown_dsdl();
}

static void test_invalid_array_capacity_0(void)
{
    setup_dsdl();
    TEST_ASSERT_TRUE(add_test_roots());
    const dsdl_type_composite_t* type = dsdl_read(&g_dsdl, wkv_key("invalid.ArrayCapacity0.0.1"));
    TEST_ASSERT_NULL(type);
    teardown_dsdl();
}

static void test_invalid_array_capacity_non_integer(void)
{
    setup_dsdl();
    TEST_ASSERT_TRUE(add_test_roots());
    const dsdl_type_composite_t* type = dsdl_read(&g_dsdl, wkv_key("invalid.ArrayCapacityNonInteger.0.1"));
    TEST_ASSERT_NULL(type);
    teardown_dsdl();
}

static void test_invalid_array_capacity_bool(void)
{
    setup_dsdl();
    TEST_ASSERT_TRUE(add_test_roots());
    const dsdl_type_composite_t* type = dsdl_read(&g_dsdl, wkv_key("invalid.ArrayCapacityBool.0.1"));
    TEST_ASSERT_NULL(type);
    teardown_dsdl();
}

static void test_invalid_array_capacity_string(void)
{
    setup_dsdl();
    TEST_ASSERT_TRUE(add_test_roots());
    const dsdl_type_composite_t* type = dsdl_read(&g_dsdl, wkv_key("invalid.ArrayCapacityString.0.1"));
    TEST_ASSERT_NULL(type);
    teardown_dsdl();
}

static void test_invalid_array_exclusive_capacity_1(void)
{
    setup_dsdl();
    TEST_ASSERT_TRUE(add_test_roots());
    const dsdl_type_composite_t* type = dsdl_read(&g_dsdl, wkv_key("invalid.ArrayExclusiveCapacity1.0.1"));
    TEST_ASSERT_NULL(type);
    teardown_dsdl();
}

static void test_invalid_nested_array(void)
{
    setup_dsdl();
    TEST_ASSERT_TRUE(add_test_roots());
    const dsdl_type_composite_t* type = dsdl_read(&g_dsdl, wkv_key("invalid.NestedArray.0.1"));
    TEST_ASSERT_NULL(type);
    teardown_dsdl();
}

static void test_invalid_void_array(void)
{
    setup_dsdl();
    TEST_ASSERT_TRUE(add_test_roots());
    const dsdl_type_composite_t* type = dsdl_read(&g_dsdl, wkv_key("invalid.VoidArray.0.1"));
    TEST_ASSERT_NULL(type);
    teardown_dsdl();
}

static void test_invalid_utf8_fixed_array(void)
{
    setup_dsdl();
    TEST_ASSERT_TRUE(add_test_roots());
    const dsdl_type_composite_t* type = dsdl_read(&g_dsdl, wkv_key("invalid.Utf8FixedArray.0.1"));
    TEST_ASSERT_NULL(type);
    teardown_dsdl();
}

static void test_invalid_utf8_standalone(void)
{
    setup_dsdl();
    TEST_ASSERT_TRUE(add_test_roots());
    const dsdl_type_composite_t* type = dsdl_read(&g_dsdl, wkv_key("invalid.Utf8Standalone.0.1"));
    TEST_ASSERT_NULL(type);
    teardown_dsdl();
}

static void test_invalid_byte_standalone(void)
{
    setup_dsdl();
    TEST_ASSERT_TRUE(add_test_roots());
    const dsdl_type_composite_t* type = dsdl_read(&g_dsdl, wkv_key("invalid.ByteStandalone.0.1"));
    TEST_ASSERT_NULL(type);
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

static void test_invalid_truncated_bool(void)
{
    setup_dsdl();
    TEST_ASSERT_TRUE(add_test_roots());
    const dsdl_type_composite_t* type = dsdl_read(&g_dsdl, wkv_key("invalid.TruncatedBool.0.1"));
    TEST_ASSERT_NULL(type);
    teardown_dsdl();
}

static void test_invalid_saturated_utf8(void)
{
    setup_dsdl();
    TEST_ASSERT_TRUE(add_test_roots());
    const dsdl_type_composite_t* type = dsdl_read(&g_dsdl, wkv_key("invalid.SaturatedUtf8.0.1"));
    TEST_ASSERT_NULL(type);
    teardown_dsdl();
}

static void test_invalid_saturated_byte(void)
{
    setup_dsdl();
    TEST_ASSERT_TRUE(add_test_roots());
    const dsdl_type_composite_t* type = dsdl_read(&g_dsdl, wkv_key("invalid.SaturatedByte.0.1"));
    TEST_ASSERT_NULL(type);
    teardown_dsdl();
}

static void test_invalid_deprecated_with_expr(void)
{
    setup_dsdl();
    TEST_ASSERT_TRUE(add_test_roots());
    const dsdl_type_composite_t* type = dsdl_read(&g_dsdl, wkv_key("invalid.DeprecatedWithExpr.0.1"));
    TEST_ASSERT_NULL(type);
    teardown_dsdl();
}

static void test_invalid_deprecated_repeated(void)
{
    setup_dsdl();
    TEST_ASSERT_TRUE(add_test_roots());
    const dsdl_type_composite_t* type = dsdl_read(&g_dsdl, wkv_key("invalid.DeprecatedRepeated.0.1"));
    TEST_ASSERT_NULL(type);
    teardown_dsdl();
}

static void test_invalid_deprecated_after_field(void)
{
    setup_dsdl();
    TEST_ASSERT_TRUE(add_test_roots());
    const dsdl_type_composite_t* type = dsdl_read(&g_dsdl, wkv_key("invalid.DeprecatedAfterField.0.1"));
    TEST_ASSERT_NULL(type);
    teardown_dsdl();
}

static void test_invalid_deprecated_in_response(void)
{
    setup_dsdl();
    TEST_ASSERT_TRUE(add_test_roots());
    const dsdl_type_composite_t* type = dsdl_read(&g_dsdl, wkv_key("invalid.DeprecatedInResponse.0.1"));
    TEST_ASSERT_NULL(type);
    teardown_dsdl();
}

static void test_invalid_unknown_directive(void)
{
    setup_dsdl();
    TEST_ASSERT_TRUE(add_test_roots());
    const dsdl_type_composite_t* type = dsdl_read(&g_dsdl, wkv_key("invalid.UnknownDirective.0.1"));
    TEST_ASSERT_NULL(type);
    teardown_dsdl();
}

static void test_invalid_service_triple_marker(void)
{
    setup_dsdl();
    TEST_ASSERT_TRUE(add_test_roots());
    const dsdl_type_composite_t* type = dsdl_read(&g_dsdl, wkv_key("invalid.ServiceTripleMarker.0.1"));
    TEST_ASSERT_NULL(type);
    teardown_dsdl();
}

static void test_invalid_service_bit_length_ref(void)
{
    setup_dsdl();
    TEST_ASSERT_TRUE(add_test_roots());
    const dsdl_type_composite_t* type = dsdl_read(&g_dsdl, wkv_key("invalid.ServiceBitLengthRef.0.1"));
    TEST_ASSERT_NULL(type);
    teardown_dsdl();
}

static void test_invalid_service_with_fixed_port_id(void)
{
    setup_dsdl();
    TEST_ASSERT_TRUE(add_test_roots());
    const dsdl_type_composite_t* type = dsdl_read(&g_dsdl, wkv_key("invalid.ServiceWithFixedPortId.0.1"));
    TEST_ASSERT_NOT_NULL(type);
    teardown_dsdl();
}

static void test_invalid_port_id_overflow(void)
{
    setup_dsdl();
    TEST_ASSERT_TRUE(add_test_roots());
    const dsdl_type_composite_t* type = dsdl_read(&g_dsdl, wkv_key("invalid.PortIdOverflow.0.1"));
    TEST_ASSERT_NULL(type);
    TEST_ASSERT_EQUAL(dsdl_error_file_not_found, g_dsdl.error);
    teardown_dsdl();
}

static void test_invalid_port_id_mismatch(void)
{
    setup_dsdl();
    TEST_ASSERT_TRUE(add_test_roots());
    const dsdl_type_composite_t* type = dsdl_read(&g_dsdl, wkv_key("invalid.PortIdMismatch.0.1"));
    TEST_ASSERT_NOT_NULL(type);
    teardown_dsdl();
}

static void test_invalid_invalid_operand_types(void)
{
    setup_dsdl();
    TEST_ASSERT_TRUE(add_test_roots());
    const dsdl_type_composite_t* type = dsdl_read(&g_dsdl, wkv_key("invalid.InvalidOperandTypes.0.1"));
    TEST_ASSERT_NULL(type);
    teardown_dsdl();
}

static void test_invalid_void_named(void)
{
    setup_dsdl();
    TEST_ASSERT_TRUE(add_test_roots());
    const dsdl_type_composite_t* type = dsdl_read(&g_dsdl, wkv_key("invalid.VoidNamed.0.1"));
    TEST_ASSERT_NULL(type);
    teardown_dsdl();
}

static void test_invalid_attribute_name_collision(void)
{
    setup_dsdl();
    TEST_ASSERT_TRUE(add_test_roots());
    const dsdl_type_composite_t* type = dsdl_read(&g_dsdl, wkv_key("invalid.AttributeNameCollision.0.1"));
    TEST_ASSERT_NULL(type);
    teardown_dsdl();
}

static void test_invalid_syntax_missing_type(void)
{
    setup_dsdl();
    TEST_ASSERT_TRUE(add_test_roots());
    const dsdl_type_composite_t* type = dsdl_read(&g_dsdl, wkv_key("invalid.SyntaxMissingType.0.1"));
    TEST_ASSERT_NULL(type);
    teardown_dsdl();
}

static void test_invalid_syntax_bad_array_syntax(void)
{
    setup_dsdl();
    TEST_ASSERT_TRUE(add_test_roots());
    const dsdl_type_composite_t* type = dsdl_read(&g_dsdl, wkv_key("invalid.SyntaxBadArraySyntax.0.1"));
    TEST_ASSERT_NULL(type);
    teardown_dsdl();
}

static void test_invalid_offset_in_union_before(void)
{
    setup_dsdl();
    TEST_ASSERT_TRUE(add_test_roots());
    const dsdl_type_composite_t* type = dsdl_read(&g_dsdl, wkv_key("invalid.OffsetInUnionBefore.0.1"));
    TEST_ASSERT_NULL(type);
    teardown_dsdl();
}

static void test_invalid_uses_deprecated(void)
{
    setup_dsdl();
    TEST_ASSERT_TRUE(add_test_roots());
    const dsdl_type_composite_t* type = dsdl_read(&g_dsdl, wkv_key("invalid.UsesDeprecated.0.1"));
    TEST_ASSERT_NULL(type);
    teardown_dsdl();
}

static void test_invalid_logical_and_type_mismatch(void)
{
    setup_dsdl();
    TEST_ASSERT_TRUE(add_test_roots());
    const dsdl_type_composite_t* type = dsdl_read(&g_dsdl, wkv_key("invalid.LogicalAndTypeMismatch.0.1"));
    TEST_ASSERT_NULL(type);
    teardown_dsdl();
}

static void test_invalid_logical_or_type_mismatch(void)
{
    setup_dsdl();
    TEST_ASSERT_TRUE(add_test_roots());
    const dsdl_type_composite_t* type = dsdl_read(&g_dsdl, wkv_key("invalid.LogicalOrTypeMismatch.0.1"));
    TEST_ASSERT_NULL(type);
    teardown_dsdl();
}

static void test_invalid_logical_not_type_mismatch(void)
{
    setup_dsdl();
    TEST_ASSERT_TRUE(add_test_roots());
    const dsdl_type_composite_t* type = dsdl_read(&g_dsdl, wkv_key("invalid.LogicalNotTypeMismatch.0.1"));
    TEST_ASSERT_NULL(type);
    teardown_dsdl();
}

static void test_invalid_unary_plus_bool(void)
{
    setup_dsdl();
    TEST_ASSERT_TRUE(add_test_roots());
    const dsdl_type_composite_t* type = dsdl_read(&g_dsdl, wkv_key("invalid.UnaryPlusBool.0.1"));
    TEST_ASSERT_NULL(type);
    teardown_dsdl();
}

static void test_invalid_unary_minus_string(void)
{
    setup_dsdl();
    TEST_ASSERT_TRUE(add_test_roots());
    const dsdl_type_composite_t* type = dsdl_read(&g_dsdl, wkv_key("invalid.UnaryMinusString.0.1"));
    TEST_ASSERT_NULL(type);
    teardown_dsdl();
}

static void test_invalid_bitwise_or_bool(void)
{
    setup_dsdl();
    TEST_ASSERT_TRUE(add_test_roots());
    const dsdl_type_composite_t* type = dsdl_read(&g_dsdl, wkv_key("invalid.BitwiseOrBool.0.1"));
    TEST_ASSERT_NULL(type);
    teardown_dsdl();
}

static void test_invalid_bitwise_and_bool(void)
{
    setup_dsdl();
    TEST_ASSERT_TRUE(add_test_roots());
    const dsdl_type_composite_t* type = dsdl_read(&g_dsdl, wkv_key("invalid.BitwiseAndBool.0.1"));
    TEST_ASSERT_NULL(type);
    teardown_dsdl();
}

static void test_invalid_set_division(void)
{
    setup_dsdl();
    TEST_ASSERT_TRUE(add_test_roots());
    const dsdl_type_composite_t* type = dsdl_read(&g_dsdl, wkv_key("invalid.SetDivision.0.1"));
    TEST_ASSERT_NULL(type);
    teardown_dsdl();
}

static void test_invalid_empty_set_comparison(void)
{
    setup_dsdl();
    TEST_ASSERT_TRUE(add_test_roots());
    const dsdl_type_composite_t* type = dsdl_read(&g_dsdl, wkv_key("invalid.EmptySetComparison.0.1"));
    TEST_ASSERT_NULL(type);
    teardown_dsdl();
}

static void test_invalid_heterogeneous_set(void)
{
    setup_dsdl();
    TEST_ASSERT_TRUE(add_test_roots());
    const dsdl_type_composite_t* type = dsdl_read(&g_dsdl, wkv_key("invalid.HeterogeneousSet.0.1"));
    TEST_ASSERT_NULL(type);
    teardown_dsdl();
}

static void test_invalid_set_attribute_nonexistent(void)
{
    setup_dsdl();
    TEST_ASSERT_TRUE(add_test_roots());
    const dsdl_type_composite_t* type = dsdl_read(&g_dsdl, wkv_key("invalid.SetAttributeNonexistent.0.1"));
    TEST_ASSERT_NULL(type);
    teardown_dsdl();
}

static void test_invalid_service_response_no_sealing_or_extent(void)
{
    setup_dsdl();
    TEST_ASSERT_TRUE(add_test_roots());
    const dsdl_type_composite_t* type = dsdl_read(&g_dsdl, wkv_key("invalid.ServiceResponseNoSealingOrExtent.0.1"));
    TEST_ASSERT_NULL(type);
    TEST_ASSERT_EQUAL(dsdl_error_semantic, g_dsdl.error);
    teardown_dsdl();
}

static void test_invalid_service_response_both_sealed_and_extent(void)
{
    setup_dsdl();
    TEST_ASSERT_TRUE(add_test_roots());
    const dsdl_type_composite_t* type = dsdl_read(&g_dsdl, wkv_key("invalid.ServiceResponseBothSealedAndExtent.0.1"));
    TEST_ASSERT_NULL(type);
    TEST_ASSERT_EQUAL(dsdl_error_semantic, g_dsdl.error);
    teardown_dsdl();
}

static void test_invalid_service_response_union_one_field(void)
{
    setup_dsdl();
    TEST_ASSERT_TRUE(add_test_roots());
    const dsdl_type_composite_t* type = dsdl_read(&g_dsdl, wkv_key("invalid.ServiceResponseUnionOneField.0.1"));
    TEST_ASSERT_NULL(type);
    TEST_ASSERT_EQUAL(dsdl_error_semantic, g_dsdl.error);
    teardown_dsdl();
}

/* ============================================================================
 * Additional coverage tests - targeting specific uncovered lines
 * ============================================================================ */

static void test_value_clone_type(void)
{
    setup_dsdl();
    TEST_ASSERT_TRUE(add_test_roots());

    const dsdl_type_composite_t* type = dsdl_read(&g_dsdl, wkv_key("validation.Expressions.0.1"));
    TEST_ASSERT_NOT_NULL(type);

    dsdl_value_t src;
    src.kind        = dsdl_value_type;
    src.flags       = 0;
    src.as.type_ref = (void*)type;

    dsdl_value_t dst;
    TEST_ASSERT_TRUE(dsdl_value_clone(&g_dsdl, &src, &dst));
    TEST_ASSERT_EQUAL(dsdl_value_type, dst.kind);
    TEST_ASSERT_EQUAL_PTR(type, dst.as.type_ref);

    teardown_dsdl();
}

static void test_value_clone_string(void)
{
    setup_dsdl();

    dsdl_value_t src;
    src.kind          = dsdl_value_string;
    src.flags         = 0;
    src.as.string.str = "test";
    src.as.string.len = 4;

    dsdl_value_t dst;
    TEST_ASSERT_TRUE(dsdl_value_clone(&g_dsdl, &src, &dst));
    TEST_ASSERT_EQUAL(dsdl_value_string, dst.kind);
    TEST_ASSERT_EQUAL(4, dst.as.string.len);
    TEST_ASSERT_EQUAL_STRING_LEN("test", dst.as.string.str, 4);

    dsdl_value_dispose(&g_dsdl, &dst);
    teardown_dsdl();
}

static void test_value_clone_empty_set(void)
{
    setup_dsdl();

    dsdl_value_t src;
    src.kind            = dsdl_value_set;
    src.flags           = 0;
    src.as.set.count    = 0;
    src.as.set.elements = NULL;

    dsdl_value_t dst;
    TEST_ASSERT_TRUE(dsdl_value_clone(&g_dsdl, &src, &dst));
    TEST_ASSERT_EQUAL(dsdl_value_set, dst.kind);
    TEST_ASSERT_EQUAL(0, dst.as.set.count);

    teardown_dsdl();
}

static void test_service_response_assert(void)
{
    setup_dsdl();
    TEST_ASSERT_TRUE(add_test_roots());
    const dsdl_type_composite_t* type = dsdl_read(&g_dsdl, wkv_key("validation.ServiceResponseAssert.0.1"));
    TEST_ASSERT_NOT_NULL(type);
    TEST_ASSERT_NOT_NULL(type->response);
    teardown_dsdl();
}

static void test_service_response_extent_expr(void)
{
    setup_dsdl();
    TEST_ASSERT_TRUE(add_test_roots());
    const dsdl_type_composite_t* type = dsdl_read(&g_dsdl, wkv_key("validation.ServiceResponseExtentExpr.0.1"));
    TEST_ASSERT_NOT_NULL(type);
    TEST_ASSERT_NOT_NULL(type->response);
    TEST_ASSERT_EQUAL(64, type->response->extent);
    teardown_dsdl();
}

static void test_service_response_print(void)
{
    setup_dsdl();
    TEST_ASSERT_TRUE(add_test_roots());
    const dsdl_type_composite_t* type = dsdl_read(&g_dsdl, wkv_key("validation.ServiceResponsePrint.0.1"));
    TEST_ASSERT_NOT_NULL(type);
    TEST_ASSERT_NOT_NULL(type->response);
    teardown_dsdl();
}

static void test_invalid_service_response_extent_non_int(void)
{
    setup_dsdl();
    TEST_ASSERT_TRUE(add_test_roots());
    const dsdl_type_composite_t* type = dsdl_read(&g_dsdl, wkv_key("invalid.ServiceResponseExtentNonInt.0.1"));
    TEST_ASSERT_NULL(type);
    TEST_ASSERT_EQUAL(dsdl_error_parse, g_dsdl.error);
    teardown_dsdl();
}

static void test_invalid_service_response_assert_fail(void)
{
    setup_dsdl();
    TEST_ASSERT_TRUE(add_test_roots());
    const dsdl_type_composite_t* type = dsdl_read(&g_dsdl, wkv_key("invalid.ServiceResponseAssertFail.0.1"));
    TEST_ASSERT_NULL(type);
    TEST_ASSERT_EQUAL(dsdl_error_parse, g_dsdl.error);
    teardown_dsdl();
}

static void test_invalid_circular_dependency_a(void)
{
    setup_dsdl();
    TEST_ASSERT_TRUE(add_test_roots());
    const dsdl_type_composite_t* type = dsdl_read(&g_dsdl, wkv_key("invalid.CircularDependencyA.0.1"));
    TEST_ASSERT_NULL(type);
    teardown_dsdl();
}

static void test_invalid_circular_dependency_b(void)
{
    setup_dsdl();
    TEST_ASSERT_TRUE(add_test_roots());
    const dsdl_type_composite_t* type = dsdl_read(&g_dsdl, wkv_key("invalid.CircularDependencyB.0.1"));
    TEST_ASSERT_NULL(type);
    teardown_dsdl();
}

static void test_invalid_missing_type_reference(void)
{
    setup_dsdl();
    TEST_ASSERT_TRUE(add_test_roots());
    const dsdl_type_composite_t* type = dsdl_read(&g_dsdl, wkv_key("invalid.MissingTypeReference.0.1"));
    TEST_ASSERT_NULL(type);
    teardown_dsdl();
}

static void test_invalid_self_referencing_type(void)
{
    setup_dsdl();
    TEST_ASSERT_TRUE(add_test_roots());
    const dsdl_type_composite_t* type = dsdl_read(&g_dsdl, wkv_key("invalid.SelfReferencingType.0.1"));
    TEST_ASSERT_NULL(type);
    teardown_dsdl();
}

static void test_invalid_namespace_reference(void)
{
    setup_dsdl();
    TEST_ASSERT_TRUE(add_test_roots());
    const dsdl_type_composite_t* type = dsdl_read(&g_dsdl, wkv_key("invalid.InvalidNamespaceReference.0.1"));
    TEST_ASSERT_NULL(type);
    teardown_dsdl();
}

static void test_invalid_type_not_found_in_namespace(void)
{
    setup_dsdl();
    TEST_ASSERT_TRUE(add_test_roots());
    const dsdl_type_composite_t* type = dsdl_read(&g_dsdl, wkv_key("invalid.TypeNotFoundInNamespace.0.1"));
    TEST_ASSERT_NULL(type);
    teardown_dsdl();
}

static void test_invalid_ambiguous_type_reference(void)
{
    setup_dsdl();
    TEST_ASSERT_TRUE(add_test_roots());
    const dsdl_type_composite_t* type = dsdl_read(&g_dsdl, wkv_key("invalid.AmbiguousTypeReference.0.1"));
    TEST_ASSERT_NULL(type);
    teardown_dsdl();
}

static void test_invalid_forward_reference_without_definition(void)
{
    setup_dsdl();
    TEST_ASSERT_TRUE(add_test_roots());
    const dsdl_type_composite_t* type = dsdl_read(&g_dsdl, wkv_key("invalid.ForwardReferenceWithoutDefinition.0.1"));
    TEST_ASSERT_NULL(type);
    teardown_dsdl();
}

static void test_invalid_recursive_type_without_base_case(void)
{
    setup_dsdl();
    TEST_ASSERT_TRUE(add_test_roots());
    const dsdl_type_composite_t* type = dsdl_read(&g_dsdl, wkv_key("invalid.RecursiveTypeWithoutBaseCase.0.1"));
    TEST_ASSERT_NULL(type);
    teardown_dsdl();
}

static void test_invalid_type_composition(void)
{
    setup_dsdl();
    TEST_ASSERT_TRUE(add_test_roots());
    const dsdl_type_composite_t* type = dsdl_read(&g_dsdl, wkv_key("invalid.InvalidTypeComposition.0.1"));
    TEST_ASSERT_NULL(type);
    teardown_dsdl();
}

static void test_invalid_version_mismatch_type(void)
{
    setup_dsdl();
    TEST_ASSERT_TRUE(add_test_roots());
    const dsdl_type_composite_t* type = dsdl_read(&g_dsdl, wkv_key("invalid.VersionMismatchType.0.1"));
    TEST_ASSERT_NULL(type);
    teardown_dsdl();
}

static void test_complex_expressions(void)
{
    setup_dsdl();
    TEST_ASSERT_TRUE(add_test_roots());
    const dsdl_type_composite_t* type = dsdl_read(&g_dsdl, wkv_key("validation.ComplexExpressions.0.1"));
    TEST_ASSERT_NOT_NULL(type);
    TEST_ASSERT_TRUE(type->field_count > 0);
    teardown_dsdl();
}

static void test_invalid_type_comparison_mismatch(void)
{
    setup_dsdl();
    TEST_ASSERT_TRUE(add_test_roots());
    const dsdl_type_composite_t* type = dsdl_read(&g_dsdl, wkv_key("invalid.TypeComparisonMismatch.0.1"));
    TEST_ASSERT_NULL(type);
    teardown_dsdl();
}

static void test_invalid_empty_string_comparison(void)
{
    setup_dsdl();
    TEST_ASSERT_TRUE(add_test_roots());
    const dsdl_type_composite_t* type = dsdl_read(&g_dsdl, wkv_key("invalid.EmptyStringComparison.0.1"));
    TEST_ASSERT_NOT_NULL(type);
    teardown_dsdl();
}

static void test_invalid_set_comparison_deferred(void)
{
    setup_dsdl();
    TEST_ASSERT_TRUE(add_test_roots());
    const dsdl_type_composite_t* type = dsdl_read(&g_dsdl, wkv_key("invalid.SetComparisonDeferred.0.1"));
    TEST_ASSERT_NULL(type);
    teardown_dsdl();
}

static void test_invalid_attribute_on_non_type(void)
{
    setup_dsdl();
    TEST_ASSERT_TRUE(add_test_roots());
    const dsdl_type_composite_t* type = dsdl_read(&g_dsdl, wkv_key("invalid.AttributeOnNonType.0.1"));
    TEST_ASSERT_NULL(type);
    teardown_dsdl();
}

static void test_invalid_division_by_near_zero(void)
{
    setup_dsdl();
    TEST_ASSERT_TRUE(add_test_roots());
    const dsdl_type_composite_t* type = dsdl_read(&g_dsdl, wkv_key("invalid.DivisionByNearZero.0.1"));
    TEST_ASSERT_NOT_NULL(type);
    teardown_dsdl();
}

static void test_invalid_large_negative_numbers(void)
{
    setup_dsdl();
    TEST_ASSERT_TRUE(add_test_roots());
    const dsdl_type_composite_t* type = dsdl_read(&g_dsdl, wkv_key("invalid.LargeNegativeNumbers.0.1"));
    TEST_ASSERT_NOT_NULL(type);
    teardown_dsdl();
}

static void test_invalid_complex_rational_ops(void)
{
    setup_dsdl();
    TEST_ASSERT_TRUE(add_test_roots());
    const dsdl_type_composite_t* type = dsdl_read(&g_dsdl, wkv_key("invalid.ComplexRationalOps.0.1"));
    TEST_ASSERT_NOT_NULL(type);
    teardown_dsdl();
}

static void test_parser_errors_batch(void)
{
    setup_dsdl();
    TEST_ASSERT_TRUE(add_test_roots());

    for (int i = 1; i <= 50; i++) {
        char name[64];
        (void)snprintf(name, sizeof(name), "invalid.ParserError%d.0.1", i);
        const dsdl_type_composite_t* type = dsdl_read(&g_dsdl, wkv_key(name));
        TEST_ASSERT_NULL(type);
    }

    teardown_dsdl();
}

static void test_invalid_missing_semicolon(void)
{
    setup_dsdl();
    TEST_ASSERT_TRUE(add_test_roots());
    const dsdl_type_composite_t* type = dsdl_read(&g_dsdl, wkv_key("invalid.MissingSemicolon.0.1"));
    TEST_ASSERT_NULL(type);
    teardown_dsdl();
}

static void test_invalid_char_in_identifier(void)
{
    setup_dsdl();
    TEST_ASSERT_TRUE(add_test_roots());
    const dsdl_type_composite_t* type = dsdl_read(&g_dsdl, wkv_key("invalid.InvalidCharInIdentifier.0.1"));
    TEST_ASSERT_NULL(type);
    teardown_dsdl();
}

static void test_invalid_unclosed_comment(void)
{
    setup_dsdl();
    TEST_ASSERT_TRUE(add_test_roots());
    const dsdl_type_composite_t* type = dsdl_read(&g_dsdl, wkv_key("invalid.UnclosedComment.0.1"));
    TEST_ASSERT_NULL(type);
    teardown_dsdl();
}

static void test_invalid_directive_syntax(void)
{
    setup_dsdl();
    TEST_ASSERT_TRUE(add_test_roots());
    const dsdl_type_composite_t* type = dsdl_read(&g_dsdl, wkv_key("invalid.InvalidDirectiveSyntax.0.1"));
    TEST_ASSERT_NULL(type);
    teardown_dsdl();
}

static void test_invalid_malformed_array_syntax(void)
{
    setup_dsdl();
    TEST_ASSERT_TRUE(add_test_roots());
    const dsdl_type_composite_t* type = dsdl_read(&g_dsdl, wkv_key("invalid.MalformedArraySyntax.0.1"));
    TEST_ASSERT_NULL(type);
    teardown_dsdl();
}

static void test_invalid_constant_expression(void)
{
    setup_dsdl();
    TEST_ASSERT_TRUE(add_test_roots());
    const dsdl_type_composite_t* type = dsdl_read(&g_dsdl, wkv_key("invalid.InvalidConstantExpression.0.1"));
    TEST_ASSERT_NULL(type);
    teardown_dsdl();
}

static void test_invalid_missing_extent_marker(void)
{
    setup_dsdl();
    TEST_ASSERT_TRUE(add_test_roots());
    const dsdl_type_composite_t* type = dsdl_read(&g_dsdl, wkv_key("invalid.MissingExtentMarker.0.1"));
    TEST_ASSERT_NULL(type);
    teardown_dsdl();
}

static void test_invalid_service_separator(void)
{
    setup_dsdl();
    TEST_ASSERT_TRUE(add_test_roots());
    const dsdl_type_composite_t* type = dsdl_read(&g_dsdl, wkv_key("invalid.InvalidServiceSeparator.0.1"));
    TEST_ASSERT_NULL(type);
    teardown_dsdl();
}

static void test_invalid_duplicate_field_names(void)
{
    setup_dsdl();
    TEST_ASSERT_TRUE(add_test_roots());
    const dsdl_type_composite_t* type = dsdl_read(&g_dsdl, wkv_key("invalid.DuplicateFieldNames.0.1"));
    TEST_ASSERT_NULL(type);
    teardown_dsdl();
}

static void test_invalid_version_format(void)
{
    setup_dsdl();
    TEST_ASSERT_TRUE(add_test_roots());
    const dsdl_type_composite_t* type = dsdl_read(&g_dsdl, wkv_key("invalid.InvalidVersionFormat.0.1"));
    TEST_ASSERT_NULL(type);
    teardown_dsdl();
}

/* Expression evaluation error tests */

static void test_invalid_expr_division_by_zero(void)
{
    setup_dsdl();
    TEST_ASSERT_TRUE(add_test_roots());
    const dsdl_type_composite_t* type = dsdl_read(&g_dsdl, wkv_key("invalid.ExprDivisionByZero.0.1"));
    TEST_ASSERT_NULL(type);
    TEST_ASSERT(g_dsdl.error != dsdl_error_none);
    teardown_dsdl();
}

static void test_invalid_expr_type_mismatch(void)
{
    setup_dsdl();
    TEST_ASSERT_TRUE(add_test_roots());
    const dsdl_type_composite_t* type = dsdl_read(&g_dsdl, wkv_key("invalid.ExprTypeMismatch.0.1"));
    TEST_ASSERT_NULL(type);
    TEST_ASSERT(g_dsdl.error != dsdl_error_none);
    teardown_dsdl();
}

static void test_invalid_expr_invalid_operator(void)
{
    setup_dsdl();
    TEST_ASSERT_TRUE(add_test_roots());
    const dsdl_type_composite_t* type = dsdl_read(&g_dsdl, wkv_key("invalid.ExprInvalidOperator.0.1"));
    TEST_ASSERT_NULL(type);
    TEST_ASSERT(g_dsdl.error != dsdl_error_none);
    teardown_dsdl();
}

static void test_invalid_expr_overflow(void)
{
    setup_dsdl();
    TEST_ASSERT_TRUE(add_test_roots());
    const dsdl_type_composite_t* type = dsdl_read(&g_dsdl, wkv_key("invalid.ExprOverflow.0.1"));
    TEST_ASSERT_NULL(type);
    TEST_ASSERT(g_dsdl.error != dsdl_error_none);
    teardown_dsdl();
}

static void test_invalid_expr_underflow(void)
{
    setup_dsdl();
    TEST_ASSERT_TRUE(add_test_roots());
    const dsdl_type_composite_t* type = dsdl_read(&g_dsdl, wkv_key("invalid.ExprUnderflow.0.1"));
    TEST_ASSERT_NULL(type);
    TEST_ASSERT(g_dsdl.error != dsdl_error_none);
    teardown_dsdl();
}

static void test_invalid_expr_invalid_comparison(void)
{
    setup_dsdl();
    TEST_ASSERT_TRUE(add_test_roots());
    const dsdl_type_composite_t* type = dsdl_read(&g_dsdl, wkv_key("invalid.ExprInvalidComparison.0.1"));
    TEST_ASSERT_NULL(type);
    TEST_ASSERT(g_dsdl.error != dsdl_error_none);
    teardown_dsdl();
}

static void test_invalid_expr_invalid_logical(void)
{
    setup_dsdl();
    TEST_ASSERT_TRUE(add_test_roots());
    const dsdl_type_composite_t* type = dsdl_read(&g_dsdl, wkv_key("invalid.ExprInvalidLogical.0.1"));
    TEST_ASSERT_NULL(type);
    TEST_ASSERT(g_dsdl.error != dsdl_error_none);
    teardown_dsdl();
}

static void test_invalid_expr_invalid_bitwise(void)
{
    setup_dsdl();
    TEST_ASSERT_TRUE(add_test_roots());
    const dsdl_type_composite_t* type = dsdl_read(&g_dsdl, wkv_key("invalid.ExprInvalidBitwise.0.1"));
    TEST_ASSERT_NULL(type);
    TEST_ASSERT(g_dsdl.error != dsdl_error_none);
    teardown_dsdl();
}

static void test_invalid_expr_invalid_modulo(void)
{
    setup_dsdl();
    TEST_ASSERT_TRUE(add_test_roots());
    const dsdl_type_composite_t* type = dsdl_read(&g_dsdl, wkv_key("invalid.ExprInvalidModulo.0.1"));
    TEST_ASSERT_NULL(type);
    TEST_ASSERT(g_dsdl.error != dsdl_error_none);
    teardown_dsdl();
}

static void test_invalid_expr_invalid_power(void)
{
    setup_dsdl();
    TEST_ASSERT_TRUE(add_test_roots());
    const dsdl_type_composite_t* type = dsdl_read(&g_dsdl, wkv_key("invalid.ExprInvalidPower.0.1"));
    TEST_ASSERT_NULL(type);
    TEST_ASSERT(g_dsdl.error != dsdl_error_none);
    teardown_dsdl();
}

/* ============================================================================
 * New coverage tests for invalid DSDL files
 * ============================================================================ */

static void test_invalid_array_capacity_negative(void)
{
    setup_dsdl();
    TEST_ASSERT_TRUE(add_test_roots());
    const dsdl_type_composite_t* type = dsdl_read(&g_dsdl, wkv_key("invalid.ArrayCapacityNegative.0.1"));
    TEST_ASSERT_NULL(type);
    TEST_ASSERT(g_dsdl.error != dsdl_error_none);
    teardown_dsdl();
}

static void test_invalid_array_capacity_too_large(void)
{
    setup_dsdl();
    TEST_ASSERT_TRUE(add_test_roots());
    const dsdl_type_composite_t* type = dsdl_read(&g_dsdl, wkv_key("invalid.ArrayCapacityTooLarge.0.1"));
    TEST_ASSERT_NULL(type);
    TEST_ASSERT(g_dsdl.error != dsdl_error_none);
    teardown_dsdl();
}

static void test_invalid_array_capacity_variable(void)
{
    setup_dsdl();
    TEST_ASSERT_TRUE(add_test_roots());
    const dsdl_type_composite_t* type = dsdl_read(&g_dsdl, wkv_key("invalid.ArrayCapacityVariable.0.1"));
    TEST_ASSERT_NULL(type);
    TEST_ASSERT(g_dsdl.error != dsdl_error_none);
    teardown_dsdl();
}

static void test_invalid_array_capacity_float(void)
{
    setup_dsdl();
    TEST_ASSERT_TRUE(add_test_roots());
    const dsdl_type_composite_t* type = dsdl_read(&g_dsdl, wkv_key("invalid.ArrayCapacityFloat.0.1"));
    TEST_ASSERT_NULL(type);
    TEST_ASSERT(g_dsdl.error != dsdl_error_none);
    teardown_dsdl();
}

static void test_invalid_array_capacity_negative_expr(void)
{
    setup_dsdl();
    TEST_ASSERT_TRUE(add_test_roots());
    const dsdl_type_composite_t* type = dsdl_read(&g_dsdl, wkv_key("invalid.ArrayCapacityNegativeExpr.0.1"));
    TEST_ASSERT_NULL(type);
    TEST_ASSERT(g_dsdl.error != dsdl_error_none);
    teardown_dsdl();
}

static void test_invalid_void_field_named(void)
{
    setup_dsdl();
    TEST_ASSERT_TRUE(add_test_roots());
    const dsdl_type_composite_t* type = dsdl_read(&g_dsdl, wkv_key("invalid.VoidFieldNamed.0.1"));
    TEST_ASSERT_NULL(type);
    TEST_ASSERT(g_dsdl.error != dsdl_error_none);
    teardown_dsdl();
}

static void test_invalid_primitive_int3(void)
{
    setup_dsdl();
    TEST_ASSERT_TRUE(add_test_roots());
    const dsdl_type_composite_t* type = dsdl_read(&g_dsdl, wkv_key("invalid.InvalidPrimitiveInt3.0.1"));
    TEST_ASSERT_NULL(type);
    TEST_ASSERT(g_dsdl.error != dsdl_error_none);
    teardown_dsdl();
}

static void test_invalid_primitive_uint33(void)
{
    setup_dsdl();
    TEST_ASSERT_TRUE(add_test_roots());
    const dsdl_type_composite_t* type = dsdl_read(&g_dsdl, wkv_key("invalid.InvalidPrimitiveUint33.0.1"));
    TEST_ASSERT_NULL(type);
    TEST_ASSERT(g_dsdl.error != dsdl_error_none);
    teardown_dsdl();
}

static void test_invalid_unknown_type_reference(void)
{
    setup_dsdl();
    TEST_ASSERT_TRUE(add_test_roots());
    const dsdl_type_composite_t* type = dsdl_read(&g_dsdl, wkv_key("invalid.UnknownTypeReference.0.1"));
    TEST_ASSERT_NULL(type);
    TEST_ASSERT(g_dsdl.error != dsdl_error_none);
    teardown_dsdl();
}

static void test_invalid_float16(void)
{
    setup_dsdl();
    TEST_ASSERT_TRUE(add_test_roots());
    const dsdl_type_composite_t* type = dsdl_read(&g_dsdl, wkv_key("invalid.InvalidFloat16.0.1"));
    TEST_ASSERT_NULL(type);
    TEST_ASSERT(g_dsdl.error != dsdl_error_none);
    teardown_dsdl();
}

static void test_invalid_assert_without_expr(void)
{
    setup_dsdl();
    TEST_ASSERT_TRUE(add_test_roots());
    const dsdl_type_composite_t* type = dsdl_read(&g_dsdl, wkv_key("invalid.AssertWithoutExpr.0.1"));
    TEST_ASSERT_NULL(type);
    TEST_ASSERT(g_dsdl.error != dsdl_error_none);
    teardown_dsdl();
}

static void test_invalid_extent_without_expr(void)
{
    setup_dsdl();
    TEST_ASSERT_TRUE(add_test_roots());
    const dsdl_type_composite_t* type = dsdl_read(&g_dsdl, wkv_key("invalid.ExtentWithoutExpr.0.1"));
    TEST_ASSERT_NULL(type);
    TEST_ASSERT(g_dsdl.error != dsdl_error_none);
    teardown_dsdl();
}

static void test_invalid_print_without_expr(void)
{
    setup_dsdl();
    TEST_ASSERT_TRUE(add_test_roots());
    const dsdl_type_composite_t* type = dsdl_read(&g_dsdl, wkv_key("invalid.PrintWithoutExpr.0.1"));
    TEST_ASSERT_NULL(type);
    TEST_ASSERT(g_dsdl.error != dsdl_error_none);
    teardown_dsdl();
}

static void test_invalid_sealed_and_extent_both(void)
{
    setup_dsdl();
    TEST_ASSERT_TRUE(add_test_roots());
    const dsdl_type_composite_t* type = dsdl_read(&g_dsdl, wkv_key("invalid.SealedAndExtentBoth.0.1"));
    TEST_ASSERT_NULL(type);
    TEST_ASSERT(g_dsdl.error != dsdl_error_none);
    teardown_dsdl();
}

static void test_invalid_assert_after_sealed(void)
{
    setup_dsdl();
    TEST_ASSERT_TRUE(add_test_roots());
    const dsdl_type_composite_t* type = dsdl_read(&g_dsdl, wkv_key("invalid.AssertAfterSealed.0.1"));
    TEST_ASSERT_NULL(type);
    TEST_ASSERT(g_dsdl.error != dsdl_error_none);
    teardown_dsdl();
}

static void test_invalid_constant_invalid_cast(void)
{
    setup_dsdl();
    TEST_ASSERT_TRUE(add_test_roots());
    const dsdl_type_composite_t* type = dsdl_read(&g_dsdl, wkv_key("invalid.ConstantInvalidCast.0.1"));
    TEST_ASSERT_NULL(type);
    TEST_ASSERT(g_dsdl.error != dsdl_error_none);
    teardown_dsdl();
}

static void test_invalid_constant_overflow(void)
{
    setup_dsdl();
    TEST_ASSERT_TRUE(add_test_roots());
    const dsdl_type_composite_t* type = dsdl_read(&g_dsdl, wkv_key("invalid.ConstantOverflow.0.1"));
    TEST_ASSERT_NULL(type);
    TEST_ASSERT(g_dsdl.error != dsdl_error_none);
    teardown_dsdl();
}

static void test_invalid_constant_type_mismatch(void)
{
    setup_dsdl();
    TEST_ASSERT_TRUE(add_test_roots());
    const dsdl_type_composite_t* type = dsdl_read(&g_dsdl, wkv_key("invalid.ConstantTypeMismatch.0.1"));
    TEST_ASSERT_NULL(type);
    TEST_ASSERT(g_dsdl.error != dsdl_error_none);
    teardown_dsdl();
}

static void test_invalid_constant_undefined(void)
{
    setup_dsdl();
    TEST_ASSERT_TRUE(add_test_roots());
    const dsdl_type_composite_t* type = dsdl_read(&g_dsdl, wkv_key("invalid.ConstantUndefined.0.1"));
    TEST_ASSERT_NULL(type);
    TEST_ASSERT(g_dsdl.error != dsdl_error_none);
    teardown_dsdl();
}

static void test_invalid_constant_negative_uint(void)
{
    setup_dsdl();
    TEST_ASSERT_TRUE(add_test_roots());
    const dsdl_type_composite_t* type = dsdl_read(&g_dsdl, wkv_key("invalid.ConstantNegativeUint.0.1"));
    TEST_ASSERT_NULL(type);
    TEST_ASSERT(g_dsdl.error != dsdl_error_none);
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
    RUN_TEST(test_bigint_mul_large);
    RUN_TEST(test_bigint_add_large);
    RUN_TEST(test_bigint_sub_underflow);
    RUN_TEST(test_bigint_mod_operations);
    RUN_TEST(test_bigint_gcd_operations);
    RUN_TEST(test_bigint_pow10);
    RUN_TEST(test_bigint_to_double);
    RUN_TEST(test_bigint_mul_small_inplace);
    RUN_TEST(test_bigint_add_small_inplace);
    RUN_TEST(test_bigint_div_small_inplace);
    RUN_TEST(test_bigint_div_small_by_zero);
    RUN_TEST(test_bigint_mul_pow2_inplace);
    RUN_TEST(test_bigint_mul_pow10_inplace);

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

    /* Additional validation types */
    RUN_TEST(test_complex_size);
    RUN_TEST(test_minimal_size);
    RUN_TEST(test_byte_arrays);
    RUN_TEST(test_string_arrays);
    RUN_TEST(test_large_arrays);
    RUN_TEST(test_nested_arrays);
    RUN_TEST(test_composite_arrays);
    RUN_TEST(test_all_uints);
    RUN_TEST(test_all_ints);
    RUN_TEST(test_all_voids);
    RUN_TEST(test_padding);
    RUN_TEST(test_arrays);
    RUN_TEST(test_union);
    RUN_TEST(test_basic_struct);
    RUN_TEST(test_nested);
    RUN_TEST(test_constants);
    RUN_TEST(test_sets);
    RUN_TEST(test_mixed_quotes);
    RUN_TEST(test_whitespace);
    RUN_TEST(test_whitespace_crlf);
    RUN_TEST(test_identifiers);
    RUN_TEST(test_comments);
    RUN_TEST(test_sealed);
    RUN_TEST(test_cast_modes);
    RUN_TEST(test_bit_alignment);
    RUN_TEST(test_union_tags);
    RUN_TEST(test_array_prefixes);
    RUN_TEST(test_primitives);
    RUN_TEST(test_empty);
    RUN_TEST(test_simple);
    RUN_TEST(test_level_types);
    RUN_TEST(test_deprecated_service);
    RUN_TEST(test_max_extent);
    RUN_TEST(test_zero_extent);
    RUN_TEST(test_extent_only);
    RUN_TEST(test_dynamic_extent);
    RUN_TEST(test_expr_errors);

    /* Serialization/Deserialization tests */
    RUN_TEST(test_serialize_arrays);
    RUN_TEST(test_serialize_union);
    RUN_TEST(test_serialize_primitives);
    RUN_TEST(test_serialize_padding);
    RUN_TEST(test_serialize_bit_alignment);
    RUN_TEST(test_serialize_buffer_size_0);
    RUN_TEST(test_serialize_buffer_size_1);
    RUN_TEST(test_serialize_buffer_size_2);
    RUN_TEST(test_serialize_buffer_size_4);
    RUN_TEST(test_serialize_buffer_size_8);
    RUN_TEST(test_serialize_buffer_size_16);
    RUN_TEST(test_serialize_buffer_size_exact_minus_1);
    RUN_TEST(test_serialize_buffer_size_exact);
    RUN_TEST(test_serialize_buffer_size_exact_plus_1);
    RUN_TEST(test_serialize_buffer_size_large);

    /* Array/String capacity tests */
    RUN_TEST(test_array_capacity_exceeded);
    RUN_TEST(test_string_capacity_exceeded);
    RUN_TEST(test_nested_array_capacity);

    /* OOM simulation tests */
    RUN_TEST(test_oom_systematic_hugestruct);
    RUN_TEST(test_oom_systematic_largeunion);
    RUN_TEST(test_oom_systematic_servicebothunion);
    RUN_TEST(test_oom_systematic_deepnesting);
    RUN_TEST(test_oom_during_serialization);
    RUN_TEST(test_oom_namespace_path_building);
    RUN_TEST(test_oom_during_type_load);
    RUN_TEST(test_oom_during_namespace_add);

    /* Null input test */
    RUN_TEST(test_deserialize_null_value);

    /* Invalid DSDL tests */
    RUN_TEST(test_invalid_no_sealing_or_extent);
    RUN_TEST(test_invalid_sealed_and_extent);
    RUN_TEST(test_invalid_extent_too_small);
    RUN_TEST(test_invalid_extent_non_integer);
    RUN_TEST(test_invalid_extent_no_expr);
    RUN_TEST(test_invalid_extent_repeated);
    RUN_TEST(test_invalid_field_after_extent);
    RUN_TEST(test_invalid_sealed_with_expr);
    RUN_TEST(test_invalid_sealed_repeated);
    RUN_TEST(test_invalid_union_one_field);
    RUN_TEST(test_invalid_union_zero_fields);
    RUN_TEST(test_invalid_union_after_field);
    RUN_TEST(test_invalid_union_repeated);
    RUN_TEST(test_invalid_union_with_expr);
    RUN_TEST(test_invalid_division_by_zero);
    RUN_TEST(test_invalid_assert_false);
    RUN_TEST(test_invalid_assert_non_bool);
    RUN_TEST(test_invalid_assert_undefined);
    RUN_TEST(test_invalid_assert_no_expr);
    RUN_TEST(test_invalid_int_constant_overflow);
    RUN_TEST(test_invalid_int_constant_float);
    RUN_TEST(test_invalid_int_constant_bool);
    RUN_TEST(test_invalid_float_constant_char);
    RUN_TEST(test_invalid_float_constant_bool);
    RUN_TEST(test_invalid_bool_constant_int);
    RUN_TEST(test_invalid_char_constant_wrong_type);
    RUN_TEST(test_invalid_char_constant_non_ascii);
    RUN_TEST(test_invalid_set_constant);
    RUN_TEST(test_invalid_undefined_type);
    RUN_TEST(test_invalid_undefined_constant);
    RUN_TEST(test_invalid_undefined_attribute);
    RUN_TEST(test_invalid_version_0_0);
    RUN_TEST(test_invalid_void0);
    RUN_TEST(test_invalid_int1);
    RUN_TEST(test_invalid_uint65);
    RUN_TEST(test_invalid_int65);
    RUN_TEST(test_invalid_void65);
    RUN_TEST(test_invalid_float8);
    RUN_TEST(test_invalid_array_capacity_0);
    RUN_TEST(test_invalid_array_capacity_non_integer);
    RUN_TEST(test_invalid_array_capacity_bool);
    RUN_TEST(test_invalid_array_capacity_string);
    RUN_TEST(test_invalid_array_exclusive_capacity_1);
    RUN_TEST(test_invalid_nested_array);
    RUN_TEST(test_invalid_void_array);
    RUN_TEST(test_invalid_utf8_fixed_array);
    RUN_TEST(test_invalid_utf8_standalone);
    RUN_TEST(test_invalid_byte_standalone);
    RUN_TEST(test_invalid_truncated_signed);
    RUN_TEST(test_invalid_truncated_bool);
    RUN_TEST(test_invalid_saturated_utf8);
    RUN_TEST(test_invalid_saturated_byte);
    RUN_TEST(test_invalid_deprecated_with_expr);
    RUN_TEST(test_invalid_deprecated_repeated);
    RUN_TEST(test_invalid_deprecated_after_field);
    RUN_TEST(test_invalid_deprecated_in_response);
    RUN_TEST(test_invalid_unknown_directive);
    RUN_TEST(test_invalid_service_triple_marker);
    RUN_TEST(test_invalid_service_bit_length_ref);
    RUN_TEST(test_invalid_service_with_fixed_port_id);
    RUN_TEST(test_invalid_port_id_overflow);
    RUN_TEST(test_invalid_port_id_mismatch);
    RUN_TEST(test_invalid_invalid_operand_types);
    RUN_TEST(test_invalid_void_named);
    RUN_TEST(test_invalid_attribute_name_collision);
    RUN_TEST(test_invalid_syntax_missing_type);
    RUN_TEST(test_invalid_syntax_bad_array_syntax);
    RUN_TEST(test_invalid_offset_in_union_before);
    RUN_TEST(test_invalid_uses_deprecated);
    RUN_TEST(test_invalid_logical_and_type_mismatch);
    RUN_TEST(test_invalid_logical_or_type_mismatch);
    RUN_TEST(test_invalid_logical_not_type_mismatch);
    RUN_TEST(test_invalid_unary_plus_bool);
    RUN_TEST(test_invalid_unary_minus_string);
    RUN_TEST(test_invalid_bitwise_or_bool);
    RUN_TEST(test_invalid_bitwise_and_bool);
    RUN_TEST(test_invalid_set_division);
    RUN_TEST(test_invalid_empty_set_comparison);
    RUN_TEST(test_invalid_heterogeneous_set);
    RUN_TEST(test_invalid_set_attribute_nonexistent);
    RUN_TEST(test_invalid_service_response_no_sealing_or_extent);
    RUN_TEST(test_invalid_service_response_both_sealed_and_extent);
    RUN_TEST(test_invalid_service_response_union_one_field);
    RUN_TEST(test_service_response_assert);
    RUN_TEST(test_service_response_extent_expr);
    RUN_TEST(test_service_response_print);
    RUN_TEST(test_invalid_service_response_extent_non_int);
    RUN_TEST(test_invalid_service_response_assert_fail);
    RUN_TEST(test_invalid_circular_dependency_a);
    RUN_TEST(test_invalid_circular_dependency_b);
    RUN_TEST(test_invalid_missing_type_reference);
    RUN_TEST(test_invalid_self_referencing_type);
    RUN_TEST(test_invalid_namespace_reference);
    RUN_TEST(test_invalid_type_not_found_in_namespace);
    RUN_TEST(test_invalid_ambiguous_type_reference);
    RUN_TEST(test_invalid_forward_reference_without_definition);
    RUN_TEST(test_invalid_recursive_type_without_base_case);
    RUN_TEST(test_invalid_type_composition);
    RUN_TEST(test_invalid_version_mismatch_type);
    RUN_TEST(test_complex_expressions);
    RUN_TEST(test_invalid_type_comparison_mismatch);
    RUN_TEST(test_invalid_empty_string_comparison);
    RUN_TEST(test_invalid_set_comparison_deferred);
    RUN_TEST(test_invalid_attribute_on_non_type);
    RUN_TEST(test_invalid_division_by_near_zero);
    RUN_TEST(test_invalid_large_negative_numbers);
    RUN_TEST(test_invalid_complex_rational_ops);
    RUN_TEST(test_parser_errors_batch);
    RUN_TEST(test_invalid_missing_semicolon);
    RUN_TEST(test_invalid_char_in_identifier);
    RUN_TEST(test_invalid_unclosed_comment);
    RUN_TEST(test_invalid_directive_syntax);
    RUN_TEST(test_invalid_malformed_array_syntax);
    RUN_TEST(test_invalid_constant_expression);
    RUN_TEST(test_invalid_missing_extent_marker);
    RUN_TEST(test_invalid_service_separator);
    RUN_TEST(test_invalid_duplicate_field_names);
    RUN_TEST(test_invalid_version_format);
    RUN_TEST(test_invalid_expr_division_by_zero);
    RUN_TEST(test_invalid_expr_type_mismatch);
    RUN_TEST(test_invalid_expr_invalid_operator);
    RUN_TEST(test_invalid_expr_overflow);
    RUN_TEST(test_invalid_expr_underflow);
    RUN_TEST(test_invalid_expr_invalid_comparison);
    RUN_TEST(test_invalid_expr_invalid_logical);
    RUN_TEST(test_invalid_expr_invalid_bitwise);
    RUN_TEST(test_invalid_expr_invalid_modulo);
    RUN_TEST(test_invalid_expr_invalid_power);

    /* New coverage tests for invalid DSDL files */
    RUN_TEST(test_invalid_array_capacity_negative);
    RUN_TEST(test_invalid_array_capacity_too_large);
    RUN_TEST(test_invalid_array_capacity_variable);
    RUN_TEST(test_invalid_array_capacity_float);
    RUN_TEST(test_invalid_array_capacity_negative_expr);
    RUN_TEST(test_invalid_void_field_named);
    RUN_TEST(test_invalid_primitive_int3);
    RUN_TEST(test_invalid_primitive_uint33);
    RUN_TEST(test_invalid_unknown_type_reference);
    RUN_TEST(test_invalid_float16);
    RUN_TEST(test_invalid_assert_without_expr);
    RUN_TEST(test_invalid_extent_without_expr);
    RUN_TEST(test_invalid_print_without_expr);
    RUN_TEST(test_invalid_sealed_and_extent_both);
    RUN_TEST(test_invalid_assert_after_sealed);
    RUN_TEST(test_invalid_constant_invalid_cast);
    RUN_TEST(test_invalid_constant_overflow);
    RUN_TEST(test_invalid_constant_type_mismatch);
    RUN_TEST(test_invalid_constant_undefined);
    RUN_TEST(test_invalid_constant_negative_uint);

    return UNITY_END();
}
