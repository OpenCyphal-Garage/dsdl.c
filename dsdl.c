/// Cyphal DSDL Parser in C
///
/// This is a compact C99+ implementation of a Cyphal DSDL parser that allows
/// loading DSDL definitions at runtime without compile-time code generation.
///
/// See AGENTS.md and docs/IMPLEMENTATION_PLAN.md for design details.
///
/// Copyright (c) OpenCyphal Development Team

#include "dsdl.h"
#include "wkv.h"

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>

// ============================================================================
// Configuration and platform detection
// ============================================================================

/// Detect C23 for _BitInt support
#if defined(__STDC_VERSION__) && (__STDC_VERSION__ >= 202311L)
#    define DSDL_HAS_BITINT 1
#else
#    define DSDL_HAS_BITINT 0
#endif

/// Mark internal functions that may be unused in the library build but are
/// used by tests that include dsdl.c directly.
#if defined(__GNUC__) || defined(__clang__)
#    define DSDL_INTERNAL __attribute__((unused))
#else
#    define DSDL_INTERNAL
#endif

// ============================================================================
// Internal type definitions
// ============================================================================

/// Rational number for exact arithmetic during expression evaluation.
/// Per DSDL spec section 3.1: rationals must be stored normalized with
/// positive denominator and GCD(num, den) == 1.
typedef struct
{
#if DSDL_HAS_BITINT
    _BitInt(2048)          num;   ///< Numerator (signed)
    unsigned _BitInt(2048) den;   ///< Denominator (always positive, 1 for integers)
#else
    intmax_t  num;   ///< Numerator (signed)
    uintmax_t den;   ///< Denominator (always positive, 0 means NaN, 1 for integers)
#endif
} dsdl_rational_t;

/// Expression value types for compile-time evaluation.
typedef enum
{
    DSDL_VALUE_RATIONAL,   ///< Numeric value (integer or rational)
    DSDL_VALUE_STRING,     ///< Unicode string
    DSDL_VALUE_BOOL,       ///< Boolean
    DSDL_VALUE_SET,        ///< Set of values
    DSDL_VALUE_TYPE,       ///< Serializable metatype reference
} dsdl_value_kind_t;

/// Forward declaration for recursive type.
typedef struct dsdl_value_t dsdl_value_t;

/// Runtime value during expression evaluation.
struct dsdl_value_t
{
    dsdl_value_kind_t kind;
    union dsdl_value_data_t
    {
        dsdl_rational_t rational;   ///< DSDL_VALUE_RATIONAL
        wkv_str_t       string;     ///< DSDL_VALUE_STRING (borrowed pointer)
        bool            boolean;    ///< DSDL_VALUE_BOOL
        struct                      ///< DSDL_VALUE_SET
        {
            size_t        count;
            dsdl_value_t* elements;
        } set;
        void* type_ref;   ///< DSDL_VALUE_TYPE (pointer to dsdl_composite_t)
    } as;
};

// ============================================================================
// Rational arithmetic
// ============================================================================

/// Compute GCD using Euclidean algorithm.
DSDL_INTERNAL static uintmax_t dsdl_gcd_(uintmax_t a, uintmax_t b)
{
    while (b != 0)
    {
        const uintmax_t t = b;
        b                 = a % b;
        a                 = t;
    }
    return a;
}

/// Absolute value for signed integers.
DSDL_INTERNAL static uintmax_t dsdl_abs_(intmax_t x)
{
    // Handle INTMAX_MIN carefully to avoid UB
    if (x >= 0)
    {
        return (uintmax_t) x;
    }
    // For negative values, negate. Special case for INTMAX_MIN.
    if (x == INTMAX_MIN)
    {
        return (uintmax_t) INTMAX_MAX + 1U;
    }
    return (uintmax_t) (-x);
}

/// Create a rational from an integer.
DSDL_INTERNAL static dsdl_rational_t dsdl_rational_from_int_(intmax_t value)
{
    dsdl_rational_t r;
    r.num = value;
    r.den = 1;
    return r;
}

/// Normalize a rational: ensure den > 0 and GCD(|num|, den) == 1.
DSDL_INTERNAL static dsdl_rational_t dsdl_rational_normalize_(dsdl_rational_t r)
{
    if (r.den == 0)
    {
        // NaN - return as-is
        return r;
    }
    // Ensure denominator is positive (for non-_BitInt version)
#if !DSDL_HAS_BITINT
    // intmax_t/uintmax_t version: denominator is already unsigned
#endif
    // Reduce by GCD
    const uintmax_t g = dsdl_gcd_(dsdl_abs_(r.num), r.den);
    if (g > 1)
    {
        r.num = r.num / (intmax_t) g;
        r.den = r.den / g;
    }
    return r;
}

/// Add two rationals: a/b + c/d = (ad + bc) / bd
DSDL_INTERNAL static dsdl_rational_t dsdl_rational_add_(dsdl_rational_t a, dsdl_rational_t b)
{
    dsdl_rational_t r;
    // TODO: overflow detection
    r.num = a.num * (intmax_t) b.den + b.num * (intmax_t) a.den;
    r.den = a.den * b.den;
    return dsdl_rational_normalize_(r);
}

/// Subtract two rationals: a/b - c/d = (ad - bc) / bd
DSDL_INTERNAL static dsdl_rational_t dsdl_rational_sub_(dsdl_rational_t a, dsdl_rational_t b)
{
    dsdl_rational_t r;
    r.num = a.num * (intmax_t) b.den - b.num * (intmax_t) a.den;
    r.den = a.den * b.den;
    return dsdl_rational_normalize_(r);
}

/// Multiply two rationals: a/b * c/d = ac / bd
DSDL_INTERNAL static dsdl_rational_t dsdl_rational_mul_(dsdl_rational_t a, dsdl_rational_t b)
{
    dsdl_rational_t r;
    r.num = a.num * b.num;
    r.den = a.den * b.den;
    return dsdl_rational_normalize_(r);
}

/// Divide two rationals: (a/b) / (c/d) = ad / bc
DSDL_INTERNAL static dsdl_rational_t dsdl_rational_div_(dsdl_rational_t a, dsdl_rational_t b)
{
    dsdl_rational_t r;
    if (b.num == 0)
    {
        r.num = 0;
        r.den = 0;   // NaN
        return r;
    }
    // Handle sign: if b.num is negative, flip signs
    if (b.num < 0)
    {
        r.num = a.num * (-(intmax_t) b.den);
        r.den = a.den * dsdl_abs_(b.num);
    }
    else
    {
        r.num = a.num * (intmax_t) b.den;
        r.den = a.den * (uintmax_t) b.num;
    }
    return dsdl_rational_normalize_(r);
}

/// Negate a rational.
DSDL_INTERNAL static dsdl_rational_t dsdl_rational_neg_(dsdl_rational_t a)
{
    a.num = -a.num;
    return a;
}

/// Compare two rationals: returns <0, 0, >0.
DSDL_INTERNAL static int dsdl_rational_cmp_(dsdl_rational_t a, dsdl_rational_t b)
{
    // a/b vs c/d  =>  compare a*d vs c*b
    const intmax_t lhs = a.num * (intmax_t) b.den;
    const intmax_t rhs = b.num * (intmax_t) a.den;
    if (lhs < rhs)
    {
        return -1;
    }
    if (lhs > rhs)
    {
        return 1;
    }
    return 0;
}

/// Check if rational is an integer (denominator == 1).
DSDL_INTERNAL static bool dsdl_rational_is_int_(dsdl_rational_t r)
{
    return r.den == 1;
}

/// Convert rational to intmax_t (only valid if is_int).
DSDL_INTERNAL static intmax_t dsdl_rational_to_int_(dsdl_rational_t r)
{
    return r.num;   // Assumes den == 1
}

// ============================================================================
// Parser state
// ============================================================================

/// Internal parser state.
typedef struct
{
    const char* input;   ///< Input buffer (not NUL-terminated necessarily)
    size_t      len;     ///< Input length in bytes
    size_t      pos;     ///< Current parse position
    size_t      line;    ///< Current line number (1-based)
    size_t      col;     ///< Current column number (1-based)
    dsdl_t*     dsdl;    ///< Parent state for memory allocation and type cache
} dsdl_parser_t;

// ============================================================================
// Memory management helpers
// ============================================================================

DSDL_INTERNAL static void* dsdl_alloc_(dsdl_t* const self, const size_t size)
{
    if (size == 0)
    {
        return NULL;
    }
    return self->realloc(self, NULL, size);
}

static void dsdl_free_(dsdl_t* const self, void* const ptr)
{
    if (ptr != NULL)
    {
        (void) self->realloc(self, ptr, 0);
    }
}

// ============================================================================
// Parser foundation
// ============================================================================

/// Initialize parser state.
DSDL_INTERNAL static void dsdl_parser_init_(dsdl_parser_t* const parser,
                                            dsdl_t* const        dsdl,
                                            const char* const    input,
                                            const size_t         len)
{
    parser->input = input;
    parser->len   = len;
    parser->pos   = 0;
    parser->line  = 1;
    parser->col   = 1;
    parser->dsdl  = dsdl;
}

