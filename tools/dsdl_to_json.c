#include "dsdl_tool_common.h"

#include <inttypes.h>
#include <stdio.h>

static void json_write_string(FILE* const out, const char* const str, const size_t len)
{
    (void)fputc('"', out);
    for (size_t i = 0U; i < len; i++) {
        const unsigned char ch = (unsigned char)str[i];
        switch (ch) {
            case '\\':
                (void)fputs("\\\\", out);
                break;
            case '"':
                (void)fputs("\\\"", out);
                break;
            case '\n':
                (void)fputs("\\n", out);
                break;
            case '\r':
                (void)fputs("\\r", out);
                break;
            case '\t':
                (void)fputs("\\t", out);
                break;
            default:
                if (ch < 0x20U) {
                    (void)fprintf(out, "\\u%04X", (unsigned)ch);
                } else {
                    (void)fputc((int)ch, out);
                }
                break;
        }
    }
    (void)fputc('"', out);
}

static void json_write_wkv(FILE* const out, const wkv_str_t str)
{
    json_write_string(out, str.str != NULL ? str.str : "", str.str != NULL ? str.len : 0U);
}

static void json_write_bigint(FILE* const out, const dsdl_bigint_t* const value)
{
    if ((value == NULL) || (value->limb_count == 0U)) {
        (void)fputc('0', out);
        return;
    }
    if (value->negative) {
        (void)fputc('-', out);
    }
    uint_least8_t i = value->limb_count;
    i--;
    (void)fprintf(out, "%u", value->limbs[i]);
    while (i-- > 0U) {
        (void)fprintf(out, "%0*u", (int)DSDL_BIGINT_BASE_DIGITS, value->limbs[i]);
    }
}

static void json_write_value(FILE* const out, const dsdl_value_t* const value)
{
    switch (value->kind) {
        case dsdl_value_rational:
            (void)fputs("{\"kind\":\"rational\",\"num\":\"", out);
            json_write_bigint(out, &value->as.rational.num);
            (void)fputs("\",\"den\":\"", out);
            json_write_bigint(out, &value->as.rational.den);
            (void)fputs("\"}", out);
            break;
        case dsdl_value_bool:
            (void)fprintf(out, "{\"kind\":\"bool\",\"value\":%s}", value->as.boolean ? "true" : "false");
            break;
        case dsdl_value_string:
            (void)fputs("{\"kind\":\"string\",\"value\":", out);
            json_write_wkv(out, value->as.string);
            (void)fputc('}', out);
            break;
        case dsdl_value_set:
            (void)fputs("{\"kind\":\"set\",\"elements\":[", out);
            for (size_t i = 0U; i < value->as.set.count; i++) {
                if (i > 0U) {
                    (void)fputc(',', out);
                }
                json_write_value(out, &value->as.set.elements[i]);
            }
            (void)fputs("]}", out);
            break;
        case dsdl_value_type: {
            const dsdl_type_composite_t* const comp = (const dsdl_type_composite_t*)value->as.type_ref;
            (void)fputs("{\"kind\":\"type\",\"name\":", out);
            if (comp != NULL) {
                json_write_wkv(out, comp->name);
            } else {
                json_write_string(out, "", 0U);
            }
            (void)fputc('}', out);
            break;
        }
        case dsdl_value_deferred:
        default:
            (void)fputs("{\"kind\":\"deferred\"}", out);
            break;
    }
}

