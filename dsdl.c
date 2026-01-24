/// Cyphal DSDL Parser in C
///
/// This is a compact C99+ implementation of a Cyphal DSDL parser that allows
/// loading DSDL definitions at runtime without compile-time code generation.
///
/// See AGENTS.md and docs/IMPLEMENTATION_PLAN.md for design details.
///
/// Copyright (c) OpenCyphal Development Team

#include "dsdl.h"

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>
#include <math.h>

// ============================================================================
// Internal type definitions
// ============================================================================

/// Rational number for exact arithmetic during expression evaluation.
/// Per DSDL spec section 3.1: rationals must be stored normalized with
/// positive denominator and GCD(num, den) == 1.
///
/// When arithmetic operations would overflow intmax_t/uintmax_t, the rational
/// is approximated by repeatedly halving both numerator and denominator until
/// representable. This trades exactness for bounded representation.
typedef struct
{
    intmax_t  num; ///< Numerator (signed)
    uintmax_t den; ///< Denominator (always positive, 0 means NaN, 1 for integers)
} dsdl_rational_t;

/// Expression value types for compile-time evaluation.
typedef enum
{
    dsdl_value_rational, ///< Numeric value (integer or rational)
    dsdl_value_string,   ///< Unicode string
    dsdl_value_bool,     ///< Boolean
    dsdl_value_set,      ///< Set of values
    dsdl_value_type,     ///< Serializable metatype reference
} dsdl_value_kind_t;

/// Forward declaration for recursive type.
typedef struct dsdl_value_t dsdl_value_t;

/// Runtime value during expression evaluation.
struct dsdl_value_t
{
    dsdl_value_kind_t kind;
    union dsdl_value_data_t
    {
        dsdl_rational_t rational; ///< dsdl_value_rational
        wkv_str_t       string;   ///< dsdl_value_string (borrowed pointer)
        bool            boolean;  ///< dsdl_value_bool
        struct                    ///< dsdl_value_set
        {
            size_t        count;
            dsdl_value_t* elements;
        } set;
        void* type_ref; ///< dsdl_value_type (pointer to dsdl_type_composite_t)
    } as;
};

// ============================================================================
// Rational arithmetic
// ============================================================================

/// Compute GCD using Euclidean algorithm.
static uintmax_t dsdl_gcd(uintmax_t a, uintmax_t b)
{
    while (b != 0) {
        const uintmax_t t = b;
        b                 = a % b;
        a                 = t;
    }
    return a;
}

/// Absolute value for signed integers.
static uintmax_t dsdl_abs(intmax_t x)
{
    // Handle INTMAX_MIN carefully to avoid UB
    if (x >= 0) {
        return (uintmax_t)x;
    }
    // For negative values, negate. Special case for INTMAX_MIN.
    if (x == INTMAX_MIN) {
        return (uintmax_t)INTMAX_MAX + 1U;
    }
    return (uintmax_t)(-x);
}

/// Check if signed multiplication would overflow.
/// Returns true if a * b would overflow intmax_t.
static bool dsdl_would_overflow_mul_signed(intmax_t a, intmax_t b)
{
    if (a == 0 || b == 0) {
        return false;
    }
    const uintmax_t abs_a = dsdl_abs(a);
    const uintmax_t abs_b = dsdl_abs(b);
    // Check if abs_a * abs_b > INTMAX_MAX (or INTMAX_MAX+1 for negative result)
    // Since both are unsigned, we can check: abs_a > INTMAX_MAX / abs_b
    // But we need to be careful: result can be INTMAX_MAX+1 if signs differ
    const uintmax_t limit = ((a < 0) != (b < 0)) ? ((uintmax_t)INTMAX_MAX + 1U) : (uintmax_t)INTMAX_MAX;
    return abs_a > limit / abs_b;
}

/// Check if unsigned multiplication would overflow.
/// Returns true if a * b would overflow uintmax_t.
static bool dsdl_would_overflow_mul_unsigned(uintmax_t a, uintmax_t b)
{
    if (a == 0 || b == 0) {
        return false;
    }
    return a > UINTMAX_MAX / b;
}

/// Check if signed addition would overflow.
/// Returns true if a + b would overflow intmax_t.
static bool dsdl_would_overflow_add_signed(intmax_t a, intmax_t b)
{
    if (b > 0 && a > INTMAX_MAX - b) {
        return true;
    }
    if (b < 0 && a < INTMAX_MIN - b) {
        return true;
    }
    return false;
}

/// Reduce a rational by halving both numerator and denominator.
/// This loses precision but ensures the value stays approximately correct.
/// Rounds toward zero.
static dsdl_rational_t dsdl_rational_halve(dsdl_rational_t r)
{
    // Halve both, rounding toward zero
    r.num = r.num / 2;
    r.den = r.den / 2;
    // Don't let denominator become 0
    if (r.den == 0) {
        r.den = 1;
    }
    return r;
}

/// Create a rational from an integer.
static dsdl_rational_t dsdl_rational_from_int(intmax_t value)
{
    dsdl_rational_t r;
    r.num = value;
    r.den = 1;
    return r;
}

/// Normalize a rational: ensure den > 0 and GCD(|num|, den) == 1.
static dsdl_rational_t dsdl_rational_normalize(dsdl_rational_t r)
{
    if (r.den == 0) {
        // NaN - return as-is
        return r;
    }
    // Reduce by GCD
    const uintmax_t g = dsdl_gcd(dsdl_abs(r.num), r.den);
    if (g > 1) {
        r.num = r.num / (intmax_t)g;
        r.den = r.den / g;
    }
    return r;
}

/// Add two rationals: a/b + c/d = (ad + bc) / bd
/// Uses overflow-safe arithmetic with halving approximation.
static dsdl_rational_t dsdl_rational_add(dsdl_rational_t a, dsdl_rational_t b)
{
    // First ensure denominators fit in intmax_t (required for signed multiplication)
    while (a.den > (uintmax_t)INTMAX_MAX || b.den > (uintmax_t)INTMAX_MAX) {
        a = dsdl_rational_halve(a);
        b = dsdl_rational_halve(b);
    }

    // Now denominators are safe to cast. Reduce until multiplication won't overflow.
    // We need: a.num * b.den, b.num * a.den, and a.den * b.den to not overflow
    while (dsdl_would_overflow_mul_signed(a.num, (intmax_t)b.den) ||
           dsdl_would_overflow_mul_signed(b.num, (intmax_t)a.den) || dsdl_would_overflow_mul_unsigned(a.den, b.den)) {
        a = dsdl_rational_halve(a);
        b = dsdl_rational_halve(b);
    }

    const intmax_t ad = a.num * (intmax_t)b.den;
    const intmax_t bc = b.num * (intmax_t)a.den;

    // Check if addition would overflow
    if (dsdl_would_overflow_add_signed(ad, bc)) {
        // Halve and restart
        return dsdl_rational_add(dsdl_rational_halve(a), dsdl_rational_halve(b));
    }

    dsdl_rational_t r;
    r.num = ad + bc;
    r.den = a.den * b.den;
    return dsdl_rational_normalize(r);
}

/// Subtract two rationals: a/b - c/d = (ad - bc) / bd
/// Uses overflow-safe arithmetic with halving approximation.
static dsdl_rational_t dsdl_rational_sub(dsdl_rational_t a, dsdl_rational_t b)
{
    b.num = -b.num;
    return dsdl_rational_add(a, b);
}

/// Multiply two rationals: a/b * c/d = ac / bd
/// Uses overflow-safe arithmetic with halving approximation.
static dsdl_rational_t dsdl_rational_mul(dsdl_rational_t a, dsdl_rational_t b)
{
    // First normalize to reduce magnitude
    a = dsdl_rational_normalize(a);
    b = dsdl_rational_normalize(b);

    // Cross-reduce to minimize overflow risk: GCD(a.num, b.den) and GCD(b.num, a.den)
    const uintmax_t g1 = dsdl_gcd(dsdl_abs(a.num), b.den);
    const uintmax_t g2 = dsdl_gcd(dsdl_abs(b.num), a.den);
    if (g1 > 1) {
        a.num = a.num / (intmax_t)g1;
        b.den = b.den / g1;
    }
    if (g2 > 1) {
        b.num = b.num / (intmax_t)g2;
        a.den = a.den / g2;
    }

    // Reduce operands until multiplication won't overflow
    while (dsdl_would_overflow_mul_signed(a.num, b.num) || dsdl_would_overflow_mul_unsigned(a.den, b.den)) {
        a = dsdl_rational_halve(a);
        b = dsdl_rational_halve(b);
    }

    dsdl_rational_t r;
    r.num = a.num * b.num;
    r.den = a.den * b.den;
    return dsdl_rational_normalize(r);
}

/// Divide two rationals: (a/b) / (c/d) = ad / bc
/// Uses overflow-safe arithmetic with halving approximation.
static dsdl_rational_t dsdl_rational_div(dsdl_rational_t a, dsdl_rational_t b)
{
    if (b.num == 0) {
        dsdl_rational_t r = { 0, 0 }; // NaN
        return r;
    }

    // Create inverse of b: (c/d)^-1 = d/c
    // Handle the case where b.den > INTMAX_MAX by capping at INTMAX_MAX
    dsdl_rational_t b_inv;
    if (b.num < 0) {
        // b.den becomes numerator (with sign flip)
        if (b.den > (uintmax_t)INTMAX_MAX) {
            // Cap at INTMAX_MAX (approximation)
            b_inv.num = -INTMAX_MAX;
            // Scale denominator proportionally: den * (INTMAX_MAX / b.den)
            const uintmax_t abs_num = dsdl_abs(b.num);
            b_inv.den               = (abs_num * (uintmax_t)INTMAX_MAX) / b.den;
            if (b_inv.den == 0) {
                b_inv.den = 1;
            }
        } else {
            b_inv.num = -(intmax_t)b.den;
            b_inv.den = dsdl_abs(b.num);
        }
    } else {
        if (b.den > (uintmax_t)INTMAX_MAX) {
            // Cap at INTMAX_MAX (approximation)
            b_inv.num = INTMAX_MAX;
            // Scale denominator proportionally
            b_inv.den = ((uintmax_t)b.num * (uintmax_t)INTMAX_MAX) / b.den;
            if (b_inv.den == 0) {
                b_inv.den = 1;
            }
        } else {
            b_inv.num = (intmax_t)b.den;
            b_inv.den = (uintmax_t)b.num;
        }
    }
    return dsdl_rational_mul(a, b_inv);
}

/// Negate a rational.
static dsdl_rational_t dsdl_rational_neg(dsdl_rational_t a)
{
    // Handle INTMAX_MIN edge case
    if (a.num == INTMAX_MIN) {
        // Can't negate directly; halve first then negate
        a = dsdl_rational_halve(a);
    }
    a.num = -a.num;
    return a;
}

/// Compare two rationals: returns <0, 0, >0.
/// Uses overflow-safe arithmetic with halving approximation.
static int dsdl_rational_cmp(dsdl_rational_t a, dsdl_rational_t b)
{
    // a/b vs c/d  =>  compare a*d vs c*b
    // First ensure denominators fit in intmax_t
    while (a.den > (uintmax_t)INTMAX_MAX || b.den > (uintmax_t)INTMAX_MAX) {
        a = dsdl_rational_halve(a);
        b = dsdl_rational_halve(b);
    }

    // Now reduce until multiplication won't overflow
    while (dsdl_would_overflow_mul_signed(a.num, (intmax_t)b.den) ||
           dsdl_would_overflow_mul_signed(b.num, (intmax_t)a.den)) {
        a = dsdl_rational_halve(a);
        b = dsdl_rational_halve(b);
    }

    const intmax_t lhs = a.num * (intmax_t)b.den;
    const intmax_t rhs = b.num * (intmax_t)a.den;
    if (lhs < rhs) {
        return -1;
    }
    if (lhs > rhs) {
        return 1;
    }
    return 0;
}

/// Check if rational is an integer (denominator == 1).
static bool dsdl_rational_is_int(dsdl_rational_t r) { return r.den == 1; }

/// Convert a double to a rational approximation.
/// Uses continued fraction expansion for best approximation within intmax_t range.
static dsdl_rational_t dsdl_rational_from_double(double x)
{
    dsdl_rational_t result = { 0, 1 };

    // Handle special cases
    if (!isfinite(x)) {
        result.den = 0; // NaN
        return result;
    }
    if (fabs(x) < 1e-15) {
        return result; // Effectively zero
    }

    // Handle sign
    const bool negative = x < 0;
    if (negative) {
        x = -x;
    }

    // Use continued fraction expansion for best rational approximation
    // Limit iterations to prevent infinite loops
    intmax_t  h0 = 0, h1 = 1; // Numerator convergents
    uintmax_t k0 = 1, k1 = 0; // Denominator convergents

    for (int iter = 0; iter < 64 && x > 1e-15; ++iter) {
        const double   a_d = floor(x);
        const intmax_t a   = (a_d > (double)INTMAX_MAX) ? INTMAX_MAX : (intmax_t)a_d;

        // Check for overflow before computing next convergent
        // h2 = a * h1 + h0, k2 = a * k1 + k0
        if (dsdl_would_overflow_mul_signed(a, h1) || dsdl_would_overflow_mul_unsigned((uintmax_t)a, k1)) {
            break;
        }
        const intmax_t  h2_tmp = a * h1;
        const uintmax_t k2_tmp = (uintmax_t)a * k1;

        if (dsdl_would_overflow_add_signed(h2_tmp, h0) || k2_tmp > UINTMAX_MAX - k0) {
            break;
        }

        const intmax_t  h2 = h2_tmp + h0;
        const uintmax_t k2 = k2_tmp + k0;

        h0 = h1;
        h1 = h2;
        k0 = k1;
        k1 = k2;

        // Compute remainder for next iteration
        const double remainder = x - a_d;
        if (remainder < 1e-15) {
            break;
        }
        x = 1.0 / remainder;
    }

    result.num = negative ? -h1 : h1;
    result.den = (k1 == 0) ? 1 : k1;
    return dsdl_rational_normalize(result);
}

// ============================================================================
// Parser state
// ============================================================================

/// Internal parser state.
typedef struct
{
    const char* input; ///< Input buffer (not NUL-terminated necessarily)
    size_t      len;   ///< Input length in bytes
    size_t      pos;   ///< Current parse position
    size_t      line;  ///< Current line number (1-based)
    size_t      col;   ///< Current column number (1-based)
    dsdl_t*     dsdl;  ///< Parent state for memory allocation and type cache
} dsdl_parser_t;

// ============================================================================
// Memory management helpers
// ============================================================================

static void* dsdl_alloc(dsdl_t* const self, const size_t size)
{
    if (size == 0) {
        return NULL;
    }
    return self->realloc(self, NULL, size);
}

static void dsdl_free(dsdl_t* const self, void* const ptr)
{
    if (ptr != NULL) {
        (void)self->realloc(self, ptr, 0);
    }
}

static void* dsdl_realloc(dsdl_t* const self, void* const ptr, const size_t new_size)
{
    return self->realloc(self, ptr, new_size);
}

// ============================================================================
// Parser foundation
// ============================================================================

/// Initialize parser state.
static void dsdl_parser_init(dsdl_parser_t* const parser, dsdl_t* const dsdl, const char* const input, const size_t len)
{
    parser->input = input;
    parser->len   = len;
    parser->pos   = 0;
    parser->line  = 1;
    parser->col   = 1;
    parser->dsdl  = dsdl;
}

/// Check if parser has reached end of input.
static bool dsdl_parser_eof(const dsdl_parser_t* const parser) { return parser->pos >= parser->len; }

/// Peek at character at current position + offset. Returns 0 if out of bounds.
static char dsdl_parser_peek(const dsdl_parser_t* const parser, const size_t offset)
{
    const size_t idx = parser->pos + offset;
    if (idx >= parser->len) {
        return '\0';
    }
    return parser->input[idx];
}

/// Advance parser by count characters, updating line/col tracking.
static void dsdl_parser_advance(dsdl_parser_t* const parser, const size_t count)
{
    for (size_t i = 0; i < count && parser->pos < parser->len; ++i) {
        const char c = parser->input[parser->pos];
        if (c == '\n') {
            parser->line++;
            parser->col = 1;
        } else {
            parser->col++;
        }
        parser->pos++;
    }
}