/// Check if parser has reached end of input.
DSDL_INTERNAL static bool dsdl_parser_eof_(const dsdl_parser_t* const parser)
{
    return parser->pos >= parser->len;
}

/// Peek at character at current position + offset. Returns 0 if out of bounds.
DSDL_INTERNAL static char dsdl_parser_peek_(const dsdl_parser_t* const parser, const size_t offset)
{
    const size_t idx = parser->pos + offset;
    if (idx >= parser->len)
    {
        return '\0';
    }
    return parser->input[idx];
}

/// Advance parser by count characters, updating line/col tracking.
DSDL_INTERNAL static void dsdl_parser_advance_(dsdl_parser_t* const parser, const size_t count)
{
    for (size_t i = 0; i < count && parser->pos < parser->len; ++i)
    {
        const char c = parser->input[parser->pos];
        if (c == '\n')
        {
            parser->line++;
            parser->col = 1;
        }
        else
        {
            parser->col++;
        }
        parser->pos++;
    }
}

/// Check if the string at current position matches the given string.
/// Does NOT advance the parser.
DSDL_INTERNAL static bool dsdl_parser_match_(const dsdl_parser_t* const parser,
                                             const char* const          str,
                                             const size_t               str_len)
{
    if ((parser->pos + str_len) > parser->len)
    {
        return false;
    }
    return memcmp(&parser->input[parser->pos], str, str_len) == 0;
}

/// Check if the string matches and advance if so.
DSDL_INTERNAL static bool dsdl_parser_accept_(dsdl_parser_t* const parser,
                                              const char* const    str,
                                              const size_t         str_len)
{
    if (dsdl_parser_match_(parser, str, str_len))
    {
        dsdl_parser_advance_(parser, str_len);
        return true;
    }
    return false;
}

/// Skip whitespace (space and tab only, not newlines).
DSDL_INTERNAL static void dsdl_parser_skip_ws_(dsdl_parser_t* const parser)
{
    while (!dsdl_parser_eof_(parser))
    {
        const char c = dsdl_parser_peek_(parser, 0);
        if ((c == ' ') || (c == '\t'))
        {
            dsdl_parser_advance_(parser, 1);
        }
        else
        {
            break;
        }
    }
}

/// Skip a comment (# to end of line). Returns true if a comment was skipped.
DSDL_INTERNAL static bool dsdl_parser_skip_comment_(dsdl_parser_t* const parser)
{
    if (dsdl_parser_peek_(parser, 0) != '#')
    {
        return false;
    }
    // Consume everything until end of line (but not the newline itself)
    while (!dsdl_parser_eof_(parser))
    {
        const char c = dsdl_parser_peek_(parser, 0);
        if ((c == '\r') || (c == '\n'))
        {
            break;
        }
        dsdl_parser_advance_(parser, 1);
    }
    return true;
}

/// Skip optional whitespace and comment at end of line.
DSDL_INTERNAL static void dsdl_parser_skip_line_tail_(dsdl_parser_t* const parser)
{
    dsdl_parser_skip_ws_(parser);
    (void) dsdl_parser_skip_comment_(parser);
}

/// Skip end of line sequence (\n or \r\n). Returns true if skipped.
DSDL_INTERNAL static bool dsdl_parser_skip_eol_(dsdl_parser_t* const parser)
{
    if (dsdl_parser_peek_(parser, 0) == '\r' && dsdl_parser_peek_(parser, 1) == '\n')
    {
        dsdl_parser_advance_(parser, 2);
        return true;
    }
    if (dsdl_parser_peek_(parser, 0) == '\n')
    {
        dsdl_parser_advance_(parser, 1);
        return true;
    }
    return false;
}

/// Check if character is a digit (0-9).
DSDL_INTERNAL static bool dsdl_is_digit_(const char c)
{
    return (c >= '0') && (c <= '9');
}

/// Check if character is a hex digit (0-9, a-f, A-F).
DSDL_INTERNAL static bool dsdl_is_hex_digit_(const char c)
{
    return dsdl_is_digit_(c) || ((c >= 'a') && (c <= 'f')) || ((c >= 'A') && (c <= 'F'));
}

/// Check if character is an octal digit (0-7).
DSDL_INTERNAL static bool dsdl_is_octal_digit_(const char c)
{
    return (c >= '0') && (c <= '7');
}

/// Check if character is a binary digit (0-1).
DSDL_INTERNAL static bool dsdl_is_binary_digit_(const char c)
{
    return (c == '0') || (c == '1');
}

/// Check if character is an identifier start character.
DSDL_INTERNAL static bool dsdl_is_ident_start_(const char c)
{
    return ((c >= 'a') && (c <= 'z')) || ((c >= 'A') && (c <= 'Z')) || (c == '_');
}

/// Check if character is an identifier continuation character.
DSDL_INTERNAL static bool dsdl_is_ident_cont_(const char c)
{
    return dsdl_is_ident_start_(c) || dsdl_is_digit_(c);
}

/// Convert hex digit to value (0-15).
DSDL_INTERNAL static int dsdl_hex_value_(const char c)
{
    if (dsdl_is_digit_(c))
    {
        return c - '0';
    }
    if ((c >= 'a') && (c <= 'f'))
    {
        return 10 + (c - 'a');
    }
    if ((c >= 'A') && (c <= 'F'))
    {
        return 10 + (c - 'A');
    }
    return -1;
}

// ============================================================================
// Literal parsing
// ============================================================================

/// Parse a binary integer literal: 0b[01_]+
/// Returns rational with den=1 on success, den=0 on failure.
DSDL_INTERNAL static dsdl_rational_t dsdl_parse_int_binary_(dsdl_parser_t* const parser)
{
    dsdl_rational_t result = {0, 0};   // NaN = failure

    // Check for 0b or 0B prefix
    if (!((dsdl_parser_peek_(parser, 0) == '0') &&
          ((dsdl_parser_peek_(parser, 1) == 'b') || (dsdl_parser_peek_(parser, 1) == 'B'))))
    {
        return result;
    }
    dsdl_parser_advance_(parser, 2);

    // Must have at least one digit
    bool     has_digit = false;
    intmax_t value     = 0;

    while (!dsdl_parser_eof_(parser))
    {
        const char c = dsdl_parser_peek_(parser, 0);
        if (c == '_')
        {
            // Skip underscores (digit separator)
            dsdl_parser_advance_(parser, 1);
            continue;
        }
        if (dsdl_is_binary_digit_(c))
        {
            has_digit = true;
            value     = (value << 1) | (c - '0');
            dsdl_parser_advance_(parser, 1);
        }
        else
        {
            break;
        }
    }

    if (has_digit)
    {
        result.num = value;
        result.den = 1;
    }
    return result;
}

/// Parse an octal integer literal: 0o[0-7_]+
DSDL_INTERNAL static dsdl_rational_t dsdl_parse_int_octal_(dsdl_parser_t* const parser)
{
    dsdl_rational_t result = {0, 0};   // NaN = failure

    // Check for 0o or 0O prefix
    if (!((dsdl_parser_peek_(parser, 0) == '0') &&
          ((dsdl_parser_peek_(parser, 1) == 'o') || (dsdl_parser_peek_(parser, 1) == 'O'))))
    {
        return result;
    }
    dsdl_parser_advance_(parser, 2);

    bool     has_digit = false;
    intmax_t value     = 0;

    while (!dsdl_parser_eof_(parser))
    {
        const char c = dsdl_parser_peek_(parser, 0);
        if (c == '_')
        {
            dsdl_parser_advance_(parser, 1);
            continue;
        }
        if (dsdl_is_octal_digit_(c))
        {
            has_digit = true;
            value     = (value << 3) | (c - '0');
            dsdl_parser_advance_(parser, 1);
        }
        else
        {
            break;
        }
    }

    if (has_digit)
    {
        result.num = value;
        result.den = 1;
    }
    return result;
}

/// Parse a hexadecimal integer literal: 0x[0-9a-fA-F_]+
DSDL_INTERNAL static dsdl_rational_t dsdl_parse_int_hex_(dsdl_parser_t* const parser)
{
    dsdl_rational_t result = {0, 0};   // NaN = failure

    // Check for 0x or 0X prefix
    if (!((dsdl_parser_peek_(parser, 0) == '0') &&
          ((dsdl_parser_peek_(parser, 1) == 'x') || (dsdl_parser_peek_(parser, 1) == 'X'))))
    {
        return result;
    }
    dsdl_parser_advance_(parser, 2);

    bool     has_digit = false;
    intmax_t value     = 0;

    while (!dsdl_parser_eof_(parser))
    {
        const char c = dsdl_parser_peek_(parser, 0);
        if (c == '_')
        {
            dsdl_parser_advance_(parser, 1);
            continue;
        }
        if (dsdl_is_hex_digit_(c))
        {
            has_digit = true;
            value     = (value << 4) | dsdl_hex_value_(c);
            dsdl_parser_advance_(parser, 1);
        }
        else
        {
            break;
        }
    }

    if (has_digit)
    {
        result.num = value;
        result.den = 1;
    }
    return result;
}