static void json_write_type(FILE* const out, const dsdl_type_t* const type_ptr)
{
    const dsdl_type_t kind = *type_ptr;
    if (dsdl_type_is_array(kind)) {
        const dsdl_type_array_t* const arr = (const dsdl_type_array_t*)type_ptr;
        (void)fprintf(out,
                      "{\"kind\":\"array\",\"variable\":%s,\"capacity\":%" PRIu64 ",\"element\":",
                      (arr->type == DSDL_ARRAY_VARIABLE) ? "true" : "false",
                      arr->capacity);
        json_write_type(out, arr->member_type);
        (void)fputc('}', out);
        return;
    }
    if (dsdl_type_is_composite(kind)) {
        const dsdl_type_composite_t* const comp = (const dsdl_type_composite_t*)type_ptr;
        (void)fputs("{\"kind\":\"composite\",\"name\":", out);
        json_write_wkv(out, comp->name);
        (void)fputc('}', out);
        return;
    }

    if (kind == DSDL_BOOL) {
        (void)fputs("{\"kind\":\"bool\"}", out);
        return;
    }
    if (kind == DSDL_BYTE) {
        (void)fputs("{\"kind\":\"byte\"}", out);
        return;
    }
    if (kind == DSDL_UTF8) {
        (void)fputs("{\"kind\":\"utf8\"}", out);
        return;
    }

    dsdl_type_t base = kind;
    if (dsdl_type_is_alias(base)) {
        base = (dsdl_type_t)(base & (dsdl_type_t)~DSDL_TYPE_ALIAS_MASK);
    }

    const bool      truncated = dsdl_type_is_truncated(base);
    const dsdl_type_t raw      = (dsdl_type_t)(base & (dsdl_type_t)~DSDL_TYPE_TRUNCATED_FLAG);
    const uint_least8_t bits   = dsdl_type_bit_width(raw);

    if (dsdl_type_is_void(raw)) {
        (void)fprintf(out, "{\"kind\":\"void\",\"bits\":%" PRIuLEAST8 "}", bits);
        return;
    }
    if (dsdl_type_is_int(raw)) {
        (void)fprintf(out,
                      "{\"kind\":\"int\",\"bits\":%" PRIuLEAST8 ",\"truncated\":%s}",
                      bits,
                      truncated ? "true" : "false");
        return;
    }
    if (dsdl_type_is_uint(raw)) {
        (void)fprintf(out,
                      "{\"kind\":\"uint\",\"bits\":%" PRIuLEAST8 ",\"truncated\":%s}",
                      bits,
                      truncated ? "true" : "false");
        return;
    }
    if (dsdl_type_is_float(raw)) {
        (void)fprintf(out,
                      "{\"kind\":\"float\",\"bits\":%" PRIuLEAST8 ",\"truncated\":%s}",
                      bits,
                      truncated ? "true" : "false");
        return;
    }

    (void)fputs("{\"kind\":\"unknown\"}", out);
}

static const char* composite_kind_name(const dsdl_type_composite_t* const type)
{
    return (type->type == DSDL_COMPOSITE_UNION) ? "union" : "struct";
}

static void json_write_fields(FILE* const out, const dsdl_type_composite_t* const type)
{
    (void)fputc('[', out);
    for (size_t i = 0U; i < type->field_count; i++) {
        if (i > 0U) {
            (void)fputc(',', out);
        }
        (void)fputs("{\"name\":", out);
        json_write_wkv(out, type->field_names[i]);
        (void)fputs(",\"type\":", out);
        json_write_type(out, type->field_types[i]);
        if ((type->field_names[i].len == 0U) && dsdl_type_is_void(*type->field_types[i])) {
            (void)fputs(",\"padding\":true", out);
        }
        (void)fputc('}', out);
    }
    (void)fputc(']', out);
}

static void json_write_constants(FILE* const out, const dsdl_type_composite_t* const type)
{
    (void)fputc('[', out);
    for (size_t i = 0U; i < type->constant_count; i++) {
        if (i > 0U) {
            (void)fputc(',', out);
        }
        (void)fputs("{\"name\":", out);
        json_write_wkv(out, type->constant_names[i]);
        (void)fputs(",\"type\":", out);
        json_write_type(out, type->constant_types[i]);
        (void)fputs(",\"value\":", out);
        json_write_value(out, &type->constant_values[i]);
        (void)fputc('}', out);
    }
    (void)fputc(']', out);
}

static void json_write_section(FILE* const out, const dsdl_type_composite_t* const type, const bool include_constants)
{
    const uint64_t extent_bytes = type->extent;
    const uint64_t extent_bits =
      (extent_bytes > 0U) ? (extent_bytes * 8U) : (dsdl_serialized_footprint(type) * 8U);

    (void)fputc('{', out);
    (void)fprintf(out, "\"kind\":\"%s\",", composite_kind_name(type));
    (void)fprintf(out, "\"sealed\":%s,", type->sealed ? "true" : "false");
    (void)fprintf(out, "\"extent_bytes\":%" PRIu64 ",\"extent_bits\":%" PRIu64 ",", extent_bytes, extent_bits);
    (void)fputs("\"fields\":", out);
    json_write_fields(out, type);
    if (include_constants) {
        (void)fputs(",\"constants\":", out);
        json_write_constants(out, type);
    }
    (void)fputc('}', out);
}

