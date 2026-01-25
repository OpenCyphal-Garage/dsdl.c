#include "dsdl_tool_common.h"

#include <inttypes.h>
#include <stdio.h>
#include <string.h>

static void emit_wkv(FILE* const out, const wkv_str_t str)
{
    if ((str.str != NULL) && (str.len > 0U)) {
        (void)fwrite(str.str, 1U, str.len, out);
    }
}

static void emit_type_expr(FILE* const out, const dsdl_type_t* const type_ptr)
{
    const dsdl_type_t kind = *type_ptr;
    if (dsdl_type_is_array(kind)) {
        const dsdl_type_array_t* const arr = (const dsdl_type_array_t*)type_ptr;
        emit_type_expr(out, arr->member_type);
        if (arr->type == DSDL_ARRAY_FIXED) {
            (void)fprintf(out, "[%" PRIu64 "]", arr->capacity);
        } else {
            (void)fprintf(out, "[<=%" PRIu64 "]", arr->capacity);
        }
        return;
    }
    if (dsdl_type_is_composite(kind)) {
        const dsdl_type_composite_t* const comp = (const dsdl_type_composite_t*)type_ptr;
        emit_wkv(out, comp->name);
        return;
    }

    if (kind == DSDL_BOOL) {
        (void)fputs("bool", out);
        return;
    }
    if (kind == DSDL_BYTE) {
        (void)fputs("byte", out);
        return;
    }
    if (kind == DSDL_UTF8) {
        (void)fputs("utf8", out);
        return;
    }

    dsdl_type_t base = kind;
    if (dsdl_type_is_alias(base)) {
        base = (dsdl_type_t)(base & (dsdl_type_t)~DSDL_TYPE_ALIAS_MASK);
    }

    const bool      truncated = dsdl_type_is_truncated(base);
    const dsdl_type_t raw      = (dsdl_type_t)(base & (dsdl_type_t)~DSDL_TYPE_TRUNCATED_FLAG);
    if (truncated && (dsdl_type_is_uint(raw) || dsdl_type_is_float(raw))) {
        (void)fputs("truncated ", out);
    }

    const uint_least8_t bits = dsdl_type_bit_width(raw);
    if (dsdl_type_is_void(raw)) {
        (void)fprintf(out, "void%" PRIuLEAST8, bits);
    } else if (dsdl_type_is_int(raw)) {
        (void)fprintf(out, "int%" PRIuLEAST8, bits);
    } else if (dsdl_type_is_uint(raw)) {
        (void)fprintf(out, "uint%" PRIuLEAST8, bits);
    } else if (dsdl_type_is_float(raw)) {
        (void)fprintf(out, "float%" PRIuLEAST8, bits);
    } else {
        (void)fputs("unknown", out);
    }
}

static void emit_value_expr(FILE* const out, const dsdl_value_t* const value)
{
    switch (value->kind) {
        case dsdl_value_bool:
            (void)fputs(value->as.boolean ? "true" : "false", out);
            break;
        case dsdl_value_rational:
            if (value->as.rational.den == 1U) {
                (void)fprintf(out, "%" PRIdMAX, value->as.rational.num);
            } else {
                (void)fprintf(out, "%" PRIdMAX "/%" PRIuMAX, value->as.rational.num, value->as.rational.den);
            }
            break;
        case dsdl_value_string:
            (void)fputc('"', out);
            for (size_t i = 0U; i < value->as.string.len; i++) {
                const unsigned char ch = (unsigned char)value->as.string.str[i];
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
                        if ((ch < 0x20U) || (ch > 0x7EU)) {
                            (void)fprintf(out, "\\x%02X", (unsigned)ch);
                        } else {
                            (void)fputc((int)ch, out);
                        }
                        break;
                }
            }
            (void)fputc('"', out);
            break;
        case dsdl_value_set:
            (void)fputc('{', out);
            for (size_t i = 0U; i < value->as.set.count; i++) {
                if (i > 0U) {
                    (void)fputs(", ", out);
                }
                emit_value_expr(out, &value->as.set.elements[i]);
            }
            (void)fputc('}', out);
            break;
        case dsdl_value_type: {
            const dsdl_type_composite_t* const comp = (const dsdl_type_composite_t*)value->as.type_ref;
            if (comp != NULL) {
                emit_wkv(out, comp->name);
            }
            break;
        }
        case dsdl_value_deferred:
        default:
            (void)fputs("null", out);
            break;
    }
}