/// Parse a decimal integer literal: [0-9_]+
/// Returns rational with den=1 on success, den=0 on failure.
DSDL_INTERNAL static dsdl_rational_t dsdl_parse_int_decimal_(dsdl_parser_t* const parser)
{
    dsdl_rational_t result = {0, 0};   // NaN = failure

    // Must start with a digit
    if (!dsdl_is_digit_(dsdl_parser_peek_(parser, 0)))
    {
        return result;
    }

    bool     has_digit = false;
    intmax_t value     = 0;

    while (!dsdl_parser_eof_(parser))
    {
        const char c = dsdl_parser_peek_(parser, 0);
        if (c == '_')
        {
            dsdl_parser_advance_(parser, 1);
            continue;
        }
        if (dsdl_is_digit_(c))
        {
            has_digit = true;
            value     = (value * 10) + (c - '0');
            dsdl_parser_advance_(parser, 1);
        }
        else
        {
            break;
        }
    }

    if (has_digit)
    {
        result.num = value;
        result.den = 1;
    }
    return result;
}

/// Parse any integer literal (binary, octal, hex, or decimal).
/// The order matters: we try more specific prefixes first.
DSDL_INTERNAL static dsdl_rational_t dsdl_parse_integer_(dsdl_parser_t* const parser)
{
    const size_t start_pos = parser->pos;

    // Try binary (0b...)
    dsdl_rational_t result = dsdl_parse_int_binary_(parser);
    if (result.den != 0)
    {
        return result;
    }
    parser->pos = start_pos;   // Backtrack

    // Try octal (0o...)
    result = dsdl_parse_int_octal_(parser);
    if (result.den != 0)
    {
        return result;
    }
    parser->pos = start_pos;

    // Try hex (0x...)
    result = dsdl_parse_int_hex_(parser);
    if (result.den != 0)
    {
        return result;
    }
    parser->pos = start_pos;

    // Try decimal
    return dsdl_parse_int_decimal_(parser);
}

/// Parse digits for real number (returns count of digits parsed, 0 on failure).
DSDL_INTERNAL static size_t dsdl_parse_real_digits_(dsdl_parser_t* const parser, intmax_t* const out_value)
{
    size_t   count = 0;
    intmax_t value = 0;

    while (!dsdl_parser_eof_(parser))
    {
        const char c = dsdl_parser_peek_(parser, 0);
        if (c == '_')
        {
            dsdl_parser_advance_(parser, 1);
            continue;
        }
        if (dsdl_is_digit_(c))
        {
            count++;
            value = (value * 10) + (c - '0');
            dsdl_parser_advance_(parser, 1);
        }
        else
        {
            break;
        }
    }

    if (out_value != NULL)
    {
        *out_value = value;
    }
    return count;
}

/// Parse a real literal (point notation or exponent notation).
/// Returns rational on success, with den=0 on failure.
/// Note: For simplicity, we convert to double and then back to rational for fractional parts.
DSDL_INTERNAL static dsdl_rational_t dsdl_parse_real_(dsdl_parser_t* const parser)
{
    dsdl_rational_t result    = {0, 0};   // NaN = failure
    const size_t    start_pos = parser->pos;

    // Components of the real number
    intmax_t int_part   = 0;
    intmax_t frac_part  = 0;
    size_t   frac_count = 0;   // Number of fractional digits
    intmax_t exp_value  = 0;
    bool     exp_neg    = false;
    bool     has_point  = false;
    bool     has_exp    = false;

    // Parse integer part (optional if there's a point)
    const size_t int_digits = dsdl_parse_real_digits_(parser, &int_part);

    // Check for decimal point
    if (dsdl_parser_peek_(parser, 0) == '.')
    {
        // Make sure the next character is a digit or end-of-token (not another identifier)
        const char next = dsdl_parser_peek_(parser, 1);
        if (dsdl_is_digit_(next))
        {
            has_point = true;
            dsdl_parser_advance_(parser, 1);   // Consume '.'

            // Parse fractional part
            frac_count = dsdl_parse_real_digits_(parser, &frac_part);
        }
        else if (!dsdl_is_ident_start_(next))
        {
            // Just "123." is valid
            has_point = true;
            dsdl_parser_advance_(parser, 1);
        }
    }

    // Check for exponent
    const char exp_char = dsdl_parser_peek_(parser, 0);
    if ((exp_char == 'e') || (exp_char == 'E'))
    {
        has_exp = true;
        dsdl_parser_advance_(parser, 1);

        // Optional sign
        const char sign = dsdl_parser_peek_(parser, 0);
        if (sign == '+')
        {
            dsdl_parser_advance_(parser, 1);
        }
        else if (sign == '-')
        {
            exp_neg = true;
            dsdl_parser_advance_(parser, 1);
        }

        // Exponent digits (required)
        if (dsdl_parse_real_digits_(parser, &exp_value) == 0)
        {
            // Invalid: e without digits
            parser->pos = start_pos;
            return result;
        }
    }

    // A real literal must have either a decimal point or an exponent
    if (!has_point && !has_exp)
    {
        parser->pos = start_pos;
        return result;
    }

    // Also need at least some digits
    if ((int_digits == 0) && (frac_count == 0))
    {
        parser->pos = start_pos;
        return result;
    }

    // Build the rational.
    // Value = (int_part + frac_part / 10^frac_count) * 10^(±exp_value)
    // To keep it as a rational: num = int_part * 10^frac_count + frac_part
    //                          den = 10^frac_count
    // Then multiply/divide by 10^exp as needed.

    // Start with integer and fractional part
    uintmax_t frac_denom = 1;
    for (size_t i = 0; i < frac_count; ++i)
    {
        frac_denom *= 10;
    }

    result.num = int_part * (intmax_t) frac_denom + frac_part;
    result.den = frac_denom;

    // Apply exponent
    if (has_exp)
    {
        for (intmax_t i = 0; i < exp_value; ++i)
        {
            if (exp_neg)
            {
                result.den *= 10;
            }
            else
            {
                result.num *= 10;
            }
        }
    }

    return dsdl_rational_normalize_(result);
}

/// Parse a numeric literal (integer or real).
/// Tries real first (more specific), then integer.
DSDL_INTERNAL static dsdl_rational_t dsdl_parse_number_(dsdl_parser_t* const parser)
{
    const size_t start_pos = parser->pos;

    // Try real first (point or exponent notation)
    dsdl_rational_t result = dsdl_parse_real_(parser);
    if (result.den != 0)
    {
        return result;
    }
    parser->pos = start_pos;

    // Fall back to integer
    return dsdl_parse_integer_(parser);
}

/// Parse an identifier and return it as a borrowed string.
/// Returns empty string on failure.
DSDL_INTERNAL static wkv_str_t dsdl_parse_identifier_(dsdl_parser_t* const parser)
{
    wkv_str_t result = {0, NULL};

    if (!dsdl_is_ident_start_(dsdl_parser_peek_(parser, 0)))
    {
        return result;
    }

    const size_t start = parser->pos;
    dsdl_parser_advance_(parser, 1);

    while (dsdl_is_ident_cont_(dsdl_parser_peek_(parser, 0)))
    {
        dsdl_parser_advance_(parser, 1);
    }

    result.len = parser->pos - start;
    result.str = &parser->input[start];
    return result;
}

/// Parse a boolean literal (true/false).
/// Returns value in *out_value, returns true on success.
DSDL_INTERNAL static bool dsdl_parse_boolean_(dsdl_parser_t* const parser, bool* const out_value)
{
    if (dsdl_parser_accept_(parser, "true", 4))
    {
        // Make sure it's not followed by identifier characters (e.g., "trueX")
        if (!dsdl_is_ident_cont_(dsdl_parser_peek_(parser, 0)))
        {
            *out_value = true;
            return true;
        }
        // Backtrack
        parser->pos -= 4;
    }
    else if (dsdl_parser_accept_(parser, "false", 5))
    {
        if (!dsdl_is_ident_cont_(dsdl_parser_peek_(parser, 0)))
        {
            *out_value = false;
            return true;
        }
        parser->pos -= 5;
    }
    return false;
}

/// Parse a string literal (single or double quoted).
/// Returns the string content (without quotes, escapes NOT processed).
/// Returns empty string on failure.
DSDL_INTERNAL static wkv_str_t dsdl_parse_string_(dsdl_parser_t* const parser)
{
    wkv_str_t  result    = {0, NULL};
    const char quote     = dsdl_parser_peek_(parser, 0);
    const bool is_single = (quote == '\'');
    const bool is_double = (quote == '"');

    if (!is_single && !is_double)
    {
        return result;
    }

    dsdl_parser_advance_(parser, 1);   // Consume opening quote
    const size_t start = parser->pos;

    while (!dsdl_parser_eof_(parser))
    {
        const char c = dsdl_parser_peek_(parser, 0);

        if (c == quote)
        {
            // End of string
            result.len = parser->pos - start;
            result.str = &parser->input[start];
            dsdl_parser_advance_(parser, 1);   // Consume closing quote
            return result;
        }

        if ((c == '\r') || (c == '\n'))
        {
            // Unterminated string (newline in string)
            break;
        }

        if (c == '\\')
        {
            // Escape sequence - skip both backslash and next character
            dsdl_parser_advance_(parser, 2);
        }
        else
        {
            dsdl_parser_advance_(parser, 1);
        }
    }

    // Unterminated string - return failure
    return result;
}