/// Check if the string at current position matches the given string.
/// Does NOT advance the parser.
static bool dsdl_parser_match(const dsdl_parser_t* const parser, const char* const str, const size_t str_len)
{
    if ((parser->pos + str_len) > parser->len) {
        return false;
    }
    return memcmp(&parser->input[parser->pos], str, str_len) == 0;
}

/// Check if the string matches and advance if so.
static bool dsdl_parser_accept(dsdl_parser_t* const parser, const char* const str, const size_t str_len)
{
    if (dsdl_parser_match(parser, str, str_len)) {
        dsdl_parser_advance(parser, str_len);
        return true;
    }
    return false;
}

/// Skip whitespace (space and tab only, not newlines).
static void dsdl_parser_skip_ws(dsdl_parser_t* const parser)
{
    while (!dsdl_parser_eof(parser)) {
        const char c = dsdl_parser_peek(parser, 0);
        if ((c == ' ') || (c == '\t')) {
            dsdl_parser_advance(parser, 1);
        } else {
            break;
        }
    }
}

/// Skip a comment (# to end of line). Returns true if a comment was skipped.
static bool dsdl_parser_skip_comment(dsdl_parser_t* const parser)
{
    if (dsdl_parser_peek(parser, 0) != '#') {
        return false;
    }
    // Consume everything until end of line (but not the newline itself)
    while (!dsdl_parser_eof(parser)) {
        const char c = dsdl_parser_peek(parser, 0);
        if ((c == '\r') || (c == '\n')) {
            break;
        }
        dsdl_parser_advance(parser, 1);
    }
    return true;
}

/// Skip optional whitespace and comment at end of line.
static void dsdl_parser_skip_line_tail(dsdl_parser_t* const parser)
{
    dsdl_parser_skip_ws(parser);
    (void)dsdl_parser_skip_comment(parser);
}

/// Skip end of line sequence (\n or \r\n). Returns true if skipped.
static bool dsdl_parser_skip_eol(dsdl_parser_t* const parser)
{
    if (dsdl_parser_peek(parser, 0) == '\r' && dsdl_parser_peek(parser, 1) == '\n') {
        dsdl_parser_advance(parser, 2);
        return true;
    }
    if (dsdl_parser_peek(parser, 0) == '\n') {
        dsdl_parser_advance(parser, 1);
        return true;
    }
    return false;
}

/// Check if character is a digit (0-9).
static bool dsdl_is_digit(const char c) { return (c >= '0') && (c <= '9'); }

/// Check if character is a hex digit (0-9, a-f, A-F).
static bool dsdl_is_hex_digit(const char c)
{
    return dsdl_is_digit(c) || ((c >= 'a') && (c <= 'f')) || ((c >= 'A') && (c <= 'F'));
}

/// Check if character is an octal digit (0-7).
static bool dsdl_is_octal_digit(const char c) { return (c >= '0') && (c <= '7'); }

/// Check if character is a binary digit (0-1).
static bool dsdl_is_binary_digit(const char c) { return (c == '0') || (c == '1'); }

/// Check if character is an identifier start character.
static bool dsdl_is_ident_start(const char c)
{
    return ((c >= 'a') && (c <= 'z')) || ((c >= 'A') && (c <= 'Z')) || (c == '_');
}

/// Check if character is an identifier continuation character.
static bool dsdl_is_ident_cont(const char c) { return dsdl_is_ident_start(c) || dsdl_is_digit(c); }

/// Convert hex digit to value (0-15).
static int dsdl_hex_value(const char c)
{
    if (dsdl_is_digit(c)) {
        return c - '0';
    }
    if ((c >= 'a') && (c <= 'f')) {
        return 10 + (c - 'a');
    }
    if ((c >= 'A') && (c <= 'F')) {
        return 10 + (c - 'A');
    }
    return -1;
}

// ============================================================================
// Literal parsing
// ============================================================================

/// Parse a binary integer literal: 0b[01_]+
/// Returns rational with den=1 on success, den=0 on failure.
static dsdl_rational_t dsdl_parse_int_binary(dsdl_parser_t* const parser)
{
    dsdl_rational_t result = { 0, 0 }; // NaN = failure

    // Check for 0b or 0B prefix
    if (!((dsdl_parser_peek(parser, 0) == '0') &&
          ((dsdl_parser_peek(parser, 1) == 'b') || (dsdl_parser_peek(parser, 1) == 'B')))) {
        return result;
    }
    dsdl_parser_advance(parser, 2);

    // Must have at least one digit
    bool     has_digit = false;
    intmax_t value     = 0;

    while (!dsdl_parser_eof(parser)) {
        const char c = dsdl_parser_peek(parser, 0);
        if (c == '_') {
            // Skip underscores (digit separator)
            dsdl_parser_advance(parser, 1);
            continue;
        }
        if (dsdl_is_binary_digit(c)) {
            has_digit = true;
            value     = (value << 1) | (c - '0');
            dsdl_parser_advance(parser, 1);
        } else {
            break;
        }
    }

    if (has_digit) {
        result.num = value;
        result.den = 1;
    }
    return result;
}

/// Parse an octal integer literal: 0o[0-7_]+
static dsdl_rational_t dsdl_parse_int_octal(dsdl_parser_t* const parser)
{
    dsdl_rational_t result = { 0, 0 }; // NaN = failure

    // Check for 0o or 0O prefix
    if (!((dsdl_parser_peek(parser, 0) == '0') &&
          ((dsdl_parser_peek(parser, 1) == 'o') || (dsdl_parser_peek(parser, 1) == 'O')))) {
        return result;
    }
    dsdl_parser_advance(parser, 2);

    bool     has_digit = false;
    intmax_t value     = 0;

    while (!dsdl_parser_eof(parser)) {
        const char c = dsdl_parser_peek(parser, 0);
        if (c == '_') {
            dsdl_parser_advance(parser, 1);
            continue;
        }
        if (dsdl_is_octal_digit(c)) {
            has_digit = true;
            value     = (value << 3) | (c - '0');
            dsdl_parser_advance(parser, 1);
        } else {
            break;
        }
    }

    if (has_digit) {
        result.num = value;
        result.den = 1;
    }
    return result;
}

/// Parse a hexadecimal integer literal: 0x[0-9a-fA-F_]+
static dsdl_rational_t dsdl_parse_int_hex(dsdl_parser_t* const parser)
{
    dsdl_rational_t result = { 0, 0 }; // NaN = failure

    // Check for 0x or 0X prefix
    if (!((dsdl_parser_peek(parser, 0) == '0') &&
          ((dsdl_parser_peek(parser, 1) == 'x') || (dsdl_parser_peek(parser, 1) == 'X')))) {
        return result;
    }
    dsdl_parser_advance(parser, 2);

    bool     has_digit = false;
    intmax_t value     = 0;

    while (!dsdl_parser_eof(parser)) {
        const char c = dsdl_parser_peek(parser, 0);
        if (c == '_') {
            dsdl_parser_advance(parser, 1);
            continue;
        }
        if (dsdl_is_hex_digit(c)) {
            has_digit = true;
            value     = (value << 4) | dsdl_hex_value(c);
            dsdl_parser_advance(parser, 1);
        } else {
            break;
        }
    }

    if (has_digit) {
        result.num = value;
        result.den = 1;
    }
    return result;
}

/// Parse a decimal integer literal: [0-9_]+
/// Returns rational with den=1 on success, den=0 on failure.
static dsdl_rational_t dsdl_parse_int_decimal(dsdl_parser_t* const parser)
{
    dsdl_rational_t result = { 0, 0 }; // NaN = failure

    // Must start with a digit
    if (!dsdl_is_digit(dsdl_parser_peek(parser, 0))) {
        return result;
    }

    bool     has_digit = false;
    intmax_t value     = 0;

    while (!dsdl_parser_eof(parser)) {
        const char c = dsdl_parser_peek(parser, 0);
        if (c == '_') {
            dsdl_parser_advance(parser, 1);
            continue;
        }
        if (dsdl_is_digit(c)) {
            has_digit = true;
            value     = (value * 10) + (c - '0');
            dsdl_parser_advance(parser, 1);
        } else {
            break;
        }
    }

    if (has_digit) {
        result.num = value;
        result.den = 1;
    }
    return result;
}

/// Parse any integer literal (binary, octal, hex, or decimal).
/// The order matters: we try more specific prefixes first.
static dsdl_rational_t dsdl_parse_integer(dsdl_parser_t* const parser)
{
    const size_t start_pos = parser->pos;

    // Try binary (0b...)
    dsdl_rational_t result = dsdl_parse_int_binary(parser);
    if (result.den != 0) {
        return result;
    }
    parser->pos = start_pos; // Backtrack

    // Try octal (0o...)
    result = dsdl_parse_int_octal(parser);
    if (result.den != 0) {
        return result;
    }
    parser->pos = start_pos;

    // Try hex (0x...)
    result = dsdl_parse_int_hex(parser);
    if (result.den != 0) {
        return result;
    }
    parser->pos = start_pos;

    // Try decimal
    return dsdl_parse_int_decimal(parser);
}

/// Parse digits for real number (returns count of digits parsed, 0 on failure).
static size_t dsdl_parse_real_digits(dsdl_parser_t* const parser, intmax_t* const out_value)
{
    size_t   count = 0;
    intmax_t value = 0;

    while (!dsdl_parser_eof(parser)) {
        const char c = dsdl_parser_peek(parser, 0);
        if (c == '_') {
            dsdl_parser_advance(parser, 1);
            continue;
        }
        if (dsdl_is_digit(c)) {
            count++;
            value = (value * 10) + (c - '0');
            dsdl_parser_advance(parser, 1);
        } else {
            break;
        }
    }

    if (out_value != NULL) {
        *out_value = value;
    }
    return count;
}

/// Parse a real literal (point notation or exponent notation).
/// Returns rational on success, with den=0 on failure.
/// Note: For simplicity, we convert to double and then back to rational for fractional parts.
static dsdl_rational_t dsdl_parse_real(dsdl_parser_t* const parser)
{
    dsdl_rational_t result    = { 0, 0 }; // NaN = failure
    const size_t    start_pos = parser->pos;

    // Components of the real number
    intmax_t int_part   = 0;
    intmax_t frac_part  = 0;
    size_t   frac_count = 0; // Number of fractional digits
    intmax_t exp_value  = 0;
    bool     exp_neg    = false;
    bool     has_point  = false;
    bool     has_exp    = false;

    // Parse integer part (optional if there's a point)
    const size_t int_digits = dsdl_parse_real_digits(parser, &int_part);

    // Check for decimal point
    if (dsdl_parser_peek(parser, 0) == '.') {
        // Make sure the next character is a digit or end-of-token (not another identifier)
        const char next = dsdl_parser_peek(parser, 1);
        if (dsdl_is_digit(next)) {
            has_point = true;
            dsdl_parser_advance(parser, 1); // Consume '.'

            // Parse fractional part
            frac_count = dsdl_parse_real_digits(parser, &frac_part);
        } else if (!dsdl_is_ident_start(next)) {
            // Just "123." is valid
            has_point = true;
            dsdl_parser_advance(parser, 1);
        }
    }

    // Check for exponent
    const char exp_char = dsdl_parser_peek(parser, 0);
    if ((exp_char == 'e') || (exp_char == 'E')) {
        has_exp = true;
        dsdl_parser_advance(parser, 1);

        // Optional sign
        const char sign = dsdl_parser_peek(parser, 0);
        if (sign == '+') {
            dsdl_parser_advance(parser, 1);
        } else if (sign == '-') {
            exp_neg = true;
            dsdl_parser_advance(parser, 1);
        }

        // Exponent digits (required)
        if (dsdl_parse_real_digits(parser, &exp_value) == 0) {
            // Invalid: e without digits
            parser->pos = start_pos;
            return result;
        }
    }

    // A real literal must have either a decimal point or an exponent
    if (!has_point && !has_exp) {
        parser->pos = start_pos;
        return result;
    }

    // Also need at least some digits
    if ((int_digits == 0) && (frac_count == 0)) {
        parser->pos = start_pos;
        return result;
    }

    // Build the rational.
    // Value = (int_part + frac_part / 10^frac_count) * 10^(±exp_value)
    // To keep it as a rational: num = int_part * 10^frac_count + frac_part
    //                          den = 10^frac_count
    // Then multiply/divide by 10^exp as needed.

    // Start with integer and fractional part
    // Compute frac_denom = 10^frac_count, with overflow check
    uintmax_t frac_denom = 1;
    for (size_t i = 0; i < frac_count; ++i) {
        if (frac_denom > UINTMAX_MAX / 10) {
            // Would overflow - truncate precision
            break;
        }
        frac_denom *= 10;
    }

    // Ensure frac_denom fits in intmax_t for safe signed arithmetic
    while (frac_denom > (uintmax_t)INTMAX_MAX) {
        frac_denom /= 2;
        int_part /= 2;
        frac_part /= 2;
    }

    // Check for multiplication overflow: int_part * frac_denom
    while (dsdl_would_overflow_mul_signed(int_part, (intmax_t)frac_denom)) {
        frac_denom /= 2;
        int_part /= 2;
        frac_part /= 2;
    }

    result.num = int_part * (intmax_t)frac_denom + frac_part;
    result.den = frac_denom;

    // Apply exponent with overflow protection
    if (has_exp) {
        for (intmax_t i = 0; i < exp_value; ++i) {
            if (exp_neg) {
                if (result.den > UINTMAX_MAX / 10) {
                    // Would overflow denominator - halve the rational
                    result = dsdl_rational_halve(result);
                }
                result.den *= 10;
            } else {
                // Check signed overflow for numerator
                if ((result.num > 0 && result.num > INTMAX_MAX / 10) ||
                    (result.num < 0 && result.num < INTMAX_MIN / 10)) {
                    result = dsdl_rational_halve(result);
                }
                result.num *= 10;
            }
        }
    }

    return dsdl_rational_normalize(result);
}

/// Parse a numeric literal (integer or real).
/// Tries real first (more specific), then integer.
static dsdl_rational_t dsdl_parse_number(dsdl_parser_t* const parser)
{
    const size_t start_pos = parser->pos;

    // Try real first (point or exponent notation)
    dsdl_rational_t result = dsdl_parse_real(parser);
    if (result.den != 0) {
        return result;
    }
    parser->pos = start_pos;

    // Fall back to integer
    return dsdl_parse_integer(parser);
}

/// Parse an identifier and return it as a borrowed string.
/// Returns empty string on failure.
static wkv_str_t dsdl_parse_identifier(dsdl_parser_t* const parser)
{
    wkv_str_t result = { 0, NULL };

    if (!dsdl_is_ident_start(dsdl_parser_peek(parser, 0))) {
        return result;
    }

    const size_t start = parser->pos;
    dsdl_parser_advance(parser, 1);

    while (dsdl_is_ident_cont(dsdl_parser_peek(parser, 0))) {
        dsdl_parser_advance(parser, 1);
    }

    result.len = parser->pos - start;
    result.str = &parser->input[start];
    return result;
}