static void json_write_composite(FILE* const out, const dsdl_type_composite_t* const type)
{
    const uint64_t extent_bytes = type->extent;
    const uint64_t extent_bits =
      (extent_bytes > 0U) ? (extent_bytes * 8U) : (dsdl_serialized_footprint(type) * 8U);

    (void)fputc('{', out);
    (void)fputs("\"name\":", out);
    json_write_wkv(out, type->name);
    (void)fprintf(out,
                  ",\"version\":[%" PRIuLEAST8 ",%" PRIuLEAST8 "],\"deprecated\":%s",
                  type->version[0],
                  type->version[1],
                  type->deprecated ? "true" : "false");

    if (type->response != NULL) {
        (void)fputs(",\"kind\":\"service\"", out);
        if (type->fixed_port_id != DSDL_FIXED_PORT_ID_NONE) {
            (void)fprintf(out, ",\"fixed_port_id\":%" PRIu16, type->fixed_port_id);
        } else {
            (void)fputs(",\"fixed_port_id\":null", out);
        }
        (void)fputs(",\"request\":", out);
        json_write_section(out, type, true);
        (void)fputs(",\"response\":", out);
        json_write_section(out, type->response, false);
    } else {
        (void)fprintf(out, ",\"kind\":\"%s\"", composite_kind_name(type));
        if (type->fixed_port_id != DSDL_FIXED_PORT_ID_NONE) {
            (void)fprintf(out, ",\"fixed_port_id\":%" PRIu16, type->fixed_port_id);
        } else {
            (void)fputs(",\"fixed_port_id\":null", out);
        }
        (void)fprintf(out, ",\"sealed\":%s", type->sealed ? "true" : "false");
        (void)fprintf(out,
                      ",\"extent_bytes\":%" PRIu64 ",\"extent_bits\":%" PRIu64,
                      extent_bytes,
                      extent_bits);
        (void)fputs(",\"fields\":", out);
        json_write_fields(out, type);
        (void)fputs(",\"constants\":", out);
        json_write_constants(out, type);
    }

    (void)fputc('}', out);
}

int main(int argc, char** argv)
{
    dsdl_tool_args_t args;
    if (!dsdl_tool_parse_args(&args, argc, argv)) {
        dsdl_tool_print_usage(stderr, argv[0], "Emit JSON from parsed types.");
        dsdl_tool_free_args(&args);
        return 2;
    }
    if (args.help_requested) {
        dsdl_tool_print_usage(stdout, argv[0], "Emit JSON from parsed types.");
        dsdl_tool_free_args(&args);
        return 0;
    }
    if ((args.root_count == 0U) || (args.type_count == 0U)) {
        dsdl_tool_print_usage(stderr, argv[0], "Emit JSON from parsed types.");
        dsdl_tool_free_args(&args);
        return 2;
    }

    dsdl_t dsdl;
    dsdl_tool_init(&dsdl);
    if (!dsdl_tool_add_roots(&dsdl, &args)) {
        (void)fprintf(stderr, "Failed to add namespace roots\n");
        dsdl_destroy(&dsdl);
        dsdl_tool_free_args(&args);
        return 1;
    }

    (void)fputc('[', stdout);
    int exit_code = 0;
    for (size_t i = 0U; i < args.type_count; i++) {
        const char* const type_name = args.types[i];
        const dsdl_type_composite_t* const type = dsdl_read(&dsdl, wkv_key(type_name));
        if (type == NULL) {
            (void)fprintf(stderr, "Failed to parse type: %s\n", type_name);
            exit_code = 1;
            break;
        }
        if (i > 0U) {
            (void)fputc(',', stdout);
        }
        json_write_composite(stdout, type);
    }
    (void)fputc(']', stdout);
    (void)fputc('\n', stdout);

    dsdl_destroy(&dsdl);
    dsdl_tool_free_args(&args);
    return exit_code;
}