// Forward declaration for recursive expression parsing
DSDL_INTERNAL static bool dsdl_parse_expression_(dsdl_parser_t* parser, dsdl_value_t* out_value);

/// Parse a set literal: { expr, expr, ... }
/// Returns true on success, fills out_value with DSDL_VALUE_SET.
DSDL_INTERNAL static bool dsdl_parse_set_(dsdl_parser_t* const parser, dsdl_value_t* const out_value)
{
    if (dsdl_parser_peek_(parser, 0) != '{')
    {
        return false;
    }
    dsdl_parser_advance_(parser, 1);   // Consume '{'
    dsdl_parser_skip_ws_(parser);

    // Initialize as empty set
    out_value->kind        = DSDL_VALUE_SET;
    out_value->as.set.count    = 0;
    out_value->as.set.elements = NULL;

    // Check for empty set
    if (dsdl_parser_peek_(parser, 0) == '}')
    {
        dsdl_parser_advance_(parser, 1);
        return true;
    }

    // For now, we'll use a simple fixed-size array approach
    // TODO: Dynamic allocation for arbitrary-sized sets
    dsdl_value_t temp_elements[64];
    size_t       count = 0;

    while (count < 64)
    {
        // Parse expression
        dsdl_value_t elem;
        if (!dsdl_parse_expression_(parser, &elem))
        {
            return false;   // Parse error
        }
        temp_elements[count++] = elem;

        dsdl_parser_skip_ws_(parser);

        // Check for comma or closing brace
        if (dsdl_parser_peek_(parser, 0) == ',')
        {
            dsdl_parser_advance_(parser, 1);
            dsdl_parser_skip_ws_(parser);
        }
        else if (dsdl_parser_peek_(parser, 0) == '}')
        {
            break;
        }
        else
        {
            return false;   // Unexpected character
        }
    }

    // Consume closing brace
    if (dsdl_parser_peek_(parser, 0) != '}')
    {
        return false;
    }
    dsdl_parser_advance_(parser, 1);

    // Allocate and copy elements
    if (count > 0)
    {
        out_value->as.set.elements = (dsdl_value_t*) dsdl_alloc_(parser->dsdl, count * sizeof(dsdl_value_t));
        if (out_value->as.set.elements == NULL)
        {
            return false;   // OOM
        }
        (void) memcpy(out_value->as.set.elements, temp_elements, count * sizeof(dsdl_value_t));
        out_value->as.set.count = count;
    }

    return true;
}

/// Parse a literal (number, string, boolean, or set).
DSDL_INTERNAL static bool dsdl_parse_literal_(dsdl_parser_t* const parser, dsdl_value_t* const out_value)
{
    // Try set first (starts with '{')
    if (dsdl_parser_peek_(parser, 0) == '{')
    {
        return dsdl_parse_set_(parser, out_value);
    }

    // Try string (starts with quote)
    const char c = dsdl_parser_peek_(parser, 0);
    if ((c == '"') || (c == '\''))
    {
        wkv_str_t str = dsdl_parse_string_(parser);
        if (str.str != NULL)
        {
            out_value->kind      = DSDL_VALUE_STRING;
            out_value->as.string = str;
            return true;
        }
        return false;
    }

    // Try boolean
    bool bool_val;
    if (dsdl_parse_boolean_(parser, &bool_val))
    {
        out_value->kind       = DSDL_VALUE_BOOL;
        out_value->as.boolean = bool_val;
        return true;
    }

    // Try number (integer or real)
    dsdl_rational_t num = dsdl_parse_number_(parser);
    if (num.den != 0)
    {
        out_value->kind        = DSDL_VALUE_RATIONAL;
        out_value->as.rational = num;
        return true;
    }

    return false;
}

// ============================================================================
// Expression parsing (Pratt parser / precedence climbing)
// ============================================================================

/// Operator precedence levels (higher = binds tighter)
typedef enum
{
    DSDL_PREC_NONE       = 0,
    DSDL_PREC_LOGICAL    = 1,   // || &&
    DSDL_PREC_COMPARISON = 2,   // == != < <= > >=
    DSDL_PREC_BITWISE    = 3,   // | ^ &
    DSDL_PREC_ADDITIVE   = 4,   // + -
    DSDL_PREC_MULT       = 5,   // * / %
    DSDL_PREC_EXP        = 6,   // **
    DSDL_PREC_ATTRIBUTE  = 7,   // .
} dsdl_prec_t;

/// Binary operator types
typedef enum
{
    DSDL_OP_NONE,
    // Logical
    DSDL_OP_OR,
    DSDL_OP_AND,
    // Comparison
    DSDL_OP_EQ,
    DSDL_OP_NE,
    DSDL_OP_LT,
    DSDL_OP_LE,
    DSDL_OP_GT,
    DSDL_OP_GE,
    // Bitwise
    DSDL_OP_BIT_OR,
    DSDL_OP_BIT_XOR,
    DSDL_OP_BIT_AND,
    // Additive
    DSDL_OP_ADD,
    DSDL_OP_SUB,
    // Multiplicative
    DSDL_OP_MUL,
    DSDL_OP_DIV,
    DSDL_OP_MOD,
    // Exponential
    DSDL_OP_POW,
    // Attribute
    DSDL_OP_DOT,
} dsdl_op_t;

/// Get precedence for a binary operator at current position.
/// Also returns the operator type and its length.
DSDL_INTERNAL static dsdl_prec_t dsdl_get_binary_op_(const dsdl_parser_t* const parser,
                                                     dsdl_op_t* const           out_op,
                                                     size_t* const              out_len)
{
    *out_op  = DSDL_OP_NONE;
    *out_len = 0;

    const char c0 = dsdl_parser_peek_(parser, 0);
    const char c1 = dsdl_parser_peek_(parser, 1);

    // Two-character operators first
    if (c0 == '|' && c1 == '|')
    {
        *out_op  = DSDL_OP_OR;
        *out_len = 2;
        return DSDL_PREC_LOGICAL;
    }
    if (c0 == '&' && c1 == '&')
    {
        *out_op  = DSDL_OP_AND;
        *out_len = 2;
        return DSDL_PREC_LOGICAL;
    }
    if (c0 == '=' && c1 == '=')
    {
        *out_op  = DSDL_OP_EQ;
        *out_len = 2;
        return DSDL_PREC_COMPARISON;
    }
    if (c0 == '!' && c1 == '=')
    {
        *out_op  = DSDL_OP_NE;
        *out_len = 2;
        return DSDL_PREC_COMPARISON;
    }
    if (c0 == '<' && c1 == '=')
    {
        *out_op  = DSDL_OP_LE;
        *out_len = 2;
        return DSDL_PREC_COMPARISON;
    }
    if (c0 == '>' && c1 == '=')
    {
        *out_op  = DSDL_OP_GE;
        *out_len = 2;
        return DSDL_PREC_COMPARISON;
    }
    if (c0 == '*' && c1 == '*')
    {
        *out_op  = DSDL_OP_POW;
        *out_len = 2;
        return DSDL_PREC_EXP;
    }

    // Single-character operators
    if (c0 == '<')
    {
        *out_op  = DSDL_OP_LT;
        *out_len = 1;
        return DSDL_PREC_COMPARISON;
    }
    if (c0 == '>')
    {
        *out_op  = DSDL_OP_GT;
        *out_len = 1;
        return DSDL_PREC_COMPARISON;
    }
    if (c0 == '|')
    {
        *out_op  = DSDL_OP_BIT_OR;
        *out_len = 1;
        return DSDL_PREC_BITWISE;
    }
    if (c0 == '^')
    {
        *out_op  = DSDL_OP_BIT_XOR;
        *out_len = 1;
        return DSDL_PREC_BITWISE;
    }
    if (c0 == '&')
    {
        *out_op  = DSDL_OP_BIT_AND;
        *out_len = 1;
        return DSDL_PREC_BITWISE;
    }
    if (c0 == '+')
    {
        *out_op  = DSDL_OP_ADD;
        *out_len = 1;
        return DSDL_PREC_ADDITIVE;
    }
    if (c0 == '-')
    {
        *out_op  = DSDL_OP_SUB;
        *out_len = 1;
        return DSDL_PREC_ADDITIVE;
    }
    if (c0 == '*')
    {
        *out_op  = DSDL_OP_MUL;
        *out_len = 1;
        return DSDL_PREC_MULT;
    }
    if (c0 == '/')
    {
        *out_op  = DSDL_OP_DIV;
        *out_len = 1;
        return DSDL_PREC_MULT;
    }
    if (c0 == '%')
    {
        *out_op  = DSDL_OP_MOD;
        *out_len = 1;
        return DSDL_PREC_MULT;
    }
    if (c0 == '.')
    {
        *out_op  = DSDL_OP_DOT;
        *out_len = 1;
        return DSDL_PREC_ATTRIBUTE;
    }

    return DSDL_PREC_NONE;
}