/// Parse a boolean literal (true/false).
/// Returns value in *out_value, returns true on success.
static bool dsdl_parse_boolean(dsdl_parser_t* const parser, bool* const out_value)
{
    if (dsdl_parser_accept(parser, "true", 4)) {
        // Make sure it's not followed by identifier characters (e.g., "trueX")
        if (!dsdl_is_ident_cont(dsdl_parser_peek(parser, 0))) {
            *out_value = true;
            return true;
        }
        // Backtrack
        parser->pos -= 4;
    } else if (dsdl_parser_accept(parser, "false", 5)) {
        if (!dsdl_is_ident_cont(dsdl_parser_peek(parser, 0))) {
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
static wkv_str_t dsdl_parse_string(dsdl_parser_t* const parser)
{
    wkv_str_t  result    = { 0, NULL };
    const char quote     = dsdl_parser_peek(parser, 0);
    const bool is_single = (quote == '\'');
    const bool is_double = (quote == '"');

    if (!is_single && !is_double) {
        return result;
    }

    dsdl_parser_advance(parser, 1); // Consume opening quote
    const size_t start = parser->pos;

    while (!dsdl_parser_eof(parser)) {
        const char c = dsdl_parser_peek(parser, 0);

        if (c == quote) {
            // End of string
            result.len = parser->pos - start;
            result.str = &parser->input[start];
            dsdl_parser_advance(parser, 1); // Consume closing quote
            return result;
        }

        if ((c == '\r') || (c == '\n')) {
            // Unterminated string (newline in string)
            break;
        }

        if (c == '\\') {
            // Escape sequence - skip both backslash and next character
            dsdl_parser_advance(parser, 2);
        } else {
            dsdl_parser_advance(parser, 1);
        }
    }

    // Unterminated string - return failure
    return result;
}

// Forward declaration for recursive expression parsing
static bool dsdl_parse_expression(dsdl_parser_t* parser, dsdl_value_t* out_value);

/// Parse a set literal: { expr, expr, ... }
/// Returns true on success, fills out_value with dsdl_value_set.
/// Uses dynamic allocation for arbitrary-sized sets.
static bool dsdl_parse_set(dsdl_parser_t* const parser, dsdl_value_t* const out_value)
{
    if (dsdl_parser_peek(parser, 0) != '{') {
        return false;
    }
    dsdl_parser_advance(parser, 1); // Consume '{'
    dsdl_parser_skip_ws(parser);

    // Initialize as empty set
    out_value->kind            = dsdl_value_set;
    out_value->as.set.count    = 0;
    out_value->as.set.elements = NULL;

    // Check for empty set
    if (dsdl_parser_peek(parser, 0) == '}') {
        dsdl_parser_advance(parser, 1);
        return true;
    }

    // Dynamic allocation with growth strategy
    size_t        capacity = 8; // Initial capacity
    size_t        count    = 0;
    dsdl_value_t* elements = (dsdl_value_t*)dsdl_alloc(parser->dsdl, capacity * sizeof(dsdl_value_t));
    if (elements == NULL) {
        return false; // OOM
    }

    for (;;) {
        // Parse expression
        dsdl_value_t elem;
        if (!dsdl_parse_expression(parser, &elem)) {
            dsdl_free(parser->dsdl, elements);
            return false; // Parse error
        }

        // Grow buffer if needed
        if (count >= capacity) {
            const size_t        new_capacity = capacity * 2;
            dsdl_value_t* const new_elements =
              (dsdl_value_t*)dsdl_realloc(parser->dsdl, elements, new_capacity * sizeof(dsdl_value_t));
            if (new_elements == NULL) {
                dsdl_free(parser->dsdl, elements);
                return false; // OOM
            }
            elements = new_elements;
            capacity = new_capacity;
        }

        elements[count++] = elem;
        dsdl_parser_skip_ws(parser);

        // Check for comma or closing brace
        if (dsdl_parser_peek(parser, 0) == ',') {
            dsdl_parser_advance(parser, 1);
            dsdl_parser_skip_ws(parser);
        } else if (dsdl_parser_peek(parser, 0) == '}') {
            break;
        } else {
            dsdl_free(parser->dsdl, elements);
            return false; // Unexpected character
        }
    }

    // Consume closing brace
    if (dsdl_parser_peek(parser, 0) != '}') {
        dsdl_free(parser->dsdl, elements);
        return false;
    }
    dsdl_parser_advance(parser, 1);

    // Shrink to fit if significantly oversized (optional optimization)
    if ((count > 0) && (count < capacity / 2) && (capacity > 8)) {
        dsdl_value_t* const shrunk = (dsdl_value_t*)dsdl_realloc(parser->dsdl, elements, count * sizeof(dsdl_value_t));
        if (shrunk != NULL) {
            elements = shrunk;
        }
        // If shrink fails, keep the larger buffer - not a critical error
    }

    out_value->as.set.elements = elements;
    out_value->as.set.count    = count;

    return true;
}

/// Parse a literal (number, string, boolean, or set).
static bool dsdl_parse_literal(dsdl_parser_t* const parser, dsdl_value_t* const out_value)
{
    // Try set first (starts with '{')
    if (dsdl_parser_peek(parser, 0) == '{') {
        return dsdl_parse_set(parser, out_value);
    }

    // Try string (starts with quote)
    const char c = dsdl_parser_peek(parser, 0);
    if ((c == '"') || (c == '\'')) {
        wkv_str_t str = dsdl_parse_string(parser);
        if (str.str != NULL) {
            out_value->kind      = dsdl_value_string;
            out_value->as.string = str;
            return true;
        }
        return false;
    }

    // Try boolean
    bool bool_val;
    if (dsdl_parse_boolean(parser, &bool_val)) {
        out_value->kind       = dsdl_value_bool;
        out_value->as.boolean = bool_val;
        return true;
    }

    // Try number (integer or real)
    dsdl_rational_t num = dsdl_parse_number(parser);
    if (num.den != 0) {
        out_value->kind        = dsdl_value_rational;
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
    dsdl_prec_none       = 0,
    dsdl_prec_logical    = 1, // || &&
    dsdl_prec_comparison = 2, // == != < <= > >=
    dsdl_prec_bitwise    = 3, // | ^ &
    dsdl_prec_additive   = 4, // + -
    dsdl_prec_mult       = 5, // * / %
    dsdl_prec_exp        = 6, // **
    dsdl_prec_attribute  = 7, // .
} dsdl_prec_t;

/// Binary operator types
typedef enum
{
    dsdl_op_none,
    // Logical
    dsdl_op_or,
    dsdl_op_and,
    // Comparison
    dsdl_op_eq,
    dsdl_op_ne,
    dsdl_op_lt,
    dsdl_op_le,
    dsdl_op_gt,
    dsdl_op_ge,
    // Bitwise
    dsdl_op_bit_or,
    dsdl_op_bit_xor,
    dsdl_op_bit_and,
    // Additive
    dsdl_op_add,
    dsdl_op_sub,
    // Multiplicative
    dsdl_op_mul,
    dsdl_op_div,
    dsdl_op_mod,
    // Exponential
    dsdl_op_pow,
    // Attribute
    dsdl_op_dot,
} dsdl_op_t;

/// Get precedence for a binary operator at current position.
/// Also returns the operator type and its length.
static dsdl_prec_t dsdl_get_binary_op(const dsdl_parser_t* const parser, dsdl_op_t* const out_op, size_t* const out_len)
{
    *out_op  = dsdl_op_none;
    *out_len = 0;

    const char c0 = dsdl_parser_peek(parser, 0);
    const char c1 = dsdl_parser_peek(parser, 1);

    // Two-character operators first
    if (c0 == '|' && c1 == '|') {
        *out_op  = dsdl_op_or;
        *out_len = 2;
        return dsdl_prec_logical;
    }
    if (c0 == '&' && c1 == '&') {
        *out_op  = dsdl_op_and;
        *out_len = 2;
        return dsdl_prec_logical;
    }
    if (c0 == '=' && c1 == '=') {
        *out_op  = dsdl_op_eq;
        *out_len = 2;
        return dsdl_prec_comparison;
    }
    if (c0 == '!' && c1 == '=') {
        *out_op  = dsdl_op_ne;
        *out_len = 2;
        return dsdl_prec_comparison;
    }
    if (c0 == '<' && c1 == '=') {
        *out_op  = dsdl_op_le;
        *out_len = 2;
        return dsdl_prec_comparison;
    }
    if (c0 == '>' && c1 == '=') {
        *out_op  = dsdl_op_ge;
        *out_len = 2;
        return dsdl_prec_comparison;
    }
    if (c0 == '*' && c1 == '*') {
        *out_op  = dsdl_op_pow;
        *out_len = 2;
        return dsdl_prec_exp;
    }

    // Single-character operators
    if (c0 == '<') {
        *out_op  = dsdl_op_lt;
        *out_len = 1;
        return dsdl_prec_comparison;
    }
    if (c0 == '>') {
        *out_op  = dsdl_op_gt;
        *out_len = 1;
        return dsdl_prec_comparison;
    }
    if (c0 == '|') {
        *out_op  = dsdl_op_bit_or;
        *out_len = 1;
        return dsdl_prec_bitwise;
    }
    if (c0 == '^') {
        *out_op  = dsdl_op_bit_xor;
        *out_len = 1;
        return dsdl_prec_bitwise;
    }
    if (c0 == '&') {
        *out_op  = dsdl_op_bit_and;
        *out_len = 1;
        return dsdl_prec_bitwise;
    }
    if (c0 == '+') {
        *out_op  = dsdl_op_add;
        *out_len = 1;
        return dsdl_prec_additive;
    }
    if (c0 == '-') {
        *out_op  = dsdl_op_sub;
        *out_len = 1;
        return dsdl_prec_additive;
    }
    if (c0 == '*') {
        *out_op  = dsdl_op_mul;
        *out_len = 1;
        return dsdl_prec_mult;
    }
    if (c0 == '/') {
        *out_op  = dsdl_op_div;
        *out_len = 1;
        return dsdl_prec_mult;
    }
    if (c0 == '%') {
        *out_op  = dsdl_op_mod;
        *out_len = 1;
        return dsdl_prec_mult;
    }
    if (c0 == '.') {
        *out_op  = dsdl_op_dot;
        *out_len = 1;
        return dsdl_prec_attribute;
    }

    return dsdl_prec_none;
}

/// Apply a binary operation to two values.
static bool dsdl_apply_binary_op(const dsdl_op_t           op,
                                 const dsdl_value_t* const left,
                                 const dsdl_value_t* const right,
                                 dsdl_value_t* const       result)
{
    // Rational arithmetic operations
    if ((left->kind == dsdl_value_rational) && (right->kind == dsdl_value_rational)) {
        const dsdl_rational_t a = left->as.rational;
        const dsdl_rational_t b = right->as.rational;

        // Arithmetic operators
        if (op == dsdl_op_add) {
            result->kind        = dsdl_value_rational;
            result->as.rational = dsdl_rational_add(a, b);
            return true;
        }
        if (op == dsdl_op_sub) {
            result->kind        = dsdl_value_rational;
            result->as.rational = dsdl_rational_sub(a, b);
            return true;
        }
        if (op == dsdl_op_mul) {
            result->kind        = dsdl_value_rational;
            result->as.rational = dsdl_rational_mul(a, b);
            return true;
        }
        if (op == dsdl_op_div) {
            result->kind        = dsdl_value_rational;
            result->as.rational = dsdl_rational_div(a, b);
            return true;
        }
        if (op == dsdl_op_mod) {
            // Modulo only defined for integers
            if (dsdl_rational_is_int(a) && dsdl_rational_is_int(b) && (b.num != 0)) {
                result->kind        = dsdl_value_rational;
                result->as.rational = dsdl_rational_from_int(a.num % b.num);
                return true;
            }
            return false;
        }
        if (op == dsdl_op_pow) {
            result->kind = dsdl_value_rational;
            if (dsdl_rational_is_int(b)) {
                // Integer exponent: exact rational arithmetic
                intmax_t exp            = b.num;
                result->as.rational.num = 1;
                result->as.rational.den = 1;
                if (exp < 0) {
                    // Negative exponent: invert base
                    // First ensure denominator fits in intmax_t
                    dsdl_rational_t base = a;
                    while (base.den > (uintmax_t)INTMAX_MAX) {
                        base = dsdl_rational_halve(base);
                    }
                    dsdl_rational_t inv = { (intmax_t)base.den, dsdl_abs(base.num) };
                    if ((base.num < 0) && (((-exp) % 2) == 1)) {
                        inv.num = -inv.num;
                    }
                    exp = -exp;
                    for (intmax_t i = 0; i < exp; ++i) {
                        result->as.rational = dsdl_rational_mul(result->as.rational, inv);
                    }
                } else {
                    for (intmax_t i = 0; i < exp; ++i) {
                        result->as.rational = dsdl_rational_mul(result->as.rational, a);
                    }
                }
            } else {
                // Non-integer exponent: use floating-point approximation
                // Per DSDL spec, accuracy is implementation-defined
                const double base_d   = (double)a.num / (double)a.den;
                const double exp_d    = (double)b.num / (double)b.den;
                const double result_d = pow(base_d, exp_d);
                result->as.rational   = dsdl_rational_from_double(result_d);
                if (result->as.rational.den == 0) {
                    return false; // NaN result (e.g., negative base with fractional exponent)
                }
            }
            return true;
        }

        // Bitwise operators (integers only)
        if ((op == dsdl_op_bit_or) || (op == dsdl_op_bit_xor) || (op == dsdl_op_bit_and)) {
            if (dsdl_rational_is_int(a) && dsdl_rational_is_int(b)) {
                intmax_t r = 0;
                if (op == dsdl_op_bit_or) {
                    r = a.num | b.num;
                } else if (op == dsdl_op_bit_xor) {
                    r = a.num ^ b.num;
                } else // dsdl_op_bit_and
                {
                    r = a.num & b.num;
                }
                result->kind        = dsdl_value_rational;
                result->as.rational = dsdl_rational_from_int(r);
                return true;
            }
            return false;
        }

        // Comparison operators
        const int cmp = dsdl_rational_cmp(a, b);
        result->kind  = dsdl_value_bool;
        if (op == dsdl_op_eq) {
            result->as.boolean = (cmp == 0);
            return true;
        }
        if (op == dsdl_op_ne) {
            result->as.boolean = (cmp != 0);
            return true;
        }
        if (op == dsdl_op_lt) {
            result->as.boolean = (cmp < 0);
            return true;
        }
        if (op == dsdl_op_le) {
            result->as.boolean = (cmp <= 0);
            return true;
        }
        if (op == dsdl_op_gt) {
            result->as.boolean = (cmp > 0);
            return true;
        }
        if (op == dsdl_op_ge) {
            result->as.boolean = (cmp >= 0);
            return true;
        }
    }

    // Boolean operations
    if ((left->kind == dsdl_value_bool) && (right->kind == dsdl_value_bool)) {
        result->kind = dsdl_value_bool;
        if (op == dsdl_op_or) {
            result->as.boolean = left->as.boolean || right->as.boolean;
            return true;
        }
        if (op == dsdl_op_and) {
            result->as.boolean = left->as.boolean && right->as.boolean;
            return true;
        }
        if (op == dsdl_op_eq) {
            result->as.boolean = (left->as.boolean == right->as.boolean);
            return true;
        }
        if (op == dsdl_op_ne) {
            result->as.boolean = (left->as.boolean != right->as.boolean);
            return true;
        }
    }

    return false; // Unsupported operation or type mismatch
}

// Forward declaration for expression parsing
static bool dsdl_parse_expr_prec(dsdl_parser_t* parser, dsdl_prec_t min_prec, dsdl_value_t* out_value);

/// Parse an atom (literal, identifier, or parenthesized expression).
static bool dsdl_parse_atom(dsdl_parser_t* const parser, dsdl_value_t* const out_value)
{
    dsdl_parser_skip_ws(parser);

    // Parenthesized expression
    if (dsdl_parser_peek(parser, 0) == '(') {
        dsdl_parser_advance(parser, 1);
        dsdl_parser_skip_ws(parser);

        if (!dsdl_parse_expression(parser, out_value)) {
            return false;
        }

        dsdl_parser_skip_ws(parser);
        if (dsdl_parser_peek(parser, 0) != ')') {
            return false; // Missing closing paren
        }
        dsdl_parser_advance(parser, 1);
        return true;
    }

    // Try literal first
    if (dsdl_parse_literal(parser, out_value)) {
        return true;
    }

    // Try identifier (could be a constant name or type reference)
    wkv_str_t ident = dsdl_parse_identifier(parser);
    if (ident.str != NULL) {
        // For now, just store as a string (will be resolved during semantic analysis)
        out_value->kind      = dsdl_value_string;
        out_value->as.string = ident;
        return true;
    }

    return false;
}

/// Parse unary prefix operators (!, +, -).
static bool dsdl_parse_unary(dsdl_parser_t* const parser, dsdl_value_t* const out_value)
{
    dsdl_parser_skip_ws(parser);

    const char c = dsdl_parser_peek(parser, 0);

    // Logical NOT
    if (c == '!') {
        dsdl_parser_advance(parser, 1);
        dsdl_parser_skip_ws(parser);

        dsdl_value_t operand;
        if (!dsdl_parse_unary(parser, &operand)) {
            return false;
        }

        if (operand.kind != dsdl_value_bool) {
            return false; // NOT only valid for booleans
        }

        out_value->kind       = dsdl_value_bool;
        out_value->as.boolean = !operand.as.boolean;
        return true;
    }

    // Unary plus
    if (c == '+') {
        dsdl_parser_advance(parser, 1);
        dsdl_parser_skip_ws(parser);

        // Parse the operand at exponential precedence (just above unary)
        return dsdl_parse_expr_prec(parser, dsdl_prec_exp, out_value);
    }

    // Unary minus
    if (c == '-') {
        dsdl_parser_advance(parser, 1);
        dsdl_parser_skip_ws(parser);

        dsdl_value_t operand;
        if (!dsdl_parse_expr_prec(parser, dsdl_prec_exp, &operand)) {
            return false;
        }

        if (operand.kind != dsdl_value_rational) {
            return false; // Negation only valid for numbers
        }

        out_value->kind        = dsdl_value_rational;
        out_value->as.rational = dsdl_rational_neg(operand.as.rational);
        return true;
    }

    // No unary operator, parse atom
    return dsdl_parse_atom(parser, out_value);
}

/// Parse expression with precedence climbing.
static bool dsdl_parse_expr_prec(dsdl_parser_t* const parser, const dsdl_prec_t min_prec, dsdl_value_t* const out_value)
{
    // Parse left-hand side (unary or atom)
    if (!dsdl_parse_unary(parser, out_value)) {
        return false;
    }

    while (true) {
        dsdl_parser_skip_ws(parser);

        // Check for binary operator
        dsdl_op_t         op;
        size_t            op_len;
        const dsdl_prec_t prec = dsdl_get_binary_op(parser, &op, &op_len);

        if (prec == dsdl_prec_none || prec < min_prec) {
            break;
        }

        // Consume operator
        dsdl_parser_advance(parser, op_len);
        dsdl_parser_skip_ws(parser);

        // Handle attribute access (.) specially
        if (op == dsdl_op_dot) {
            wkv_str_t attr = dsdl_parse_identifier(parser);
            if (attr.str == NULL) {
                return false;
            }
            // TODO: Implement attribute access (e.g., _offset_.min)
            // For now, we'll just ignore attribute access
            continue;
        }

        // Parse right-hand side with higher precedence (for left-associativity)
        // For right-associative ** we would use same precedence
        dsdl_prec_t next_prec = (dsdl_prec_t)(prec + 1);
        if (op == dsdl_op_pow) {
            next_prec = prec; // Right-associative
        }

        dsdl_value_t right;
        if (!dsdl_parse_expr_prec(parser, next_prec, &right)) {
            return false;
        }

        // Apply operator
        dsdl_value_t result;
        if (!dsdl_apply_binary_op(op, out_value, &right, &result)) {
            return false;
        }
        *out_value = result;
    }

    return true;
}

/// Parse an expression (top-level entry point).
static bool dsdl_parse_expression(dsdl_parser_t* const parser, dsdl_value_t* const out_value)
{
    return dsdl_parse_expr_prec(parser, dsdl_prec_logical, out_value);
}

// ============================================================================
// Type parsing
// ============================================================================

/// Parsed type representation during parsing.
/// This is an intermediate representation before converting to dsdl_type_t.
typedef struct
{
    dsdl_type_t kind;          ///< DSDL_xxx type kind constant (DSDL_ARRAY_* for arrays)
    dsdl_type_t element_kind;  ///< For arrays: the element type kind (primitive or composite marker)
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
static uint8_t dsdl_parse_bit_length(dsdl_parser_t* const parser)
{
    if (!dsdl_is_digit(dsdl_parser_peek(parser, 0))) {
        return 0;
    }
    // First digit must be 1-9 (no leading zeros allowed for non-zero numbers)
    if (dsdl_parser_peek(parser, 0) == '0') {
        return 0; // Bit length can't start with 0
    }

    size_t value = 0;
    while (dsdl_is_digit(dsdl_parser_peek(parser, 0))) {
        value = value * 10 + (size_t)(dsdl_parser_peek(parser, 0) - '0');
        dsdl_parser_advance(parser, 1);
        if (value > 64) {
            return 0; // Overflow - bit length too large
        }
    }

    return (uint8_t)value;
}

/// Parse a void type: void[1-64]
static bool dsdl_parse_type_void(dsdl_parser_t* const parser, dsdl_parsed_type_t* const out_type)
{
    if (!dsdl_parser_accept(parser, "void", 4)) {
        return false;
    }

    const uint8_t bits = dsdl_parse_bit_length(parser);
    if ((bits < 1) || (bits > 64)) {
        return false;
    }

    out_type->kind      = DSDL_VOID(bits);
    out_type->bit_width = bits;
    return true;
}

/// Parse a primitive type name (uint, int, float) with bit width.
static bool dsdl_parse_primitive_name(dsdl_parser_t* const parser, dsdl_parsed_type_t* const out_type)
{
    if (dsdl_parser_accept(parser, "uint", 4)) {
        const uint8_t bits = dsdl_parse_bit_length(parser);
        if ((bits < 1) || (bits > 64)) {
            return false;
        }
        out_type->kind      = DSDL_UINT(bits);
        out_type->bit_width = bits;
        return true;
    }

    if (dsdl_parser_accept(parser, "int", 3)) {
        const uint8_t bits = dsdl_parse_bit_length(parser);
        if ((bits < 2) || (bits > 64)) {
            return false; // int requires at least 2 bits
        }
        out_type->kind      = DSDL_INT(bits);
        out_type->bit_width = bits;
        return true;
    }

    if (dsdl_parser_accept(parser, "float", 5)) {
        const uint8_t bits = dsdl_parse_bit_length(parser);
        if ((bits != 16) && (bits != 32) && (bits != 64)) {
            return false; // Only float16, float32, float64
        }
        out_type->kind      = DSDL_FLOAT(bits);
        out_type->bit_width = bits;
        return true;
    }

    return false;
}

/// Parse a primitive type (bool, byte, utf8, or [saturated/truncated] primitive_name).
static bool dsdl_parse_type_primitive(dsdl_parser_t* const parser, dsdl_parsed_type_t* const out_type)
{
    out_type->is_saturated = true; // Default

    // Check for "bool"
    if (dsdl_parser_match(parser, "bool", 4) && !dsdl_is_ident_cont(dsdl_parser_peek(parser, 4))) {
        dsdl_parser_advance(parser, 4);
        out_type->kind      = DSDL_BOOL;
        out_type->bit_width = 1;
        return true;
    }

    // Check for "byte"
    if (dsdl_parser_match(parser, "byte", 4) && !dsdl_is_ident_cont(dsdl_parser_peek(parser, 4))) {
        dsdl_parser_advance(parser, 4);
        out_type->kind      = DSDL_BYTE;
        out_type->bit_width = 8;
        return true;
    }

    // Check for "utf8" (alias for uint8)
    if (dsdl_parser_match(parser, "utf8", 4) && !dsdl_is_ident_cont(dsdl_parser_peek(parser, 4))) {
        dsdl_parser_advance(parser, 4);
        out_type->kind      = DSDL_UINT(8); // utf8 is alias for uint8
        out_type->bit_width = 8;
        return true;
    }

    // Check for "truncated" modifier
    if (dsdl_parser_match(parser, "truncated", 9) && !dsdl_is_ident_cont(dsdl_parser_peek(parser, 9))) {
        dsdl_parser_advance(parser, 9);
        dsdl_parser_skip_ws(parser);
        out_type->is_saturated = false;
        return dsdl_parse_primitive_name(parser, out_type);
    }

    // Check for optional "saturated" modifier
    if (dsdl_parser_match(parser, "saturated", 9) && !dsdl_is_ident_cont(dsdl_parser_peek(parser, 9))) {
        dsdl_parser_advance(parser, 9);
        dsdl_parser_skip_ws(parser);
    }

    return dsdl_parse_primitive_name(parser, out_type);
}

/// Parse a versioned type reference: namespace.Name.major.minor
static bool dsdl_parse_type_versioned(dsdl_parser_t* const parser, dsdl_parsed_type_t* const out_type)
{
    const size_t start_pos = parser->pos;

    // Must start with identifier
    wkv_str_t first = dsdl_parse_identifier(parser);
    if (first.str == NULL) {
        return false;
    }

    // Continue parsing namespace parts (identifier.identifier...)
    while (dsdl_parser_peek(parser, 0) == '.') {
        dsdl_parser_advance(parser, 1); // Skip '.'

        // Check if next part is an identifier or version number
        if (!dsdl_is_ident_start(dsdl_parser_peek(parser, 0))) {
            // Should be version specifier now
            break;
        }

        wkv_str_t part = dsdl_parse_identifier(parser);
        if (part.str == NULL) {
            parser->pos = start_pos;
            return false;
        }
    }

    // Now we should have type name (without version).
    // The last '.' was consumed, so now parse version: major.minor
    const size_t type_name_end = parser->pos - 1; // Before the last '.'

    // Parse major version
    dsdl_rational_t major_r = dsdl_parse_int_decimal(parser);
    if ((major_r.den == 0) || !dsdl_rational_is_int(major_r)) {
        parser->pos = start_pos;
        return false;
    }

    // Expect '.'
    if (dsdl_parser_peek(parser, 0) != '.') {
        parser->pos = start_pos;
        return false;
    }
    dsdl_parser_advance(parser, 1);

    // Parse minor version
    dsdl_rational_t minor_r = dsdl_parse_int_decimal(parser);
    if ((minor_r.den == 0) || !dsdl_rational_is_int(minor_r)) {
        parser->pos = start_pos;
        return false;
    }

    // Verify versions are valid (0-255)
    if ((major_r.num < 0) || (major_r.num > 255) || (minor_r.num < 0) || (minor_r.num > 255)) {
        parser->pos = start_pos;
        return false;
    }

    // Success - fill out type
    out_type->kind          = DSDL_COMPOSITE_STRUCT; // Placeholder until we resolve
    out_type->type_name.str = first.str;
    out_type->type_name.len = type_name_end - start_pos;
    out_type->version_major = (uint8_t)major_r.num;
    out_type->version_minor = (uint8_t)minor_r.num;

    return true;
}

/// Parse a scalar type (primitive, void, or versioned composite).
static bool dsdl_parse_type_scalar(dsdl_parser_t* const parser, dsdl_parsed_type_t* const out_type)
{
    const size_t start_pos = parser->pos;

    // Try void type first
    if (dsdl_parse_type_void(parser, out_type)) {
        return true;
    }
    parser->pos = start_pos;

    // Try primitive type
    if (dsdl_parse_type_primitive(parser, out_type)) {
        return true;
    }
    parser->pos = start_pos;

    // Try versioned composite type
    return dsdl_parse_type_versioned(parser, out_type);
}

/// Parse an array type: scalar_type [ expression ] or scalar_type [ <= expression ] etc.
static bool dsdl_parse_type_array(dsdl_parser_t* const parser, dsdl_parsed_type_t* const out_type)
{
    // First parse the element type (scalar)
    if (!dsdl_parse_type_scalar(parser, out_type)) {
        return false;
    }

    dsdl_parser_skip_ws(parser);

    // Check for array brackets
    if (dsdl_parser_peek(parser, 0) != '[') {
        return true; // Not an array, just a scalar - success
    }
    dsdl_parser_advance(parser, 1); // Skip '['
    dsdl_parser_skip_ws(parser);

    // Check for variable array indicators
    out_type->is_variable  = false;
    out_type->is_inclusive = false;

    if (dsdl_parser_accept(parser, "<=", 2)) {
        out_type->is_variable  = true;
        out_type->is_inclusive = true;
        dsdl_parser_skip_ws(parser);
    } else if (dsdl_parser_accept(parser, "<", 1)) {
        out_type->is_variable  = true;
        out_type->is_inclusive = false;
        dsdl_parser_skip_ws(parser);
    }

    // Parse array size expression
    dsdl_value_t size_val;
    if (!dsdl_parse_expression(parser, &size_val)) {
        return false;
    }

    // Size must be a positive integer
    if ((size_val.kind != dsdl_value_rational) || !dsdl_rational_is_int(size_val.as.rational)) {
        return false;
    }
    if (size_val.as.rational.num <= 0) {
        return false;
    }

    out_type->array_size = (size_t)size_val.as.rational.num;

    dsdl_parser_skip_ws(parser);

    // Expect closing bracket
    if (dsdl_parser_peek(parser, 0) != ']') {
        return false;
    }
    dsdl_parser_advance(parser, 1);

    // Save element type and update kind to array marker
    out_type->element_kind = out_type->kind;
    if (out_type->is_variable) {
        out_type->kind = DSDL_ARRAY_VARIABLE;
    } else {
        out_type->kind = DSDL_ARRAY_FIXED;
    }

    return true;
}

/// Parse any type (the main entry point for type parsing).
static bool dsdl_parse_type(dsdl_parser_t* const parser, dsdl_parsed_type_t* const out_type)
{
    (void)memset(out_type, 0, sizeof(*out_type));
    return dsdl_parse_type_array(parser, out_type);
}

// ============================================================================
// Statement parsing
// ============================================================================

/// Statement kind enumeration.
typedef enum
{
    dsdl_stmt_none,
    dsdl_stmt_constant,       ///< type name = expression
    dsdl_stmt_field,          ///< type name
    dsdl_stmt_padding,        ///< void type (no name)
    dsdl_stmt_directive,      ///< @directive [expression]
    dsdl_stmt_service_marker, ///< ---
} dsdl_stmt_kind_t;

/// Parsed statement representation.
typedef struct
{
    dsdl_stmt_kind_t   kind;
    dsdl_parsed_type_t type;      ///< Type for CONSTANT, FIELD, PADDING
    wkv_str_t          name;      ///< Name for CONSTANT, FIELD, DIRECTIVE
    dsdl_value_t       value;     ///< Value for CONSTANT, optional for DIRECTIVE
    bool               has_value; ///< True if value is set (for DIRECTIVE)
} dsdl_parsed_stmt_t;

/// Parse a directive: @name [expression]
static bool dsdl_parse_directive(dsdl_parser_t* const parser, dsdl_parsed_stmt_t* const out_stmt)
{
    if (dsdl_parser_peek(parser, 0) != '@') {
        return false;
    }
    dsdl_parser_advance(parser, 1); // Skip '@'

    wkv_str_t name = dsdl_parse_identifier(parser);
    if (name.str == NULL) {
        return false;
    }

    out_stmt->kind      = dsdl_stmt_directive;
    out_stmt->name      = name;
    out_stmt->has_value = false;

    // Check for optional expression (requires whitespace separator)
    dsdl_parser_skip_ws(parser);

    // Try to parse expression (may fail if no expression follows)
    const size_t start_pos = parser->pos;
    if (dsdl_parse_expression(parser, &out_stmt->value)) {
        out_stmt->has_value = true;
    } else {
        parser->pos = start_pos; // Backtrack
    }

    return true;
}

/// Parse service response marker: ---+
static bool dsdl_parse_service_marker(dsdl_parser_t* const parser, dsdl_parsed_stmt_t* const out_stmt)
{
    // Must have at least three dashes
    if (!(dsdl_parser_peek(parser, 0) == '-' && dsdl_parser_peek(parser, 1) == '-' &&
          dsdl_parser_peek(parser, 2) == '-')) {
        return false;
    }

    // Consume all consecutive dashes
    while (dsdl_parser_peek(parser, 0) == '-') {
        dsdl_parser_advance(parser, 1);
    }

    out_stmt->kind = dsdl_stmt_service_marker;
    return true;
}

/// Parse a statement (attribute statement: constant, field, or padding).
static bool dsdl_parse_attribute_stmt(dsdl_parser_t* const parser, dsdl_parsed_stmt_t* const out_stmt)
{
    // Parse type
    if (!dsdl_parse_type(parser, &out_stmt->type)) {
        return false;
    }

    dsdl_parser_skip_ws(parser);

    // Check if this is a void (padding) field - no name allowed
    if (dsdl_type_is_void(out_stmt->type.kind) && !dsdl_type_is_array(out_stmt->type.kind)) {
        out_stmt->kind = dsdl_stmt_padding;
        return true;
    }

    // Parse name (required for non-padding fields)
    wkv_str_t name = dsdl_parse_identifier(parser);
    if (name.str == NULL) {
        return false;
    }

    out_stmt->name = name;
    dsdl_parser_skip_ws(parser);

    // Check for constant assignment
    if (dsdl_parser_peek(parser, 0) == '=') {
        dsdl_parser_advance(parser, 1); // Skip '='
        dsdl_parser_skip_ws(parser);

        if (!dsdl_parse_expression(parser, &out_stmt->value)) {
            return false;
        }

        out_stmt->kind      = dsdl_stmt_constant;
        out_stmt->has_value = true;
    } else {
        out_stmt->kind = dsdl_stmt_field;
    }

    return true;
}

/// Parse a statement (any kind).
static bool dsdl_parse_statement(dsdl_parser_t* const parser, dsdl_parsed_stmt_t* const out_stmt)
{
    (void)memset(out_stmt, 0, sizeof(*out_stmt));

    dsdl_parser_skip_ws(parser);

    // Empty line or comment-only line
    const char c = dsdl_parser_peek(parser, 0);
    if ((c == '\0') || (c == '\r') || (c == '\n') || (c == '#')) {
        out_stmt->kind = dsdl_stmt_none;
        return true;
    }

    // Try directive (@...)
    if (c == '@') {
        return dsdl_parse_directive(parser, out_stmt);
    }

    // Try service marker (---)
    if (c == '-') {
        return dsdl_parse_service_marker(parser, out_stmt);
    }

    // Otherwise it's an attribute statement (type name / type name = expr)
    return dsdl_parse_attribute_stmt(parser, out_stmt);
}

/// Parse a single line (statement + optional comment).
static bool dsdl_parse_line(dsdl_parser_t* const parser, dsdl_parsed_stmt_t* const out_stmt)
{
    // Parse statement
    if (!dsdl_parse_statement(parser, out_stmt)) {
        return false;
    }

    // Skip trailing whitespace and comment
    dsdl_parser_skip_line_tail(parser);

    return true;
}

// ============================================================================
// Definition parsing
// ============================================================================

/// Parsed definition intermediate representation.
/// Arrays are heap-allocated and grown as needed during parsing.
typedef struct dsdl_parsed_def_t dsdl_parsed_def_t;

struct dsdl_parsed_def_t
{
    dsdl_t* dsdl; ///< Parser context for memory allocation

    bool                is_service;  ///< True if service type (has request/response)
    size_t              field_count; ///< Number of fields in request (or struct/union)
    size_t              field_capacity;
    dsdl_parsed_type_t* field_types;
    wkv_str_t*          field_names;

    size_t              const_count; ///< Number of constants
    size_t              const_capacity;
    dsdl_value_t*       const_values;
    wkv_str_t*          const_names;
    dsdl_parsed_type_t* const_types;

    // For service types: response fields
    size_t              response_field_count;
    size_t              response_field_capacity;
    dsdl_parsed_type_t* response_field_types;
    wkv_str_t*          response_field_names;

    // Directives
    bool   has_extent;
    size_t extent_bits;
    bool   is_sealed;
    bool   is_deprecated;
    bool   is_union;
};

// Forward declarations
static void dsdl_parsed_def_deinit(dsdl_parsed_def_t* const def);

/// Initialize parsed definition with heap-allocated arrays.
static bool dsdl_parsed_def_init(dsdl_parsed_def_t* const def, dsdl_t* const dsdl)
{
    (void)memset(def, 0, sizeof(*def));
    def->dsdl = dsdl;

    // Start with reasonable initial capacity
    const size_t initial_capacity = 16;

    def->field_capacity = initial_capacity;
    def->field_types    = (dsdl_parsed_type_t*)dsdl_alloc(dsdl, initial_capacity * sizeof(dsdl_parsed_type_t));
    def->field_names    = (wkv_str_t*)dsdl_alloc(dsdl, initial_capacity * sizeof(wkv_str_t));

    def->const_capacity = initial_capacity;
    def->const_values   = (dsdl_value_t*)dsdl_alloc(dsdl, initial_capacity * sizeof(dsdl_value_t));
    def->const_names    = (wkv_str_t*)dsdl_alloc(dsdl, initial_capacity * sizeof(wkv_str_t));
    def->const_types    = (dsdl_parsed_type_t*)dsdl_alloc(dsdl, initial_capacity * sizeof(dsdl_parsed_type_t));

    def->response_field_capacity = initial_capacity;
    def->response_field_types    = (dsdl_parsed_type_t*)dsdl_alloc(dsdl, initial_capacity * sizeof(dsdl_parsed_type_t));
    def->response_field_names    = (wkv_str_t*)dsdl_alloc(dsdl, initial_capacity * sizeof(wkv_str_t));

    // Check if any allocation failed
    if ((def->field_types == NULL) || (def->field_names == NULL) || (def->const_values == NULL) ||
        (def->const_names == NULL) || (def->const_types == NULL) || (def->response_field_types == NULL) ||
        (def->response_field_names == NULL)) {
        dsdl_parsed_def_deinit(def);
        return false;
    }

    return true;
}

/// Free all heap-allocated arrays in parsed definition.
static void dsdl_parsed_def_deinit(dsdl_parsed_def_t* const def)
{
    if (def->dsdl != NULL) {
        dsdl_free(def->dsdl, def->field_types);
        dsdl_free(def->dsdl, def->field_names);
        dsdl_free(def->dsdl, def->const_values);
        dsdl_free(def->dsdl, def->const_names);
        dsdl_free(def->dsdl, def->const_types);
        dsdl_free(def->dsdl, def->response_field_types);
        dsdl_free(def->dsdl, def->response_field_names);
    }
    (void)memset(def, 0, sizeof(*def));
}

/// Ensure field array has capacity for at least one more element.
static bool dsdl_parsed_def_ensure_field_capacity(dsdl_parsed_def_t* const def)
{
    if (def->field_count >= def->field_capacity) {
        const size_t        new_capacity = def->field_capacity * 2;
        dsdl_parsed_type_t* new_types =
          (dsdl_parsed_type_t*)dsdl_realloc(def->dsdl, def->field_types, new_capacity * sizeof(dsdl_parsed_type_t));
        wkv_str_t* new_names = (wkv_str_t*)dsdl_realloc(def->dsdl, def->field_names, new_capacity * sizeof(wkv_str_t));

        if ((new_types == NULL) || (new_names == NULL)) {
            return false;
        }

        def->field_types    = new_types;
        def->field_names    = new_names;
        def->field_capacity = new_capacity;
    }
    return true;
}

/// Ensure const array has capacity for at least one more element.
static bool dsdl_parsed_def_ensure_const_capacity(dsdl_parsed_def_t* const def)
{
    if (def->const_count >= def->const_capacity) {
        const size_t  new_capacity = def->const_capacity * 2;
        dsdl_value_t* new_values =
          (dsdl_value_t*)dsdl_realloc(def->dsdl, def->const_values, new_capacity * sizeof(dsdl_value_t));
        wkv_str_t* new_names = (wkv_str_t*)dsdl_realloc(def->dsdl, def->const_names, new_capacity * sizeof(wkv_str_t));
        dsdl_parsed_type_t* new_types =
          (dsdl_parsed_type_t*)dsdl_realloc(def->dsdl, def->const_types, new_capacity * sizeof(dsdl_parsed_type_t));

        if ((new_values == NULL) || (new_names == NULL) || (new_types == NULL)) {
            return false;
        }

        def->const_values   = new_values;
        def->const_names    = new_names;
        def->const_types    = new_types;
        def->const_capacity = new_capacity;
    }
    return true;
}

/// Ensure response field array has capacity for at least one more element.
static bool dsdl_parsed_def_ensure_response_capacity(dsdl_parsed_def_t* const def)
{
    if (def->response_field_count >= def->response_field_capacity) {
        const size_t        new_capacity = def->response_field_capacity * 2;
        dsdl_parsed_type_t* new_types    = (dsdl_parsed_type_t*)dsdl_realloc(
          def->dsdl, def->response_field_types, new_capacity * sizeof(dsdl_parsed_type_t));
        wkv_str_t* new_names =
          (wkv_str_t*)dsdl_realloc(def->dsdl, def->response_field_names, new_capacity * sizeof(wkv_str_t));

        if ((new_types == NULL) || (new_names == NULL)) {
            return false;
        }

        def->response_field_types    = new_types;
        def->response_field_names    = new_names;
        def->response_field_capacity = new_capacity;
    }
    return true;
}

/// Parse a complete DSDL definition.
/// Expects out_def to be initialized with dsdl_parsed_def_init().
static bool dsdl_parse_definition(dsdl_parser_t* const parser, dsdl_parsed_def_t* const out_def)
{
    bool parsing_response = false;

    while (!dsdl_parser_eof(parser)) {
        dsdl_parsed_stmt_t stmt;
        if (!dsdl_parse_line(parser, &stmt)) {
            return false; // Parse error
        }

        // Process statement based on kind
        switch (stmt.kind) {
            case dsdl_stmt_none:
                // Empty line or comment only - skip
                break;

            case dsdl_stmt_field:
            case dsdl_stmt_padding: {
                if (parsing_response) {
                    if (!dsdl_parsed_def_ensure_response_capacity(out_def)) {
                        return false; // OOM
                    }
                    const size_t idx                   = out_def->response_field_count++;
                    out_def->response_field_types[idx] = stmt.type;
                    out_def->response_field_names[idx] = stmt.name;
                } else {
                    if (!dsdl_parsed_def_ensure_field_capacity(out_def)) {
                        return false; // OOM
                    }
                    const size_t idx          = out_def->field_count++;
                    out_def->field_types[idx] = stmt.type;
                    out_def->field_names[idx] = stmt.name;
                }
                break;
            }

            case dsdl_stmt_constant: {
                if (!dsdl_parsed_def_ensure_const_capacity(out_def)) {
                    return false; // OOM
                }
                const size_t idx           = out_def->const_count++;
                out_def->const_types[idx]  = stmt.type;
                out_def->const_names[idx]  = stmt.name;
                out_def->const_values[idx] = stmt.value;
                break;
            }

            case dsdl_stmt_service_marker:
                if (parsing_response) {
                    return false; // Duplicate service marker
                }
                out_def->is_service = true;
                parsing_response    = true;
                break;

            case dsdl_stmt_directive:
                // Process known directives
                if ((stmt.name.len == 6) && (memcmp(stmt.name.str, "sealed", 6) == 0)) {
                    out_def->is_sealed = true;
                } else if ((stmt.name.len == 6) && (memcmp(stmt.name.str, "extent", 6) == 0)) {
                    if (!stmt.has_value || (stmt.value.kind != dsdl_value_rational) ||
                        !dsdl_rational_is_int(stmt.value.as.rational)) {
                        return false; // Invalid extent
                    }
                    out_def->has_extent  = true;
                    out_def->extent_bits = (size_t)stmt.value.as.rational.num;
                } else if ((stmt.name.len == 10) && (memcmp(stmt.name.str, "deprecated", 10) == 0)) {
                    out_def->is_deprecated = true;
                } else if ((stmt.name.len == 5) && (memcmp(stmt.name.str, "union", 5) == 0)) {
                    out_def->is_union = true;
                }
                // Other directives: @assert, @print - handled differently (semantic analysis)
                break;

            default:
                break;
        }

        // Skip end-of-line
        (void)dsdl_parser_skip_eol(parser);
    }

    return true;
}

// ============================================================================
// Public API implementation
// ============================================================================

static void* wkv_realloc_adapter(wkv_t* const self, void* const ptr, const size_t new_size)
{
    dsdl_t* const owner = self->context;
    return owner->realloc(owner, ptr, new_size);
}

void dsdl_new(dsdl_t* const self, void* (*const realloc_func)(dsdl_t*, void*, size_t))
{
    assert((self != NULL) && (realloc_func != NULL));
    (void)memset(self, 0, sizeof(*self));
    self->realloc = realloc_func;

    wkv_init(&self->types, wkv_realloc_adapter);
    self->types.sep     = '.';
    self->types.context = self;

    self->namespace_count = 0;
    self->namespaces      = NULL;
}

void dsdl_destroy(dsdl_t* const self)
{
    if (self == NULL) {
        return;
    }

    // Free all type definitions stored in the types WKV
    // TODO: iterate and free each composite type

    // Free WKV internal allocations
    while (!wkv_is_empty(&self->types)) {
        wkv_node_t* const node = wkv_at(&self->types, 0);
        if (node != NULL) {
            // Free the type data if present
            if (node->value != NULL) {
                dsdl_free(self, node->value);
            }
            wkv_del(&self->types, node);
        }
    }

    // Free namespace strings and array
    for (size_t i = 0; i < self->namespace_count; i++) {
        // Each namespace string was allocated separately (cast away const - we own this memory)
        dsdl_free(self, (void*)(uintptr_t)self->namespaces[i].str);
    }
    dsdl_free(self, self->namespaces);
    self->namespaces      = NULL;
    self->namespace_count = 0;
}

// ============================================================================
// Type Resolution Helpers
// ============================================================================

/// Parsed type name components
typedef struct
{
    wkv_str_t full_name;      ///< Full name including namespace (e.g., "uavcan.node.Heartbeat")
    wkv_str_t namespace_part; ///< Namespace portion (e.g., "uavcan.node")
    wkv_str_t type_name;      ///< Just the type name (e.g., "Heartbeat")
    uint8_t   major;          ///< Major version
    uint8_t   minor;          ///< Minor version
    bool      has_major;      ///< True if major version was specified
    bool      has_minor;      ///< True if minor version was specified
} dsdl_type_ref_t;

/// Check if a string consists entirely of decimal digits.
static bool dsdl_is_all_digits(const wkv_str_t str)
{
    if ((str.str == NULL) || (str.len == 0)) {
        return false;
    }

    for (size_t i = 0; i < str.len; i++) {
        if (!dsdl_is_digit(str.str[i])) {
            return false;
        }
    }

    return true;
}

/// Parse a decimal integer from a string (simple non-negative version).
/// Returns -1 on error.
static int64_t dsdl_parse_int(const wkv_str_t str)
{
    if (!dsdl_is_all_digits(str)) {
        return -1;
    }

    int64_t result = 0;
    for (size_t i = 0; i < str.len; i++) {
        const int64_t digit = str.str[i] - '0';
        // Check for overflow
        if (result > (INT64_MAX - digit) / 10) {
            return -1;
        }
        result = result * 10 + digit;
    }

    return result;
}

/// Parse a type name string into components.
///
/// Handles forms:
///   - "TypeName" → no namespace, no version
///   - "TypeName.1" → no namespace, major only
///   - "TypeName.1.0" → no namespace, full version
///   - "namespace.TypeName" → namespace, no version
///   - "namespace.TypeName.1" → namespace, major only
///   - "namespace.TypeName.1.0" → namespace, full version
///
/// @param name  Type name string
/// @param out   Output structure to populate
/// @return true on success, false if malformed
static bool dsdl_parse_typename(const wkv_str_t name, dsdl_type_ref_t* const out)
{
    if ((name.str == NULL) || (name.len == 0) || (out == NULL)) {
        return false;
    }

    memset(out, 0, sizeof(*out));

    // Find all '.' separators
    size_t dot_positions[16];
    size_t dot_count = 0;

    for (size_t i = 0; i < name.len && dot_count < 16; i++) {
        if (name.str[i] == '.') {
            dot_positions[dot_count++] = i;
        }
    }

    if (dot_count == 0) {
        // Just "TypeName"
        out->type_name      = name;
        out->namespace_part = (wkv_str_t){ 0, NULL };
        return true;
    }

    // Try to parse version numbers from the end
    size_t version_dots = 0; // How many trailing dots are version separators

    // Check for major.minor format (two trailing numbers)
    if (dot_count >= 2) {
        const size_t    last_dot       = dot_positions[dot_count - 1];
        const wkv_str_t last_component = { name.len - last_dot - 1, name.str + last_dot + 1 };

        if (dsdl_is_all_digits(last_component)) {
            const size_t    prev_dot       = dot_positions[dot_count - 2];
            const wkv_str_t prev_component = { last_dot - prev_dot - 1, name.str + prev_dot + 1 };

            if (dsdl_is_all_digits(prev_component)) {
                // Both are numbers, so: major.minor
                const int64_t major_val = dsdl_parse_int(prev_component);
                const int64_t minor_val = dsdl_parse_int(last_component);

                if ((major_val >= 0) && (major_val <= 255) && (minor_val >= 0) && (minor_val <= 255)) {
                    out->major     = (uint8_t)major_val;
                    out->minor     = (uint8_t)minor_val;
                    out->has_major = true;
                    out->has_minor = true;
                    version_dots   = 2;
                }
            }
        }
    }

    // Check for major-only version (single trailing number)
    if ((version_dots == 0) && (dot_count >= 1)) {
        const size_t    last_dot       = dot_positions[dot_count - 1];
        const wkv_str_t last_component = { name.len - last_dot - 1, name.str + last_dot + 1 };

        if (dsdl_is_all_digits(last_component)) {
            const int64_t major_val = dsdl_parse_int(last_component);
            if ((major_val >= 0) && (major_val <= 255)) {
                out->major     = (uint8_t)major_val;
                out->has_major = true;
                version_dots   = 1;
            }
        }
    }

    // Extract type name and namespace
    if (dot_count > version_dots) {
        // There's a namespace part
        const size_t type_name_dot = dot_positions[dot_count - version_dots - 1];
        out->namespace_part        = (wkv_str_t){ type_name_dot, name.str };
        out->type_name = (wkv_str_t){ (version_dots > 0) ? (dot_positions[dot_count - version_dots] - type_name_dot - 1)
                                                         : (name.len - type_name_dot - 1),
                                      name.str + type_name_dot + 1 };
    } else {
        // No namespace, just type name (and maybe version)
        out->namespace_part = (wkv_str_t){ 0, NULL };
        out->type_name = (version_dots > 0) ? ((wkv_str_t){ dot_positions[dot_count - version_dots], name.str }) : name;
    }

    // Construct full_name (without version)
    if (out->namespace_part.len > 0) {
        out->full_name = (wkv_str_t){ out->namespace_part.len + 1 + out->type_name.len, name.str };
    } else {
        out->full_name = out->type_name;
    }

    return true;
}

/// Try to construct and test a file path.
/// Returns true if the file exists (read() succeeds or returns non-NULL).
static bool dsdl_try_path(dsdl_t* const   self,
                          const char*     namespace_root,
                          const size_t    namespace_root_len,
                          const wkv_str_t namespace_part,
                          const wkv_str_t type_name,
                          const uint8_t   major,
                          const uint8_t   minor,
                          char*           out_path,
                          const size_t    path_capacity)
{
    // Construct path: namespace_root/namespace/TypeName.major.minor.dsdl
    size_t pos = 0;

    // Add namespace root
    if ((pos + namespace_root_len) >= path_capacity) {
        return false;
    }
    memcpy(out_path + pos, namespace_root, namespace_root_len);
    pos += namespace_root_len;

    // Add separator if needed
    if ((namespace_root_len > 0) && (namespace_root[namespace_root_len - 1] != '/')) {
        if ((pos + 1) >= path_capacity) {
            return false;
        }
        out_path[pos++] = '/';
    }

    // Add namespace part (with dots replaced by slashes)
    if (namespace_part.len > 0) {
        if ((pos + namespace_part.len) >= path_capacity) {
            return false;
        }
        for (size_t i = 0; i < namespace_part.len; i++) {
            out_path[pos++] = (namespace_part.str[i] == '.') ? '/' : namespace_part.str[i];
        }

        // Add separator after namespace
        if ((pos + 1) >= path_capacity) {
            return false;
        }
        out_path[pos++] = '/';
    }

    // Add type name
    if ((pos + type_name.len) >= path_capacity) {
        return false;
    }
    memcpy(out_path + pos, type_name.str, type_name.len);
    pos += type_name.len;

    // Add version: .major.minor.dsdl
    // Format: ".%u.%u.dsdl" - max is ".255.255.dsdl" = 13 chars
    if ((pos + 13) >= path_capacity) {
        return false;
    }

    // Simple integer to string conversion for major
    out_path[pos++] = '.';
    if (major >= 100) {
        out_path[pos++] = (char)('0' + (major / 100));
    }
    if (major >= 10) {
        out_path[pos++] = (char)('0' + ((major / 10) % 10));
    }
    out_path[pos++] = (char)('0' + (major % 10));

    // Simple integer to string conversion for minor
    out_path[pos++] = '.';
    if (minor >= 100) {
        out_path[pos++] = (char)('0' + (minor / 100));
    }
    if (minor >= 10) {
        out_path[pos++] = (char)('0' + ((minor / 10) % 10));
    }
    out_path[pos++] = (char)('0' + (minor % 10));

    // Add .dsdl extension
    if ((pos + 6) >= path_capacity) {
        return false;
    }
    memcpy(out_path + pos, ".dsdl", 5);
    pos += 5;
    out_path[pos] = '\0';

    // Try to read the file to check if it exists
    if (self->read != NULL) {
        size_t size   = 0;
        void*  buffer = self->read(self, (wkv_str_t){ pos, out_path }, &size);
        if (buffer != NULL) {
            // File exists! Free the buffer and return success
            dsdl_free(self, buffer);
            return true;
        }
    }

    return false;
}

/// Locate a DSDL file in registered namespace roots.
///
/// Constructs the filesystem path from the type name and searches all registered
/// namespace directories. Handles version resolution.
///
/// @param self     Parser state with registered namespaces
/// @param type_ref Parsed type reference with namespace, name, and version
/// @param out_path Output buffer for the full file path (caller-allocated)
/// @param path_capacity Size of out_path buffer
/// @return true if file found, false otherwise
static bool dsdl_locate_file(dsdl_t* const          self,
                             const dsdl_type_ref_t* type_ref,
                             char*                  out_path,
                             const size_t           path_capacity)
{
    if ((self == NULL) || (type_ref == NULL) || (out_path == NULL) || (path_capacity == 0)) {
        return false;
    }

    // Iterate through registered namespace roots in order (first match wins)
    for (size_t ns_idx = 0; ns_idx < self->namespace_count; ns_idx++) {
        const wkv_str_t*  ns                 = &self->namespaces[ns_idx];
        const char* const namespace_root     = ns->str;
        const size_t      namespace_root_len = ns->len;

        // Version resolution strategy:
        // - If both major and minor specified: try exact match
        // - If only major specified: try minor from 255 down to 0
        // - If neither specified: try major from 255 down to 0, minor from 255 down to 0

        if (type_ref->has_major && type_ref->has_minor) {
            // Exact version specified
            if (dsdl_try_path(self,
                              namespace_root,
                              namespace_root_len,
                              type_ref->namespace_part,
                              type_ref->type_name,
                              type_ref->major,
                              type_ref->minor,
                              out_path,
                              path_capacity)) {
                return true;
            }
        } else if (type_ref->has_major) {
            // Major specified, find latest minor
            for (int minor = 255; minor >= 0; minor--) {
                if (dsdl_try_path(self,
                                  namespace_root,
                                  namespace_root_len,
                                  type_ref->namespace_part,
                                  type_ref->type_name,
                                  type_ref->major,
                                  (uint8_t)minor,
                                  out_path,
                                  path_capacity)) {
                    return true;
                }
            }
        } else {
            // No version specified, find latest major.minor
            for (int major = 255; major >= 0; major--) {
                for (int minor = 255; minor >= 0; minor--) {
                    if (dsdl_try_path(self,
                                      namespace_root,
                                      namespace_root_len,
                                      type_ref->namespace_part,
                                      type_ref->type_name,
                                      (uint8_t)major,
                                      (uint8_t)minor,
                                      out_path,
                                      path_capacity)) {
                        return true;
                    }
                }
            }
        }
    }

    return false; // Not found in any namespace root
}

/// Resolve a composite type reference, loading it if necessary.
/// Returns NULL if the type cannot be found or loaded.
static const dsdl_type_composite_t* dsdl_resolve_composite_type(dsdl_t* const   self,
                                                                const wkv_str_t type_name,
                                                                const uint8_t   version_major,
                                                                const uint8_t   version_minor,
                                                                const wkv_str_t current_namespace)
{
    // Build fully qualified type name with version
    char   full_name[256];
    size_t pos = 0;

    // If type_name doesn't contain a dot, it's relative to current_namespace
    bool has_namespace = false;
    for (size_t i = 0; i < type_name.len; i++) {
        if (type_name.str[i] == '.') {
            has_namespace = true;
            break;
        }
    }

    if (!has_namespace && (current_namespace.len > 0)) {
        // Prepend current namespace
        if ((pos + current_namespace.len + 1) >= sizeof(full_name)) {
            return NULL;
        }
        memcpy(full_name + pos, current_namespace.str, current_namespace.len);
        pos += current_namespace.len;
        full_name[pos++] = '.';
    }

    // Add type name
    if ((pos + type_name.len) >= sizeof(full_name)) {
        return NULL;
    }
    memcpy(full_name + pos, type_name.str, type_name.len);
    pos += type_name.len;

    // Add version
    if ((pos + 10) >= sizeof(full_name)) {
        return NULL;
    }
    full_name[pos++] = '.';
    if (version_major >= 100)
        full_name[pos++] = (char)('0' + (version_major / 100));
    if (version_major >= 10)
        full_name[pos++] = (char)('0' + ((version_major / 10) % 10));
    full_name[pos++] = (char)('0' + (version_major % 10));
    full_name[pos++] = '.';
    if (version_minor >= 100)
        full_name[pos++] = (char)('0' + (version_minor / 100));
    if (version_minor >= 10)
        full_name[pos++] = (char)('0' + ((version_minor / 10) % 10));
    full_name[pos++] = (char)('0' + (version_minor % 10));
    full_name[pos]   = '\0';

    // Try to load the type
    return dsdl_read(self, (wkv_str_t){ pos, full_name });
}

/// Create a type descriptor from a parsed type, resolving composite references.
/// Returns a pointer to the allocated type descriptor, or NULL on error.
/// The returned pointer can be cast to dsdl_type_t* to read the type discriminator,
/// then cast to the appropriate concrete type (dsdl_type_array_t*, dsdl_type_composite_t*, etc.).
static dsdl_type_t* dsdl_create_type_descriptor(dsdl_t* const             self,
                                                const dsdl_parsed_type_t* parsed_type,
                                                const wkv_str_t           current_namespace)
{
    // For primitives, void, and aliases: allocate a single dsdl_type_t
    if (!dsdl_type_is_array(parsed_type->kind) && !dsdl_type_is_composite(parsed_type->kind)) {
        dsdl_type_t* type_ptr = (dsdl_type_t*)dsdl_alloc(self, sizeof(dsdl_type_t));
        if (type_ptr == NULL) {
            return NULL;
        }
        *type_ptr = parsed_type->kind;
        return type_ptr;
    }

    // For arrays: allocate dsdl_type_array_t and recursively create element type
    if (dsdl_type_is_array(parsed_type->kind)) {
        dsdl_type_array_t* arr = (dsdl_type_array_t*)dsdl_alloc(self, sizeof(dsdl_type_array_t));
        if (arr == NULL) {
            return NULL;
        }
        arr->type     = parsed_type->kind;
        arr->capacity = parsed_type->array_size;

        // Create a temporary parsed_type for the element (strip array info)
        dsdl_parsed_type_t element_type = *parsed_type;
        element_type.kind               = parsed_type->element_kind;
        element_type.array_size         = 0;
        element_type.is_variable        = false;

        arr->member_type = dsdl_create_type_descriptor(self, &element_type, current_namespace);
        if (arr->member_type == NULL) {
            dsdl_free(self, arr);
            return NULL;
        }
        return &arr->type;
    }

    // For composite types: resolve and return pointer to the cached type
    if (dsdl_type_is_composite(parsed_type->kind)) {
        const dsdl_type_composite_t* composite = dsdl_resolve_composite_type(
          self, parsed_type->type_name, parsed_type->version_major, parsed_type->version_minor, current_namespace);
        if (composite == NULL) {
            return NULL; // Failed to resolve
        }
        // Return pointer to the composite's type field (first field of the struct).
        // The const cast is safe because all type data is owned by dsdl_t and
        // the field_types array is for reading, not modification.
        return (dsdl_type_t*)(uintptr_t)composite;
    }

    return NULL;
}

bool dsdl_add_namespace(dsdl_t* const self, const wkv_str_t root_directory)
{
    if ((self == NULL) || (root_directory.str == NULL) || (root_directory.len == 0)) {
        return false;
    }

    // Allocate a copy of the directory string
    char* const str_copy = (char*)dsdl_alloc(self, root_directory.len + 1);
    if (str_copy == NULL) {
        return false; // OOM
    }
    (void)memcpy(str_copy, root_directory.str, root_directory.len);
    str_copy[root_directory.len] = '\0';

    // Grow the namespaces array
    const size_t     new_count = self->namespace_count + 1;
    wkv_str_t* const new_array = (wkv_str_t*)dsdl_realloc(self, self->namespaces, new_count * sizeof(wkv_str_t));
    if (new_array == NULL) {
        dsdl_free(self, str_copy);
        return false; // OOM
    }

    // Add the new namespace at the end
    new_array[self->namespace_count].len = root_directory.len;
    new_array[self->namespace_count].str = str_copy;

    self->namespaces      = new_array;
    self->namespace_count = new_count;
    return true;
}

const dsdl_type_composite_t* dsdl_read(dsdl_t* const self, const wkv_str_t type_name)
{
#ifdef DSDL_DEBUG_READ
    fprintf(stderr, "DSDL_READ: type_name='%.*s' (len=%zu)\n", (int)type_name.len, type_name.str, type_name.len);
#endif
    if ((self == NULL) || (type_name.str == NULL) || (type_name.len == 0)) {
#ifdef DSDL_DEBUG_READ
        fprintf(stderr, "DSDL_READ: NULL input\n");
#endif
        return NULL;
    }

    // Parse type name
    dsdl_type_ref_t type_ref;
    if (!dsdl_parse_typename(type_name, &type_ref)) {
#ifdef DSDL_DEBUG_READ
        fprintf(stderr, "DSDL_READ: Failed to parse type name\n");
#endif
        return NULL; // Malformed type name
    }
#ifdef DSDL_DEBUG_READ
    fprintf(stderr,
            "DSDL_READ: Parsed: namespace='%.*s' type='%.*s' version=%d.%d\n",
            (int)type_ref.namespace_part.len,
            type_ref.namespace_part.str,
            (int)type_ref.type_name.len,
            type_ref.type_name.str,
            type_ref.major,
            type_ref.minor);
#endif

    // Use the original type_name as cache key (includes version)
    // E.g., "mymsgs.Inner.1.0" -> cache key is "mymsgs.Inner.1.0"
    wkv_node_t* const cached = wkv_get(&self->types, type_name);
    if (cached != NULL) {
        return (const dsdl_type_composite_t*)cached->value;
    }

    // Locate the DSDL file
    char file_path[512];
    if (!dsdl_locate_file(self, &type_ref, file_path, sizeof(file_path))) {
#ifdef DSDL_DEBUG_READ
        fprintf(stderr, "DSDL_READ: Failed to locate file\n");
#endif
        return NULL; // File not found
    }
#ifdef DSDL_DEBUG_READ
    fprintf(stderr, "DSDL_READ: Located file: '%s'\n", file_path);
#endif

    // Read file contents
    if (self->read == NULL) {
#ifdef DSDL_DEBUG_READ
        fprintf(stderr, "DSDL_READ: No read callback\n");
#endif
        return NULL; // No read callback
    }

    size_t file_size = 0;
    void*  file_data = self->read(self, wkv_key(file_path), &file_size);
    if (file_data == NULL) {
#ifdef DSDL_DEBUG_READ
        fprintf(stderr, "DSDL_READ: Failed to read file\n");
#endif
        return NULL; // Failed to read file
    }
#ifdef DSDL_DEBUG_READ
    fprintf(stderr, "DSDL_READ: Read %zu bytes\n", file_size);
#endif

    // Parse the file
    dsdl_parser_t     parser;
    dsdl_parsed_def_t def;

    dsdl_parser_init(&parser, self, (const char*)file_data, file_size);

    if (!dsdl_parsed_def_init(&def, self)) {
        dsdl_free(self, file_data);
#ifdef DSDL_DEBUG_READ
        fprintf(stderr, "DSDL_READ: def init failed\n");
#endif
        return NULL; // OOM
    }

    if (!dsdl_parse_definition(&parser, &def)) {
        dsdl_parsed_def_deinit(&def);
        dsdl_free(self, file_data);
#ifdef DSDL_DEBUG_READ
        fprintf(stderr, "DSDL_READ: parse failed\n");
#endif
        return NULL; // Parse error
    }
#ifdef DSDL_DEBUG_READ
    fprintf(stderr, "DSDL_READ: parsed OK, field_count=%zu, sealed=%d\n", def.field_count, def.is_sealed);
#endif

    dsdl_free(self, file_data); // Done with file contents

    // Convert parsed definition to dsdl_type_composite_t
    // Calculate total size needed for single allocation
    size_t total_size = sizeof(dsdl_type_composite_t);
    total_size += type_name.len; // Space for name string (use original type_name which includes version)
    total_size += def.field_count * sizeof(wkv_str_t);    // field_names array
    total_size += def.field_count * sizeof(dsdl_type_t*); // field_types array (pointers)

    // Sum up field name string lengths
    size_t total_name_len = 0;
    for (size_t i = 0; i < def.field_count; i++) {
        total_name_len += def.field_names[i].len;
    }
    total_size += total_name_len; // Space for all field name strings

    // Allocate single block
    void* block = dsdl_alloc(self, total_size);
    if (block == NULL) {
        dsdl_parsed_def_deinit(&def);
#ifdef DSDL_DEBUG_READ
        fprintf(stderr, "DSDL_READ: alloc failed, size=%zu\n", total_size);
#endif
        return NULL;
    }
#ifdef DSDL_DEBUG_READ
    fprintf(stderr, "DSDL_READ: allocated %zu bytes\n", total_size);
#endif

    // Layout the block
    dsdl_type_composite_t* composite = (dsdl_type_composite_t*)block;
    char*                  str_ptr   = (char*)(composite + 1);

    // Copy type name (using original type_name which includes version)
    composite->name.len = type_name.len;
    composite->name.str = str_ptr;
    memcpy(str_ptr, type_name.str, type_name.len);
    str_ptr += type_name.len;

    // Set up field_names array
    composite->field_names = (wkv_str_t*)str_ptr;
    str_ptr += def.field_count * sizeof(wkv_str_t);

    // Set up field_types array (array of pointers)
    composite->field_types = (dsdl_type_t**)str_ptr;
    str_ptr += def.field_count * sizeof(dsdl_type_t*);

    // Copy field names and create type descriptors
    for (size_t i = 0; i < def.field_count; i++) {
        composite->field_names[i].len = def.field_names[i].len;
        composite->field_names[i].str = str_ptr;
        memcpy(str_ptr, def.field_names[i].str, def.field_names[i].len);
        str_ptr += def.field_names[i].len;

        // Create type descriptor for this field
        composite->field_types[i] = dsdl_create_type_descriptor(self, &def.field_types[i], type_ref.namespace_part);
        if (composite->field_types[i] == NULL) {
#ifdef DSDL_DEBUG_READ
            fprintf(stderr, "DSDL_READ: type descriptor creation failed for field %zu\n", i);
#endif
            dsdl_parsed_def_deinit(&def);
            dsdl_free(self, block);
            return NULL;
        }
    }

    // Set basic properties
    composite->field_count = def.field_count;
    composite->sealed      = def.is_sealed;
    composite->type =
      def.is_union ? DSDL_COMPOSITE_UNION : (def.is_service ? DSDL_COMPOSITE_RPC : DSDL_COMPOSITE_STRUCT);

    // Use version from parsed type_ref
    composite->version[0] = type_ref.major;
    composite->version[1] = type_ref.minor;

    // Set extent
    composite->extent = def.has_extent ? def.extent_bits / 8 : 0; // Convert bits to bytes

    // Cache the result using type_name as key
    wkv_node_t* const cache_node = wkv_set(&self->types, type_name);
    if (cache_node == NULL) {
        dsdl_parsed_def_deinit(&def);
        dsdl_free(self, block);
        return NULL;
    }
    cache_node->value = composite;

    dsdl_parsed_def_deinit(&def);
    return composite;
}

// ============================================================================
// Size calculation
// ============================================================================

/// Calculate ceil(log2(n)) for n > 0. Returns 0 for n <= 1.
/// Used for array length prefix and union tag bit widths.
static size_t dsdl_ceil_log2(const size_t n)
{
    if (n <= 1) {
        return 0;
    }
    size_t result = 0;
    size_t val    = n - 1; // -1 because we want ceil, not floor
    while (val > 0) {
        val >>= 1;
        result++;
    }
    return result;
}

/// Calculate the bit width of the length prefix for a variable-length array.
/// For capacity C (max elements), prefix is ceil(log2(C + 1)) bits.
static size_t dsdl_array_length_prefix_bits(const size_t capacity) { return dsdl_ceil_log2(capacity + 1); }

/// Calculate the bit width of the union tag for a union with N alternatives.
/// Tag is ceil(log2(N)) bits.
static size_t dsdl_union_tag_bits(const size_t field_count) { return dsdl_ceil_log2(field_count); }

/// Forward declaration for recursive type size calculation.
static size_t dsdl_type_max_bits(const dsdl_type_t* type_ptr);

/// Calculate the maximum serialized size in bits for a composite type.
static size_t dsdl_composite_max_bits(const dsdl_type_composite_t* const composite)
{
    if (composite == NULL) {
        return 0;
    }

    const bool is_union = (composite->type == DSDL_COMPOSITE_UNION);

    if (is_union) {
        // Union: tag bits + max of all field bit sizes (no padding for sealed types)
        const size_t tag_bits = dsdl_union_tag_bits(composite->field_count);

        size_t max_field_bits = 0;
        for (size_t i = 0; i < composite->field_count; i++) {
            const size_t field_bits = dsdl_type_max_bits(composite->field_types[i]);
            if (field_bits > max_field_bits) {
                max_field_bits = field_bits;
            }
        }

        return tag_bits + max_field_bits;
    } else {
        // Struct: sum of all field bit sizes (no padding between fields for sealed types)
        size_t total_bits = 0;
        for (size_t i = 0; i < composite->field_count; i++) {
            total_bits += dsdl_type_max_bits(composite->field_types[i]);
        }
        return total_bits;
    }
}

/// Calculate the native storage size in bytes for a single value of the given type.
/// This is the size of the C type used to represent the value, not the serialized bit size.
/// For arrays of primitives/composites, this returns the element size for iterating.
static size_t dsdl_type_native_size(const dsdl_type_t* type_ptr)
{
    if (type_ptr == NULL) {
        return 0;
    }

    const dsdl_type_t kind = *type_ptr;

    // Void has no storage
    if (dsdl_type_is_void(kind)) {
        return 0;
    }

    // Bool
    if (kind == DSDL_BOOL) {
        return sizeof(bool);
    }

    // Integers (signed and unsigned) - use int_leastX_t sizes
    if (dsdl_type_is_int(kind) || dsdl_type_is_uint(kind) || (kind == DSDL_BYTE)) {
        const size_t bits = dsdl_type_bit_width(kind);
        if (bits <= 8) {
            return sizeof(uint_least8_t);
        } else if (bits <= 16) {
            return sizeof(uint_least16_t);
        } else if (bits <= 32) {
            return sizeof(uint_least32_t);
        } else {
            return sizeof(uint_least64_t);
        }
    }

    // Floats
    if (dsdl_type_is_float(kind)) {
        const size_t bits = dsdl_type_bit_width(kind);
        if (bits <= 32) {
            return sizeof(float);
        } else {
            return sizeof(double);
        }
    }

    // Aliases (UTF8 maps to char)
    if (dsdl_type_is_alias(kind)) {
        if (kind == DSDL_UTF8) {
            return sizeof(char);
        }
        // Other aliases - strip alias and recurse
        const dsdl_type_t base = (dsdl_type_t)(kind & ~DSDL_TYPE_ALIAS_MASK);
        return dsdl_type_native_size(&base);
    }

    // Arrays - the value itself is a pointer (fixed) or dsdl_value_array_variable_t (variable)
    if (dsdl_type_is_array(kind)) {
        if (kind == DSDL_ARRAY_VARIABLE) {
            return sizeof(dsdl_value_array_variable_t);
        } else {
            // Fixed array is represented as void* to elements
            return sizeof(void*);
        }
    }

    // Composites - struct or union value wrapper
    if (dsdl_type_is_composite(kind)) {
        if (kind == DSDL_COMPOSITE_UNION) {
            return sizeof(dsdl_value_union_t);
        } else {
            return sizeof(dsdl_value_struct_t);
        }
    }

    return 0;
}

/// Calculate the maximum serialized size in bits for any type.
/// The type_ptr can point to a dsdl_type_t (primitive), dsdl_type_array_t, or dsdl_type_composite_t.
static size_t dsdl_type_max_bits(const dsdl_type_t* type_ptr)
{
    if (type_ptr == NULL) {
        return 0;
    }

    const dsdl_type_t kind = *type_ptr;

    // Primitive types (void, int, uint, float, bool, byte)
    if (dsdl_type_is_void(kind) || dsdl_type_is_int(kind) || dsdl_type_is_uint(kind) || dsdl_type_is_float(kind)) {
        return dsdl_type_bit_width(kind);
    }

    // Handle aliases (bool = uint1, byte = uint8)
    if (dsdl_type_is_alias(kind)) {
        // Strip alias flag to get base type bit width
        return dsdl_type_bit_width((dsdl_type_t)(kind & ~DSDL_TYPE_ALIAS_MASK));
    }

    // Array types
    if (dsdl_type_is_array(kind)) {
        const dsdl_type_array_t* arr          = (const dsdl_type_array_t*)type_ptr;
        const size_t             element_bits = dsdl_type_max_bits(arr->member_type);
        const size_t             total_bits   = arr->capacity * element_bits;

        if (kind == DSDL_ARRAY_VARIABLE) {
            // Variable-length array: length prefix + elements
            const size_t prefix_bits = dsdl_array_length_prefix_bits(arr->capacity);
            return prefix_bits + total_bits;
        } else {
            // Fixed-length array: just the elements
            return total_bits;
        }
    }

    // Composite types
    if (dsdl_type_is_composite(kind)) {
        const dsdl_type_composite_t* composite = (const dsdl_type_composite_t*)type_ptr;
        return dsdl_composite_max_bits(composite);
    }

    return 0; // Unknown type
}

size_t dsdl_serialized_footprint(const dsdl_type_composite_t* const type)
{
    if (type == NULL) {
        return 0;
    }

    const size_t max_bits = dsdl_composite_max_bits(type);

    // For non-sealed composites, a 32-bit delimiter header is prepended when nested
    // However, for the top-level type, we return just the content size.
    // The delimiter is only added when this type is used as a field in another type.

    return (max_bits + 7) / 8; // Convert bits to bytes, rounding up
}

// ============================================================================
// Bit buffer operations for serialization/deserialization
// ============================================================================

/// Bit buffer for serialization/deserialization.
/// Tracks current bit position within a byte array.
typedef struct
{
    uint8_t* data;          ///< Pointer to the byte buffer
    size_t   capacity_bits; ///< Total capacity in bits
    size_t   offset_bits;   ///< Current bit position
    bool     error;         ///< Set on deserialization error (e.g., array overflow)
} dsdl_bitbuf_t;

/// Write up to 64 bits to the buffer, little-endian.
/// Bits are written starting from the LSB of the value.
static void dsdl_bitbuf_write(dsdl_bitbuf_t* const buf, uint64_t value, size_t bits)
{
    if ((buf == NULL) || (bits == 0)) {
        return;
    }

    while (bits > 0) {
        if (buf->offset_bits >= buf->capacity_bits) {
            return; // Buffer overflow - stop writing
        }

        const size_t byte_index    = buf->offset_bits / 8;
        const size_t bit_in_byte   = buf->offset_bits % 8;
        const size_t bits_in_byte  = 8 - bit_in_byte;
        const size_t bits_to_write = (bits < bits_in_byte) ? bits : bits_in_byte;

        // Mask for the bits we're writing
        const uint8_t mask = (uint8_t)((1U << bits_to_write) - 1U);
        const uint8_t val  = (uint8_t)(value & mask);

        // Clear target bits and write
        buf->data[byte_index] = (uint8_t)((buf->data[byte_index] & ~(mask << bit_in_byte)) | (val << bit_in_byte));

        buf->offset_bits += bits_to_write;
        value >>= bits_to_write;
        bits -= bits_to_write;
    }
}

/// Read up to 64 bits from the buffer, little-endian.
/// If the buffer is exhausted, remaining bits are treated as zero (implicit zero extension).
static uint64_t dsdl_bitbuf_read(dsdl_bitbuf_t* const buf, size_t bits)
{
    if ((buf == NULL) || (bits == 0) || (bits > 64)) {
        return 0;
    }

    uint64_t value     = 0;
    size_t   bit_shift = 0;

    while (bits > 0) {
        if (buf->offset_bits >= buf->capacity_bits) {
            // Implicit zero extension - remaining bits are zero
            break;
        }

        const size_t byte_index   = buf->offset_bits / 8;
        const size_t bit_in_byte  = buf->offset_bits % 8;
        const size_t bits_in_byte = 8 - bit_in_byte;
        const size_t bits_to_read = (bits < bits_in_byte) ? bits : bits_in_byte;

        // Mask for the bits we're reading
        const uint8_t mask = (uint8_t)((1U << bits_to_read) - 1U);
        const uint8_t val  = (uint8_t)((buf->data[byte_index] >> bit_in_byte) & mask);

        value |= ((uint64_t)val << bit_shift);

        buf->offset_bits += bits_to_read;
        bit_shift += bits_to_read;
        bits -= bits_to_read;
    }

    return value;
}

/// Align the buffer to the next byte boundary by writing zero padding bits.
static void dsdl_bitbuf_align_write(dsdl_bitbuf_t* const buf)
{
    if (buf == NULL) {
        return;
    }
    const size_t remainder = buf->offset_bits % 8;
    if (remainder != 0) {
        dsdl_bitbuf_write(buf, 0, 8 - remainder);
    }
}

/// Align the buffer to the next byte boundary by skipping bits during read.
static void dsdl_bitbuf_align_read(dsdl_bitbuf_t* const buf)
{
    if (buf == NULL) {
        return;
    }
    const size_t remainder = buf->offset_bits % 8;
    if (remainder != 0) {
        buf->offset_bits += 8 - remainder;
    }
}

// ============================================================================
// Primitive serialization helpers
// ============================================================================

/// Write a primitive value to the buffer based on its type.
static void dsdl_serialize_primitive(dsdl_bitbuf_t* const buf, const dsdl_type_t type, const void* const value)
{
    if ((buf == NULL) || (value == NULL)) {
        return;
    }

    const size_t bits = dsdl_type_bit_width(type);
    if (bits == 0) {
        return;
    }

    // Handle void types (just write zeros)
    if (dsdl_type_is_void(type)) {
        dsdl_bitbuf_write(buf, 0, bits);
        return;
    }

    // Handle bool (alias for uint1)
    if (type == DSDL_BOOL) {
        const bool* b = (const bool*)value;
        dsdl_bitbuf_write(buf, *b ? 1 : 0, 1);
        return;
    }

    // Handle floats
    if (dsdl_type_is_float(type)) {
        if (bits == 16) {
            // float16 - stored as uint16_t in memory (IEEE 754 half-precision)
            const uint16_t* f16 = (const uint16_t*)value;
            dsdl_bitbuf_write(buf, *f16, 16);
        } else if (bits == 32) {
            const float* f32 = (const float*)value;
            uint32_t     raw;
            memcpy(&raw, f32, sizeof(raw));
            dsdl_bitbuf_write(buf, raw, 32);
        } else if (bits == 64) {
            const double* f64 = (const double*)value;
            uint64_t      raw;
            memcpy(&raw, f64, sizeof(raw));
            dsdl_bitbuf_write(buf, raw, 64);
        }
        return;
    }

    // Handle integers (signed and unsigned)
    if (dsdl_type_is_int(type) || dsdl_type_is_uint(type) || (type == DSDL_BYTE)) {
        // Read the value based on size, then write the appropriate number of bits
        uint64_t raw = 0;
        if (bits <= 8) {
            raw = *(const uint8_t*)value;
        } else if (bits <= 16) {
            raw = *(const uint16_t*)value;
        } else if (bits <= 32) {
            raw = *(const uint32_t*)value;
        } else {
            raw = *(const uint64_t*)value;
        }
        dsdl_bitbuf_write(buf, raw, bits);
        return;
    }
}

/// Read a primitive value from the buffer based on its type.
static void dsdl_deserialize_primitive(dsdl_bitbuf_t* const buf, const dsdl_type_t type, void* const value)
{
    if ((buf == NULL) || (value == NULL)) {
        return;
    }

    const size_t bits = dsdl_type_bit_width(type);
    if (bits == 0) {
        return;
    }

    // Handle void types (just skip bits)
    if (dsdl_type_is_void(type)) {
        (void)dsdl_bitbuf_read(buf, bits);
        return;
    }

    // Handle bool (alias for uint1)
    if (type == DSDL_BOOL) {
        bool* b = (bool*)value;
        *b      = (dsdl_bitbuf_read(buf, 1) != 0);
        return;
    }

    // Handle floats
    if (dsdl_type_is_float(type)) {
        if (bits == 16) {
            uint16_t* f16 = (uint16_t*)value;
            *f16          = (uint16_t)dsdl_bitbuf_read(buf, 16);
        } else if (bits == 32) {
            float*   f32 = (float*)value;
            uint32_t raw = (uint32_t)dsdl_bitbuf_read(buf, 32);
            memcpy(f32, &raw, sizeof(raw));
        } else if (bits == 64) {
            double*  f64 = (double*)value;
            uint64_t raw = dsdl_bitbuf_read(buf, 64);
            memcpy(f64, &raw, sizeof(raw));
        }
        return;
    }

    // Handle integers (signed and unsigned)
    if (dsdl_type_is_int(type) || dsdl_type_is_uint(type) || (type == DSDL_BYTE)) {
        uint64_t raw = dsdl_bitbuf_read(buf, bits);

        // Sign-extend for signed integers
        if (dsdl_type_is_int(type) && (bits < 64)) {
            const uint64_t sign_bit = (uint64_t)1 << (bits - 1);
            if (raw & sign_bit) {
                raw |= ~((uint64_t)0) << bits; // Sign extend
            }
        }

        // Store based on size
        if (bits <= 8) {
            *(uint8_t*)value = (uint8_t)raw;
        } else if (bits <= 16) {
            *(uint16_t*)value = (uint16_t)raw;
        } else if (bits <= 32) {
            *(uint32_t*)value = (uint32_t)raw;
        } else {
            *(uint64_t*)value = raw;
        }
        return;
    }
}

// ============================================================================
// Type serialization (recursive)
// ============================================================================

// Forward declarations for recursive serialization
static void dsdl_serialize_type(dsdl_bitbuf_t* buf, const dsdl_type_t* type_ptr, const void* value);
static void dsdl_deserialize_type(dsdl_bitbuf_t* buf, const dsdl_type_t* type_ptr, void* value);

/// Serialize composite content (without delimiter header).
/// For structs, value_ptr points to dsdl_value_struct_t.
/// For unions, value_ptr points to dsdl_value_union_t.
static void dsdl_serialize_composite_content(dsdl_bitbuf_t* const               buf,
                                             const dsdl_type_composite_t* const composite,
                                             const void* const                  value_ptr)
{
    if (composite->type == DSDL_COMPOSITE_UNION) {
        // Union: value_ptr is dsdl_value_union_t*
        const dsdl_value_union_t* uval = (const dsdl_value_union_t*)value_ptr;
        const size_t              tag  = uval->tag;

        if (tag >= composite->field_count) {
            return; // Invalid tag
        }

        // Write tag bits
        const size_t tag_bits = dsdl_union_tag_bits(composite->field_count);
        dsdl_bitbuf_write(buf, tag, tag_bits);

        // Serialize selected variant (skip void fields when finding the value)
        // Note: unions typically don't have void alternatives, but handle it for completeness
        const dsdl_type_t* field_type = composite->field_types[tag];
        if (!dsdl_type_is_void(*field_type)) {
            dsdl_serialize_type(buf, field_type, uval->value);
        } else {
            // Void field: just write zero bits
            dsdl_bitbuf_write(buf, 0, dsdl_type_bit_width(*field_type));
        }
    } else {
        // Struct: value_ptr is dsdl_value_struct_t*
        const dsdl_value_struct_t* sval = (const dsdl_value_struct_t*)value_ptr;

        // The values array contains only non-void fields, so we track a separate value_index
        size_t value_index = 0;
        for (size_t i = 0; i < composite->field_count; i++) {
            const dsdl_type_t* field_type = composite->field_types[i];

            if (dsdl_type_is_void(*field_type)) {
                // Void field: serialize zero bits, don't consume from values array
                dsdl_bitbuf_write(buf, 0, dsdl_type_bit_width(*field_type));
            } else {
                // Non-void field: serialize from values array
                dsdl_serialize_type(buf, field_type, sval->values[value_index]);
                value_index++;
            }
        }
    }
}

/// Serialize a composite type (struct or union).
/// For delimited (non-sealed) composites, writes a 32-bit delimiter header.
static void dsdl_serialize_composite(dsdl_bitbuf_t* const               buf,
                                     const dsdl_type_composite_t* const composite,
                                     const void* const                  value)
{
    if ((buf == NULL) || (composite == NULL) || (value == NULL)) {
        return;
    }

    // Serialize the content
    dsdl_serialize_composite_content(buf, composite, value);
}

/// Deserialize composite content (without delimiter header).
/// For structs, value_ptr points to dsdl_value_struct_t.
/// For unions, value_ptr points to dsdl_value_union_t.
static void dsdl_deserialize_composite_content(dsdl_bitbuf_t* const               buf,
                                               const dsdl_type_composite_t* const composite,
                                               void* const                        value_ptr)
{
    if (composite->type == DSDL_COMPOSITE_UNION) {
        // Union: value_ptr is dsdl_value_union_t*
        dsdl_value_union_t* uval = (dsdl_value_union_t*)value_ptr;

        // Read tag
        const size_t tag_bits = dsdl_union_tag_bits(composite->field_count);
        size_t       tag      = (size_t)dsdl_bitbuf_read(buf, tag_bits);

        if (tag >= composite->field_count) {
            tag = 0; // Invalid tag, default to first variant
        }

        // Store tag
        uval->tag = tag;

        // Deserialize selected variant
        const dsdl_type_t* field_type = composite->field_types[tag];
        if (!dsdl_type_is_void(*field_type)) {
            dsdl_deserialize_type(buf, field_type, uval->value);
        } else {
            // Void field: just skip bits
            (void)dsdl_bitbuf_read(buf, dsdl_type_bit_width(*field_type));
        }
    } else {
        // Struct: value_ptr is dsdl_value_struct_t*
        dsdl_value_struct_t* sval = (dsdl_value_struct_t*)value_ptr;

        // The values array contains only non-void fields, so we track a separate value_index
        size_t value_index = 0;
        for (size_t i = 0; i < composite->field_count; i++) {
            const dsdl_type_t* field_type = composite->field_types[i];

            if (dsdl_type_is_void(*field_type)) {
                // Void field: skip bits, don't consume from values array
                (void)dsdl_bitbuf_read(buf, dsdl_type_bit_width(*field_type));
            } else {
                // Non-void field: deserialize into values array
                dsdl_deserialize_type(buf, field_type, sval->values[value_index]);
                value_index++;
            }
        }
    }
}

/// Deserialize a composite type (struct or union).
static void dsdl_deserialize_composite(dsdl_bitbuf_t* const               buf,
                                       const dsdl_type_composite_t* const composite,
                                       void* const                        value)
{
    if ((buf == NULL) || (composite == NULL) || (value == NULL)) {
        return;
    }

    // Deserialize the content
    dsdl_deserialize_composite_content(buf, composite, value);
}

/// Serialize any type (primitive, array, or composite).
static void dsdl_serialize_type(dsdl_bitbuf_t* const buf, const dsdl_type_t* const type_ptr, const void* const value)
{
    if ((buf == NULL) || (type_ptr == NULL) || (value == NULL)) {
        return;
    }

    const dsdl_type_t kind = *type_ptr;

    // Primitive types
    if (dsdl_type_is_void(kind) || dsdl_type_is_int(kind) || dsdl_type_is_uint(kind) || dsdl_type_is_float(kind) ||
        (kind == DSDL_BOOL) || (kind == DSDL_BYTE)) {
        dsdl_serialize_primitive(buf, kind, value);
        return;
    }

    // Handle aliases
    if (dsdl_type_is_alias(kind)) {
        const dsdl_type_t base = (dsdl_type_t)(kind & ~DSDL_TYPE_ALIAS_MASK);
        dsdl_serialize_primitive(buf, base, value);
        return;
    }

    // Array types
    if (dsdl_type_is_array(kind)) {
        const dsdl_type_array_t* arr = (const dsdl_type_array_t*)type_ptr;

        // Get native storage size of each element
        const size_t element_size = dsdl_type_native_size(arr->member_type);

        if (kind == DSDL_ARRAY_VARIABLE) {
            // Variable array: value is dsdl_value_array_variable_t*
            const dsdl_value_array_variable_t* var = (const dsdl_value_array_variable_t*)value;

            // Write length prefix
            const size_t prefix_bits  = dsdl_array_length_prefix_bits(arr->capacity);
            const size_t actual_count = (var->count <= arr->capacity) ? var->count : arr->capacity;
            dsdl_bitbuf_write(buf, actual_count, prefix_bits);

            // Write elements from members pointer
            for (size_t i = 0; i < actual_count; i++) {
                const void* elem = (const char*)var->members + (i * element_size);
                dsdl_serialize_type(buf, arr->member_type, elem);
            }
        } else {
            // Fixed array: value is void* pointing directly to elements
            for (size_t i = 0; i < arr->capacity; i++) {
                const void* elem = (const char*)value + (i * element_size);
                dsdl_serialize_type(buf, arr->member_type, elem);
            }
        }
        return;
    }

    // Composite types
    if (dsdl_type_is_composite(kind)) {
        const dsdl_type_composite_t* composite = (const dsdl_type_composite_t*)type_ptr;

        if (composite->sealed) {
            // Sealed composite: serialize content directly (bit-packed)
            dsdl_serialize_composite_content(buf, composite, value);
        } else {
            // Delimited composite: byte-align, write 32-bit delimiter, then byte-aligned content
            dsdl_bitbuf_align_write(buf);

            // Remember position for delimiter
            const size_t delimiter_byte_pos = buf->offset_bits / 8;

            // Write placeholder delimiter (32 bits = 4 bytes)
            dsdl_bitbuf_write(buf, 0, 32);

            // Serialize content
            const size_t content_start = buf->offset_bits;
            dsdl_serialize_composite_content(buf, composite, value);

            // Byte-align content
            dsdl_bitbuf_align_write(buf);

            // Calculate content size and update delimiter
            const size_t content_bytes = (buf->offset_bits - content_start + 7) / 8;
            if (delimiter_byte_pos + 4 <= buf->capacity_bits / 8) {
                // Write delimiter (little-endian 32-bit)
                buf->data[delimiter_byte_pos + 0] = (uint8_t)(content_bytes & 0xFF);
                buf->data[delimiter_byte_pos + 1] = (uint8_t)((content_bytes >> 8) & 0xFF);
                buf->data[delimiter_byte_pos + 2] = (uint8_t)((content_bytes >> 16) & 0xFF);
                buf->data[delimiter_byte_pos + 3] = (uint8_t)((content_bytes >> 24) & 0xFF);
            }
        }
        return;
    }
}

/// Deserialize any type (primitive, array, or composite).
static void dsdl_deserialize_type(dsdl_bitbuf_t* const buf, const dsdl_type_t* const type_ptr, void* const value)
{
    if ((buf == NULL) || (type_ptr == NULL) || (value == NULL)) {
        return;
    }

    const dsdl_type_t kind = *type_ptr;

    // Primitive types
    if (dsdl_type_is_void(kind) || dsdl_type_is_int(kind) || dsdl_type_is_uint(kind) || dsdl_type_is_float(kind) ||
        (kind == DSDL_BOOL) || (kind == DSDL_BYTE)) {
        dsdl_deserialize_primitive(buf, kind, value);
        return;
    }

    // Handle aliases
    if (dsdl_type_is_alias(kind)) {
        const dsdl_type_t base = (dsdl_type_t)(kind & ~DSDL_TYPE_ALIAS_MASK);
        dsdl_deserialize_primitive(buf, base, value);
        return;
    }

    // Array types
    if (dsdl_type_is_array(kind)) {
        const dsdl_type_array_t* arr = (const dsdl_type_array_t*)type_ptr;

        // Get native storage size of each element
        const size_t element_size = dsdl_type_native_size(arr->member_type);

        if (kind == DSDL_ARRAY_VARIABLE) {
            // Variable array: value is dsdl_value_array_variable_t*
            dsdl_value_array_variable_t* var = (dsdl_value_array_variable_t*)value;

            // Read length prefix
            const size_t prefix_bits    = dsdl_array_length_prefix_bits(arr->capacity);
            size_t       count_from_msg = (size_t)dsdl_bitbuf_read(buf, prefix_bits);

            // Clamp to type capacity (message can't exceed type definition)
            if (count_from_msg > arr->capacity) {
                count_from_msg = arr->capacity;
            }

            // Fail if user buffer is too small
            if (count_from_msg > var->count) {
                buf->error = true;
                return;
            }

            // Update count to actual deserialized elements
            var->count = count_from_msg;

            // Read elements into members buffer
            for (size_t i = 0; i < count_from_msg; i++) {
                void* elem = (char*)var->members + (i * element_size);
                dsdl_deserialize_type(buf, arr->member_type, elem);
            }
        } else {
            // Fixed array: value is void* pointing directly to elements
            for (size_t i = 0; i < arr->capacity; i++) {
                void* elem = (char*)value + (i * element_size);
                dsdl_deserialize_type(buf, arr->member_type, elem);
            }
        }
        return;
    }

    // Composite types
    if (dsdl_type_is_composite(kind)) {
        const dsdl_type_composite_t* composite = (const dsdl_type_composite_t*)type_ptr;

        if (composite->sealed) {
            // Sealed composite: deserialize content directly (bit-packed)
            dsdl_deserialize_composite_content(buf, composite, value);
        } else {
            // Delimited composite: byte-align, read 32-bit delimiter, then deserialize content
            dsdl_bitbuf_align_read(buf);

            // Read delimiter (32 bits = content size in bytes)
            const uint32_t delimiter = (uint32_t)dsdl_bitbuf_read(buf, 32);

            // Remember where content starts
            const size_t content_start_bits = buf->offset_bits;

            // Deserialize content
            dsdl_deserialize_composite_content(buf, composite, value);

            // Skip to end of delimited content (in case there's extra data from newer version)
            const size_t content_end_bits = content_start_bits + ((size_t)delimiter * 8);
            if (content_end_bits > buf->offset_bits) {
                buf->offset_bits = content_end_bits;
            }

            // Byte-align after content
            dsdl_bitbuf_align_read(buf);
        }
        return;
    }
}

// ============================================================================
// Public serialization API
// ============================================================================

size_t dsdl_serialize(const dsdl_type_composite_t* const type,
                      const void* const                  value,
                      const size_t                       output_size,
                      void* const                        output)
{
    if ((type == NULL) || (value == NULL) || (output == NULL) || (output_size == 0)) {
        return 0;
    }

    // Zero-initialize output buffer
    memset(output, 0, output_size);

    dsdl_bitbuf_t buf = {
        .data          = (uint8_t*)output,
        .capacity_bits = output_size * 8,
        .offset_bits   = 0,
    };

    // Serialize the composite type with the provided value
    dsdl_serialize_composite(&buf, type, value);

    // Return bytes written (rounded up)
    return (buf.offset_bits + 7) / 8;
}

size_t dsdl_deserialize(const dsdl_type_composite_t* const type,
                        void* const                        value,
                        const size_t                       input_size,
                        const void* const                  input)
{
    if ((type == NULL) || (value == NULL) || (input == NULL) || (input_size == 0)) {
        return 0;
    }

    dsdl_bitbuf_t buf = {
        .data          = (uint8_t*)(uintptr_t)input, // Cast away const for the buffer struct
        .capacity_bits = input_size * 8,
        .offset_bits   = 0,
        .error         = false,
    };

    // Deserialize the composite type into the provided value buffer
    dsdl_deserialize_composite(&buf, type, value);

    // Return 0 on error, otherwise bytes consumed (rounded up)
    if (buf.error) {
        return 0;
    }
    return (buf.offset_bits + 7) / 8;
}