static void emit_constants(FILE* const out, const dsdl_type_composite_t* const type)
{
    for (size_t i = 0U; i < type->constant_count; i++) {
        emit_type_expr(out, type->constant_types[i]);
        (void)fputc(' ', out);
        emit_wkv(out, type->constant_names[i]);
        (void)fputs(" = ", out);
        emit_value_expr(out, &type->constant_values[i]);
        (void)fputc('\n', out);
    }
}

static void emit_fields(FILE* const out, const dsdl_type_composite_t* const type)
{
    for (size_t i = 0U; i < type->field_count; i++) {
        emit_type_expr(out, type->field_types[i]);
        if (type->field_names[i].len > 0U) {
            (void)fputc(' ', out);
            emit_wkv(out, type->field_names[i]);
        }
        (void)fputc('\n', out);
    }
}

static void emit_section_directives(FILE* const out, const dsdl_type_composite_t* const type)
{
    if (type->type == DSDL_COMPOSITE_UNION) {
        (void)fputs("@union\n", out);
    }
    if (type->sealed) {
        (void)fputs("@sealed\n", out);
    }
    if (type->extent > 0U) {
        (void)fprintf(out, "@extent %" PRIu64 "\n", type->extent * 8U);
    }
}

static void emit_type(FILE* const out, const dsdl_type_composite_t* const type)
{
    (void)fprintf(out,
                  "# dsdl_to_dsdl normalized output\n"
                  "# name: ");
    emit_wkv(out, type->name);
    (void)fputc('\n', out);

    if (type->deprecated) {
        (void)fputs("@deprecated\n", out);
    }
    if (type->fixed_port_id != DSDL_FIXED_PORT_ID_NONE) {
        (void)fprintf(out, "@fixed-port-id %" PRIu16 "\n", type->fixed_port_id);
    }

    emit_section_directives(out, type);
    emit_fields(out, type);
    emit_constants(out, type);

    if (type->response != NULL) {
        (void)fputs("---\n", out);
        emit_section_directives(out, type->response);
        emit_fields(out, type->response);
    }
}

int main(int argc, char** argv)
{
    dsdl_tool_args_t args;
    if (!dsdl_tool_parse_args(&args, argc, argv)) {
        dsdl_tool_print_usage(stderr, argv[0], "Emit normalized DSDL from parsed types.");
        dsdl_tool_free_args(&args);
        return 2;
    }
    if (args.help_requested) {
        dsdl_tool_print_usage(stdout, argv[0], "Emit normalized DSDL from parsed types.");
        dsdl_tool_free_args(&args);
        return 0;
    }
    if ((args.root_count == 0U) || (args.type_count == 0U)) {
        dsdl_tool_print_usage(stderr, argv[0], "Emit normalized DSDL from parsed types.");
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

    int exit_code = 0;
    for (size_t i = 0U; i < args.type_count; i++) {
        const char* const type_name = args.types[i];
        const dsdl_type_composite_t* const type = dsdl_read(&dsdl, wkv_key(type_name));
        if (type == NULL) {
            (void)fprintf(stderr, "Failed to parse type: %s\n", type_name);
            exit_code = 1;
            break;
        }
        emit_type(stdout, type);
        if (i + 1U < args.type_count) {
            (void)fputc('\n', stdout);
        }
    }

    dsdl_destroy(&dsdl);
    dsdl_tool_free_args(&args);
    return exit_code;
}