/// Apply a binary operation to two values.
DSDL_INTERNAL static bool dsdl_apply_binary_op_(const dsdl_op_t           op,
                                                const dsdl_value_t* const left,
                                                const dsdl_value_t* const right,
                                                dsdl_value_t* const       result)
{
    // Rational arithmetic operations
    if ((left->kind == DSDL_VALUE_RATIONAL) && (right->kind == DSDL_VALUE_RATIONAL))
    {
        const dsdl_rational_t a = left->as.rational;
        const dsdl_rational_t b = right->as.rational;

        // Arithmetic operators
        if (op == DSDL_OP_ADD)
        {
            result->kind        = DSDL_VALUE_RATIONAL;
            result->as.rational = dsdl_rational_add_(a, b);
            return true;
        }
        if (op == DSDL_OP_SUB)
        {
            result->kind        = DSDL_VALUE_RATIONAL;
            result->as.rational = dsdl_rational_sub_(a, b);
            return true;
        }
        if (op == DSDL_OP_MUL)
        {
            result->kind        = DSDL_VALUE_RATIONAL;
            result->as.rational = dsdl_rational_mul_(a, b);
            return true;
        }
        if (op == DSDL_OP_DIV)
        {
            result->kind        = DSDL_VALUE_RATIONAL;
            result->as.rational = dsdl_rational_div_(a, b);
            return true;
        }
        if (op == DSDL_OP_MOD)
        {
            // Modulo only defined for integers
            if (dsdl_rational_is_int_(a) && dsdl_rational_is_int_(b) && (b.num != 0))
            {
                result->kind        = DSDL_VALUE_RATIONAL;
                result->as.rational = dsdl_rational_from_int_(a.num % b.num);
                return true;
            }
            return false;
        }
        if (op == DSDL_OP_POW)
        {
            // Power only for integer exponent
            if (dsdl_rational_is_int_(b))
            {
                intmax_t exp = b.num;
                result->kind            = DSDL_VALUE_RATIONAL;
                result->as.rational.num = 1;
                result->as.rational.den = 1;
                if (exp < 0)
                {
                    // Negative exponent: invert base
                    dsdl_rational_t inv = {(intmax_t) a.den, dsdl_abs_(a.num)};
                    if ((a.num < 0) && (((-exp) % 2) == 1))
                    {
                        inv.num = -inv.num;
                    }
                    exp = -exp;
                    for (intmax_t i = 0; i < exp; ++i)
                    {
                        result->as.rational = dsdl_rational_mul_(result->as.rational, inv);
                    }
                }
                else
                {
                    for (intmax_t i = 0; i < exp; ++i)
                    {
                        result->as.rational = dsdl_rational_mul_(result->as.rational, a);
                    }
                }
                return true;
            }
            return false;
        }

        // Bitwise operators (integers only)
        if ((op == DSDL_OP_BIT_OR) || (op == DSDL_OP_BIT_XOR) || (op == DSDL_OP_BIT_AND))
        {
            if (dsdl_rational_is_int_(a) && dsdl_rational_is_int_(b))
            {
                intmax_t r = 0;
                if (op == DSDL_OP_BIT_OR)
                {
                    r = a.num | b.num;
                }
                else if (op == DSDL_OP_BIT_XOR)
                {
                    r = a.num ^ b.num;
                }
                else   // DSDL_OP_BIT_AND
                {
                    r = a.num & b.num;
                }
                result->kind        = DSDL_VALUE_RATIONAL;
                result->as.rational = dsdl_rational_from_int_(r);
                return true;
            }
            return false;
        }

        // Comparison operators
        const int cmp = dsdl_rational_cmp_(a, b);
        result->kind  = DSDL_VALUE_BOOL;
        if (op == DSDL_OP_EQ)
        {
            result->as.boolean = (cmp == 0);
            return true;
        }
        if (op == DSDL_OP_NE)
        {
            result->as.boolean = (cmp != 0);
            return true;
        }
        if (op == DSDL_OP_LT)
        {
            result->as.boolean = (cmp < 0);
            return true;
        }
        if (op == DSDL_OP_LE)
        {
            result->as.boolean = (cmp <= 0);
            return true;
        }
        if (op == DSDL_OP_GT)
        {
            result->as.boolean = (cmp > 0);
            return true;
        }
        if (op == DSDL_OP_GE)
        {
            result->as.boolean = (cmp >= 0);
            return true;
        }
    }

    // Boolean operations
    if ((left->kind == DSDL_VALUE_BOOL) && (right->kind == DSDL_VALUE_BOOL))
    {
        result->kind = DSDL_VALUE_BOOL;
        if (op == DSDL_OP_OR)
        {
            result->as.boolean = left->as.boolean || right->as.boolean;
            return true;
        }
        if (op == DSDL_OP_AND)
        {
            result->as.boolean = left->as.boolean && right->as.boolean;
            return true;
        }
        if (op == DSDL_OP_EQ)
        {
            result->as.boolean = (left->as.boolean == right->as.boolean);
            return true;
        }
        if (op == DSDL_OP_NE)
        {
            result->as.boolean = (left->as.boolean != right->as.boolean);
            return true;
        }
    }

    return false;   // Unsupported operation or type mismatch
}

// Forward declaration for expression parsing
DSDL_INTERNAL static bool dsdl_parse_expr_prec_(dsdl_parser_t* parser, dsdl_prec_t min_prec, dsdl_value_t* out_value);

/// Parse an atom (literal, identifier, or parenthesized expression).
DSDL_INTERNAL static bool dsdl_parse_atom_(dsdl_parser_t* const parser, dsdl_value_t* const out_value)
{
    dsdl_parser_skip_ws_(parser);

    // Parenthesized expression
    if (dsdl_parser_peek_(parser, 0) == '(')
    {
        dsdl_parser_advance_(parser, 1);
        dsdl_parser_skip_ws_(parser);

        if (!dsdl_parse_expression_(parser, out_value))
        {
            return false;
        }

        dsdl_parser_skip_ws_(parser);
        if (dsdl_parser_peek_(parser, 0) != ')')
        {
            return false;   // Missing closing paren
        }
        dsdl_parser_advance_(parser, 1);
        return true;
    }

    // Try literal first
    if (dsdl_parse_literal_(parser, out_value))
    {
        return true;
    }

    // Try identifier (could be a constant name or type reference)
    wkv_str_t ident = dsdl_parse_identifier_(parser);
    if (ident.str != NULL)
    {
        // For now, just store as a string (will be resolved during semantic analysis)
        out_value->kind      = DSDL_VALUE_STRING;
        out_value->as.string = ident;
        return true;
    }

    return false;
}

/// Parse unary prefix operators (!, +, -).
DSDL_INTERNAL static bool dsdl_parse_unary_(dsdl_parser_t* const parser, dsdl_value_t* const out_value)
{
    dsdl_parser_skip_ws_(parser);

    const char c = dsdl_parser_peek_(parser, 0);

    // Logical NOT
    if (c == '!')
    {
        dsdl_parser_advance_(parser, 1);
        dsdl_parser_skip_ws_(parser);

        dsdl_value_t operand;
        if (!dsdl_parse_unary_(parser, &operand))
        {
            return false;
        }

        if (operand.kind != DSDL_VALUE_BOOL)
        {
            return false;   // NOT only valid for booleans
        }

        out_value->kind       = DSDL_VALUE_BOOL;
        out_value->as.boolean = !operand.as.boolean;
        return true;
    }

    // Unary plus
    if (c == '+')
    {
        dsdl_parser_advance_(parser, 1);
        dsdl_parser_skip_ws_(parser);

        // Parse the operand at exponential precedence (just above unary)
        return dsdl_parse_expr_prec_(parser, DSDL_PREC_EXP, out_value);
    }

    // Unary minus
    if (c == '-')
    {
        dsdl_parser_advance_(parser, 1);
        dsdl_parser_skip_ws_(parser);

        dsdl_value_t operand;
        if (!dsdl_parse_expr_prec_(parser, DSDL_PREC_EXP, &operand))
        {
            return false;
        }

        if (operand.kind != DSDL_VALUE_RATIONAL)
        {
            return false;   // Negation only valid for numbers
        }

        out_value->kind        = DSDL_VALUE_RATIONAL;
        out_value->as.rational = dsdl_rational_neg_(operand.as.rational);
        return true;
    }

    // No unary operator, parse atom
    return dsdl_parse_atom_(parser, out_value);
}

/// Parse expression with precedence climbing.
DSDL_INTERNAL static bool dsdl_parse_expr_prec_(dsdl_parser_t* const parser,
                                                const dsdl_prec_t    min_prec,
                                                dsdl_value_t* const  out_value)
{
    // Parse left-hand side (unary or atom)
    if (!dsdl_parse_unary_(parser, out_value))
    {
        return false;
    }

    while (true)
    {
        dsdl_parser_skip_ws_(parser);

        // Check for binary operator
        dsdl_op_t op;
        size_t    op_len;
        const dsdl_prec_t prec = dsdl_get_binary_op_(parser, &op, &op_len);

        if (prec == DSDL_PREC_NONE || prec < min_prec)
        {
            break;
        }

        // Consume operator
        dsdl_parser_advance_(parser, op_len);
        dsdl_parser_skip_ws_(parser);

        // Handle attribute access (.) specially
        if (op == DSDL_OP_DOT)
        {
            wkv_str_t attr = dsdl_parse_identifier_(parser);
            if (attr.str == NULL)
            {
                return false;
            }
            // TODO: Implement attribute access (e.g., _offset_.min)
            // For now, we'll just ignore attribute access
            continue;
        }

        // Parse right-hand side with higher precedence (for left-associativity)
        // For right-associative ** we would use same precedence
        dsdl_prec_t next_prec = (dsdl_prec_t)(prec + 1);
        if (op == DSDL_OP_POW)
        {
            next_prec = prec;   // Right-associative
        }

        dsdl_value_t right;
        if (!dsdl_parse_expr_prec_(parser, next_prec, &right))
        {
            return false;
        }

        // Apply operator
        dsdl_value_t result;
        if (!dsdl_apply_binary_op_(op, out_value, &right, &result))
        {
            return false;
        }
        *out_value = result;
    }

    return true;
}

/// Parse an expression (top-level entry point).
DSDL_INTERNAL static bool dsdl_parse_expression_(dsdl_parser_t* const parser, dsdl_value_t* const out_value)
{
    return dsdl_parse_expr_prec_(parser, DSDL_PREC_LOGICAL, out_value);
}

// ============================================================================
// Type parsing
// ============================================================================

/// Parsed type representation during parsing.
/// This is an intermediate representation before converting to dsdl_type_t.
typedef struct
{
    dsdl_type_t kind;          ///< DSDL_xxx type kind constant
    uint8_t     bit_width;     ///< Bit width for primitives/void
    bool        is_saturated;  ///< true = saturated (default), false = truncated
    bool        is_variable;   ///< For arrays: is variable-length
    bool        is_inclusive;  ///< For variable arrays: inclusive vs exclusive
    size_t      array_size;    ///< Array capacity (max size for variable, fixed size for fixed)
    wkv_str_t   type_name;     ///< For composite types: full type name
    uint8_t     version_major; ///< For versioned types
    uint8_t     version_minor; ///< For versioned types
} dsdl_parsed_type_t;

/// Parse a bit length suffix (1-64).
/// Returns 0 on failure, otherwise the bit length.
DSDL_INTERNAL static uint8_t dsdl_parse_bit_length_(dsdl_parser_t* const parser)
{
    if (!dsdl_is_digit_(dsdl_parser_peek_(parser, 0)))
    {
        return 0;
    }
    // First digit must be 1-9 (no leading zeros allowed for non-zero numbers)
    if (dsdl_parser_peek_(parser, 0) == '0')
    {
        return 0;   // Bit length can't start with 0
    }

    size_t value = 0;
    while (dsdl_is_digit_(dsdl_parser_peek_(parser, 0)))
    {
        value = value * 10 + (size_t)(dsdl_parser_peek_(parser, 0) - '0');
        dsdl_parser_advance_(parser, 1);
        if (value > 64)
        {
            return 0;   // Overflow - bit length too large
        }
    }

    return (uint8_t) value;
}

/// Parse a void type: void[1-64]
DSDL_INTERNAL static bool dsdl_parse_type_void_(dsdl_parser_t* const parser, dsdl_parsed_type_t* const out_type)
{
    if (!dsdl_parser_accept_(parser, "void", 4))
    {
        return false;
    }

    const uint8_t bits = dsdl_parse_bit_length_(parser);
    if ((bits < 1) || (bits > 64))
    {
        return false;
    }

    out_type->kind      = DSDL_VOID1 + bits - 1;   // DSDL_VOID1 .. DSDL_VOID64
    out_type->bit_width = bits;
    return true;
}

/// Parse a primitive type name (uint, int, float) with bit width.
DSDL_INTERNAL static bool dsdl_parse_primitive_name_(dsdl_parser_t* const parser, dsdl_parsed_type_t* const out_type)
{
    if (dsdl_parser_accept_(parser, "uint", 4))
    {
        const uint8_t bits = dsdl_parse_bit_length_(parser);
        if ((bits < 1) || (bits > 64))
        {
            return false;
        }
        out_type->kind      = DSDL_UINT1 + bits - 1;
        out_type->bit_width = bits;
        return true;
    }

    if (dsdl_parser_accept_(parser, "int", 3))
    {
        const uint8_t bits = dsdl_parse_bit_length_(parser);
        if ((bits < 2) || (bits > 64))
        {
            return false;   // int requires at least 2 bits
        }
        out_type->kind      = DSDL_INT2 + bits - 2;
        out_type->bit_width = bits;
        return true;
    }

    if (dsdl_parser_accept_(parser, "float", 5))
    {
        const uint8_t bits = dsdl_parse_bit_length_(parser);
        if ((bits != 16) && (bits != 32) && (bits != 64))
        {
            return false;   // Only float16, float32, float64
        }
        if (bits == 16)
        {
            out_type->kind = DSDL_FLOAT16;
        }
        else if (bits == 32)
        {
            out_type->kind = DSDL_FLOAT32;
        }
        else
        {
            out_type->kind = DSDL_FLOAT64;
        }
        out_type->bit_width = bits;
        return true;
    }

    return false;
}

/// Parse a primitive type (bool, byte, utf8, or [saturated/truncated] primitive_name).
DSDL_INTERNAL static bool dsdl_parse_type_primitive_(dsdl_parser_t* const parser, dsdl_parsed_type_t* const out_type)
{
    out_type->is_saturated = true;   // Default

    // Check for "bool"
    if (dsdl_parser_match_(parser, "bool", 4) && !dsdl_is_ident_cont_(dsdl_parser_peek_(parser, 4)))
    {
        dsdl_parser_advance_(parser, 4);
        out_type->kind      = DSDL_BOOL;
        out_type->bit_width = 1;
        return true;
    }

    // Check for "byte"
    if (dsdl_parser_match_(parser, "byte", 4) && !dsdl_is_ident_cont_(dsdl_parser_peek_(parser, 4)))
    {
        dsdl_parser_advance_(parser, 4);
        out_type->kind      = DSDL_BYTE;
        out_type->bit_width = 8;
        return true;
    }

    // Check for "utf8" (alias for uint8)
    if (dsdl_parser_match_(parser, "utf8", 4) && !dsdl_is_ident_cont_(dsdl_parser_peek_(parser, 4)))
    {
        dsdl_parser_advance_(parser, 4);
        out_type->kind      = DSDL_UINT8;   // utf8 is alias for uint8
        out_type->bit_width = 8;
        return true;
    }

    // Check for "truncated" modifier
    if (dsdl_parser_match_(parser, "truncated", 9) && !dsdl_is_ident_cont_(dsdl_parser_peek_(parser, 9)))
    {
        dsdl_parser_advance_(parser, 9);
        dsdl_parser_skip_ws_(parser);
        out_type->is_saturated = false;
        return dsdl_parse_primitive_name_(parser, out_type);
    }

    // Check for optional "saturated" modifier
    if (dsdl_parser_match_(parser, "saturated", 9) && !dsdl_is_ident_cont_(dsdl_parser_peek_(parser, 9)))
    {
        dsdl_parser_advance_(parser, 9);
        dsdl_parser_skip_ws_(parser);
    }

    return dsdl_parse_primitive_name_(parser, out_type);
}

/// Parse a versioned type reference: namespace.Name.major.minor
DSDL_INTERNAL static bool dsdl_parse_type_versioned_(dsdl_parser_t* const parser, dsdl_parsed_type_t* const out_type)
{
    const size_t start_pos = parser->pos;

    // Must start with identifier
    wkv_str_t first = dsdl_parse_identifier_(parser);
    if (first.str == NULL)
    {
        return false;
    }

    // Continue parsing namespace parts (identifier.identifier...)
    while (dsdl_parser_peek_(parser, 0) == '.')
    {
        dsdl_parser_advance_(parser, 1);   // Skip '.'

        // Check if next part is an identifier or version number
        if (!dsdl_is_ident_start_(dsdl_parser_peek_(parser, 0)))
        {
            // Should be version specifier now
            break;
        }

        wkv_str_t part = dsdl_parse_identifier_(parser);
        if (part.str == NULL)
        {
            parser->pos = start_pos;
            return false;
        }
    }

    // Now we should have type name (without version).
    // The last '.' was consumed, so now parse version: major.minor
    const size_t type_name_end = parser->pos - 1;   // Before the last '.'

    // Parse major version
    dsdl_rational_t major_r = dsdl_parse_int_decimal_(parser);
    if ((major_r.den == 0) || !dsdl_rational_is_int_(major_r))
    {
        parser->pos = start_pos;
        return false;
    }

    // Expect '.'
    if (dsdl_parser_peek_(parser, 0) != '.')
    {
        parser->pos = start_pos;
        return false;
    }
    dsdl_parser_advance_(parser, 1);

    // Parse minor version
    dsdl_rational_t minor_r = dsdl_parse_int_decimal_(parser);
    if ((minor_r.den == 0) || !dsdl_rational_is_int_(minor_r))
    {
        parser->pos = start_pos;
        return false;
    }

    // Verify versions are valid (0-255)
    if ((major_r.num < 0) || (major_r.num > 255) || (minor_r.num < 0) || (minor_r.num > 255))
    {
        parser->pos = start_pos;
        return false;
    }

    // Success - fill out type
    out_type->kind          = DSDL_COMPOSITE_STRUCT;   // Placeholder until we resolve
    out_type->type_name.str = first.str;
    out_type->type_name.len = type_name_end - start_pos;
    out_type->version_major = (uint8_t) major_r.num;
    out_type->version_minor = (uint8_t) minor_r.num;

    return true;
}

/// Parse a scalar type (primitive, void, or versioned composite).
DSDL_INTERNAL static bool dsdl_parse_type_scalar_(dsdl_parser_t* const parser, dsdl_parsed_type_t* const out_type)
{
    const size_t start_pos = parser->pos;

    // Try void type first
    if (dsdl_parse_type_void_(parser, out_type))
    {
        return true;
    }
    parser->pos = start_pos;

    // Try primitive type
    if (dsdl_parse_type_primitive_(parser, out_type))
    {
        return true;
    }
    parser->pos = start_pos;

    // Try versioned composite type
    return dsdl_parse_type_versioned_(parser, out_type);
}

/// Parse an array type: scalar_type [ expression ] or scalar_type [ <= expression ] etc.
DSDL_INTERNAL static bool dsdl_parse_type_array_(dsdl_parser_t* const       parser,
                                                  dsdl_parsed_type_t* const out_type)
{
    // First parse the element type (scalar)
    if (!dsdl_parse_type_scalar_(parser, out_type))
    {
        return false;
    }

    dsdl_parser_skip_ws_(parser);

    // Check for array brackets
    if (dsdl_parser_peek_(parser, 0) != '[')
    {
        return true;   // Not an array, just a scalar - success
    }
    dsdl_parser_advance_(parser, 1);   // Skip '['
    dsdl_parser_skip_ws_(parser);

    // Check for variable array indicators
    out_type->is_variable  = false;
    out_type->is_inclusive = false;

    if (dsdl_parser_accept_(parser, "<=", 2))
    {
        out_type->is_variable  = true;
        out_type->is_inclusive = true;
        dsdl_parser_skip_ws_(parser);
    }
    else if (dsdl_parser_accept_(parser, "<", 1))
    {
        out_type->is_variable  = true;
        out_type->is_inclusive = false;
        dsdl_parser_skip_ws_(parser);
    }

    // Parse array size expression
    dsdl_value_t size_val;
    if (!dsdl_parse_expression_(parser, &size_val))
    {
        return false;
    }

    // Size must be a positive integer
    if ((size_val.kind != DSDL_VALUE_RATIONAL) || !dsdl_rational_is_int_(size_val.as.rational))
    {
        return false;
    }
    if (size_val.as.rational.num <= 0)
    {
        return false;
    }

    out_type->array_size = (size_t) size_val.as.rational.num;

    dsdl_parser_skip_ws_(parser);

    // Expect closing bracket
    if (dsdl_parser_peek_(parser, 0) != ']')
    {
        return false;
    }
    dsdl_parser_advance_(parser, 1);

    // Update type kind to array
    if (out_type->is_variable)
    {
        out_type->kind = DSDL_ARRAY_VARIABLE;
    }
    else
    {
        out_type->kind = DSDL_ARRAY_FIXED;
    }

    return true;
}

/// Parse any type (the main entry point for type parsing).
DSDL_INTERNAL static bool dsdl_parse_type_(dsdl_parser_t* const parser, dsdl_parsed_type_t* const out_type)
{
    (void) memset(out_type, 0, sizeof(*out_type));
    return dsdl_parse_type_array_(parser, out_type);
}

// ============================================================================
// Statement parsing
// ============================================================================

/// Statement kind enumeration.
typedef enum
{
    DSDL_STMT_NONE,
    DSDL_STMT_CONSTANT,        ///< type name = expression
    DSDL_STMT_FIELD,           ///< type name
    DSDL_STMT_PADDING,         ///< void type (no name)
    DSDL_STMT_DIRECTIVE,       ///< @directive [expression]
    DSDL_STMT_SERVICE_MARKER,  ///< ---
} dsdl_stmt_kind_t;

/// Parsed statement representation.
typedef struct
{
    dsdl_stmt_kind_t    kind;
    dsdl_parsed_type_t  type;       ///< Type for CONSTANT, FIELD, PADDING
    wkv_str_t           name;       ///< Name for CONSTANT, FIELD, DIRECTIVE
    dsdl_value_t        value;      ///< Value for CONSTANT, optional for DIRECTIVE
    bool                has_value;  ///< True if value is set (for DIRECTIVE)
} dsdl_parsed_stmt_t;

/// Parse a directive: @name [expression]
DSDL_INTERNAL static bool dsdl_parse_directive_(dsdl_parser_t* const parser, dsdl_parsed_stmt_t* const out_stmt)
{
    if (dsdl_parser_peek_(parser, 0) != '@')
    {
        return false;
    }
    dsdl_parser_advance_(parser, 1);   // Skip '@'

    wkv_str_t name = dsdl_parse_identifier_(parser);
    if (name.str == NULL)
    {
        return false;
    }

    out_stmt->kind      = DSDL_STMT_DIRECTIVE;
    out_stmt->name      = name;
    out_stmt->has_value = false;

    // Check for optional expression (requires whitespace separator)
    dsdl_parser_skip_ws_(parser);

    // Try to parse expression (may fail if no expression follows)
    const size_t start_pos = parser->pos;
    if (dsdl_parse_expression_(parser, &out_stmt->value))
    {
        out_stmt->has_value = true;
    }
    else
    {
        parser->pos = start_pos;   // Backtrack
    }

    return true;
}

/// Parse service response marker: ---+
DSDL_INTERNAL static bool dsdl_parse_service_marker_(dsdl_parser_t* const parser, dsdl_parsed_stmt_t* const out_stmt)
{
    // Must have at least three dashes
    if (!(dsdl_parser_peek_(parser, 0) == '-' && dsdl_parser_peek_(parser, 1) == '-' &&
          dsdl_parser_peek_(parser, 2) == '-'))
    {
        return false;
    }

    // Consume all consecutive dashes
    while (dsdl_parser_peek_(parser, 0) == '-')
    {
        dsdl_parser_advance_(parser, 1);
    }

    out_stmt->kind = DSDL_STMT_SERVICE_MARKER;
    return true;
}

/// Parse a statement (attribute statement: constant, field, or padding).
DSDL_INTERNAL static bool dsdl_parse_attribute_stmt_(dsdl_parser_t* const parser, dsdl_parsed_stmt_t* const out_stmt)
{
    // Parse type
    if (!dsdl_parse_type_(parser, &out_stmt->type))
    {
        return false;
    }

    dsdl_parser_skip_ws_(parser);

    // Check if this is a void (padding) field - no name allowed
    if (dsdl_type_is_void(out_stmt->type.kind) && !dsdl_type_is_array(out_stmt->type.kind))
    {
        out_stmt->kind = DSDL_STMT_PADDING;
        return true;
    }

    // Parse name (required for non-padding fields)
    wkv_str_t name = dsdl_parse_identifier_(parser);
    if (name.str == NULL)
    {
        return false;
    }

    out_stmt->name = name;
    dsdl_parser_skip_ws_(parser);

    // Check for constant assignment
    if (dsdl_parser_peek_(parser, 0) == '=')
    {
        dsdl_parser_advance_(parser, 1);   // Skip '='
        dsdl_parser_skip_ws_(parser);

        if (!dsdl_parse_expression_(parser, &out_stmt->value))
        {
            return false;
        }

        out_stmt->kind      = DSDL_STMT_CONSTANT;
        out_stmt->has_value = true;
    }
    else
    {
        out_stmt->kind = DSDL_STMT_FIELD;
    }

    return true;
}

/// Parse a statement (any kind).
DSDL_INTERNAL static bool dsdl_parse_statement_(dsdl_parser_t* const parser, dsdl_parsed_stmt_t* const out_stmt)
{
    (void) memset(out_stmt, 0, sizeof(*out_stmt));

    dsdl_parser_skip_ws_(parser);

    // Empty line or comment-only line
    const char c = dsdl_parser_peek_(parser, 0);
    if ((c == '\0') || (c == '\r') || (c == '\n') || (c == '#'))
    {
        out_stmt->kind = DSDL_STMT_NONE;
        return true;
    }

    // Try directive (@...)
    if (c == '@')
    {
        return dsdl_parse_directive_(parser, out_stmt);
    }

    // Try service marker (---)
    if (c == '-')
    {
        return dsdl_parse_service_marker_(parser, out_stmt);
    }

    // Otherwise it's an attribute statement (type name / type name = expr)
    return dsdl_parse_attribute_stmt_(parser, out_stmt);
}

/// Parse a single line (statement + optional comment).
DSDL_INTERNAL static bool dsdl_parse_line_(dsdl_parser_t* const parser, dsdl_parsed_stmt_t* const out_stmt)
{
    // Parse statement
    if (!dsdl_parse_statement_(parser, out_stmt))
    {
        return false;
    }

    // Skip trailing whitespace and comment
    dsdl_parser_skip_line_tail_(parser);

    return true;
}

// ============================================================================
// Definition parsing
// ============================================================================

/// Maximum number of fields/constants in a single type.
#define DSDL_MAX_FIELDS 256

/// Parsed definition intermediate representation.
typedef struct
{
    bool   is_service;   ///< True if service type (has request/response)
    size_t field_count;  ///< Number of fields in request (or struct/union)
    size_t const_count;  ///< Number of constants

    dsdl_parsed_type_t field_types[DSDL_MAX_FIELDS];
    wkv_str_t          field_names[DSDL_MAX_FIELDS];
    dsdl_value_t       const_values[DSDL_MAX_FIELDS];
    wkv_str_t          const_names[DSDL_MAX_FIELDS];
    dsdl_parsed_type_t const_types[DSDL_MAX_FIELDS];

    // For service types: response fields
    size_t             response_field_count;
    dsdl_parsed_type_t response_field_types[DSDL_MAX_FIELDS];
    wkv_str_t          response_field_names[DSDL_MAX_FIELDS];

    // Directives
    bool   has_extent;
    size_t extent_bits;
    bool   is_sealed;
    bool   is_deprecated;
    bool   is_union;
} dsdl_parsed_def_t;

/// Parse a complete DSDL definition.
DSDL_INTERNAL static bool dsdl_parse_definition_(dsdl_parser_t* const     parser,
                                                  dsdl_parsed_def_t* const out_def)
{
    (void) memset(out_def, 0, sizeof(*out_def));

    bool parsing_response = false;

    while (!dsdl_parser_eof_(parser))
    {
        dsdl_parsed_stmt_t stmt;
        if (!dsdl_parse_line_(parser, &stmt))
        {
            return false;   // Parse error
        }

        // Process statement based on kind
        switch (stmt.kind)
        {
        case DSDL_STMT_NONE:
            // Empty line or comment only - skip
            break;

        case DSDL_STMT_FIELD:
        case DSDL_STMT_PADDING:
        {
            const size_t max_fields = DSDL_MAX_FIELDS;
            if (parsing_response)
            {
                if (out_def->response_field_count >= max_fields)
                {
                    return false;   // Too many fields
                }
                const size_t idx                      = out_def->response_field_count++;
                out_def->response_field_types[idx] = stmt.type;
                out_def->response_field_names[idx] = stmt.name;
            }
            else
            {
                if (out_def->field_count >= max_fields)
                {
                    return false;   // Too many fields
                }
                const size_t idx             = out_def->field_count++;
                out_def->field_types[idx] = stmt.type;
                out_def->field_names[idx] = stmt.name;
            }
            break;
        }

        case DSDL_STMT_CONSTANT:
            if (out_def->const_count >= DSDL_MAX_FIELDS)
            {
                return false;   // Too many constants
            }
            {
                const size_t idx             = out_def->const_count++;
                out_def->const_types[idx]  = stmt.type;
                out_def->const_names[idx]  = stmt.name;
                out_def->const_values[idx] = stmt.value;
            }
            break;

        case DSDL_STMT_SERVICE_MARKER:
            if (parsing_response)
            {
                return false;   // Duplicate service marker
            }
            out_def->is_service    = true;
            parsing_response       = true;
            break;

        case DSDL_STMT_DIRECTIVE:
            // Process known directives
            if ((stmt.name.len == 6) && (memcmp(stmt.name.str, "sealed", 6) == 0))
            {
                out_def->is_sealed = true;
            }
            else if ((stmt.name.len == 6) && (memcmp(stmt.name.str, "extent", 6) == 0))
            {
                if (!stmt.has_value || (stmt.value.kind != DSDL_VALUE_RATIONAL) ||
                    !dsdl_rational_is_int_(stmt.value.as.rational))
                {
                    return false;   // Invalid extent
                }
                out_def->has_extent  = true;
                out_def->extent_bits = (size_t) stmt.value.as.rational.num;
            }
            else if ((stmt.name.len == 10) && (memcmp(stmt.name.str, "deprecated", 10) == 0))
            {
                out_def->is_deprecated = true;
            }
            else if ((stmt.name.len == 5) && (memcmp(stmt.name.str, "union", 5) == 0))
            {
                out_def->is_union = true;
            }
            // Other directives: @assert, @print - handled differently (semantic analysis)
            break;

        default:
            break;
        }

        // Skip end-of-line
        (void) dsdl_parser_skip_eol_(parser);
    }

    return true;
}

// ============================================================================
// Public API implementation
// ============================================================================

void dsdl_new(dsdl_t* const self, void* (*const realloc_func)(dsdl_t*, void*, size_t))
{
    if ((self == NULL) || (realloc_func == NULL))
    {
        return;
    }

    // Zero-initialize
    (void) memset(self, 0, sizeof(*self));

    // Set up memory allocator
    self->realloc = realloc_func;

    // Initialize WKV containers for types and namespaces
    // The WKV realloc signature matches ours, but we need to cast
    wkv_init(&self->types, (wkv_realloc_t) realloc_func);
    self->types.sep = '.';   // Use '.' as separator for type names

    wkv_init(&self->namespaces, (wkv_realloc_t) realloc_func);
    self->namespaces.sep = '/';   // Use '/' as separator for paths
}

void dsdl_destroy(dsdl_t* const self)
{
    if (self == NULL)
    {
        return;
    }

    // Free all type definitions stored in the types WKV
    // TODO: iterate and free each composite type

    // Free WKV internal allocations
    while (!wkv_is_empty(&self->types))
    {
        wkv_node_t* const node = wkv_at(&self->types, 0);
        if (node != NULL)
        {
            // Free the type data if present
            if (node->value != NULL)
            {
                dsdl_free_(self, node->value);
            }
            wkv_del(&self->types, node);
        }
    }

    while (!wkv_is_empty(&self->namespaces))
    {
        wkv_node_t* const node = wkv_at(&self->namespaces, 0);
        if (node != NULL)
        {
            // Namespace values are just markers, no need to free
            wkv_del(&self->namespaces, node);
        }
    }
}

bool dsdl_add_namespace(dsdl_t* const self, const wkv_str_t root_directory)
{
    if ((self == NULL) || (root_directory.str == NULL) || (root_directory.len == 0))
    {
        return false;
    }

    // Add to namespaces WKV
    wkv_node_t* const node = wkv_set(&self->namespaces, root_directory);
    if (node == NULL)
    {
        return false;   // OOM
    }

    // Mark as valid namespace (non-NULL value)
    node->value = (void*) 1;   // Just a marker
    return true;
}

const dsdl_composite_t* dsdl_read(dsdl_t* const self, const wkv_str_t type_name)
{
    if ((self == NULL) || (type_name.str == NULL) || (type_name.len == 0))
    {
        return NULL;
    }

    // Check if already loaded
    wkv_node_t* const cached = wkv_get(&self->types, type_name);
    if (cached != NULL)
    {
        return (const dsdl_composite_t*) cached->value;
    }

    // TODO: Implement type loading
    // 1. Parse type name to extract namespace, name, version
    // 2. Search namespace directories for matching .dsdl file
    // 3. Read and parse the file
    // 4. Recursively load dependencies
    // 5. Cache in self->types

    return NULL;   // Not yet implemented
}

size_t dsdl_serialized_footprint(const dsdl_composite_t* const type)
{
    if (type == NULL)
    {
        return 0;
    }

    // TODO: Implement extent calculation
    // Return ceil(extent / 8) bytes
    return (type->extent + 7U) / 8U;
}

size_t dsdl_serialize(const dsdl_composite_t* const type, const size_t output_size, void* const output)
{
    if ((type == NULL) || (output == NULL) || (output_size == 0))
    {
        return 0;
    }

    // TODO: Implement serialization
    (void) type;
    (void) output_size;
    (void) output;

    return 0;   // Not yet implemented
}

size_t dsdl_deserialize(dsdl_composite_t* const type, const size_t input_size, const void* const input)
{
    if ((type == NULL) || (input == NULL) || (input_size == 0))
    {
        return 0;
    }

    // TODO: Implement deserialization
    (void) type;
    (void) input_size;
    (void) input;

    return 0;   // Not yet implemented
}
