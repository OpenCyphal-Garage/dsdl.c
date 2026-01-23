# Cyphal DSDL Parser in C: Implementation Plan

This document outlines the comprehensive implementation plan for `dsdl.c`, a compact C library that parses Cyphal DSDL definitions at runtime and provides serialization/deserialization capabilities.

## Design Constraints

- **Language:** C99+ (compatible with C99 and later standards)
- **C++ Compatibility:** Header must compile with C++20+
- **No stdio:** No dependency on standard I/O
- **No heap:** Memory allocation only via user-provided `realloc` callback
- **Single file:** Entire implementation in `dsdl.c`
- **Minimal dependencies:** Only `lib/wkv.h` for name lookups
- **Rational precision:** The DSDL spec requires "unlimited" precision rationals for expression
  evaluation. We use `intmax_t/uintmax_t` (C99-C17) or `_BitInt(2048)` (C23+) as a practical
  compromise. This may be revised to arbitrary precision in the future if needed.

---

## Phase 0: Project Infrastructure Setup

### 0.1 Build System (CMake)

Create a CMake-based build system following the libcanard project structure:

```
dsdl.c/
├── CMakeLists.txt              # Root CMake configuration
├── dsdl.h                      # Public API (existing)
├── dsdl.c                      # Implementation (to create)
├── lib/
│   └── wkv.h                   # WKV utility (existing)
├── tests/
│   ├── CMakeLists.txt          # Test build configuration
│   ├── test_main.c             # Unity test runner
│   ├── test_parser.c           # Parser unit tests
│   ├── test_expression.c       # Expression evaluation tests
│   ├── test_serialization.c    # Serialization tests
│   ├── test_api_cpp.cpp        # C++20 compatibility test
│   └── unity/                  # ThrowTheSwitch Unity (submodule)
└── cmake/
    └── Coverage.cmake          # Code coverage configuration
```

**Key CMake features:**
- Debug/Release configurations
- clang-tidy integration for static analysis
- clang-format integration for code style
- Code coverage via gcov/llvm-cov
- Unity test framework integration
- Option to build with sanitizers (ASan, UBSan)
- C99 standard for library, C++20 for compatibility test

### 0.2 Test Framework Setup

- Add Unity as a git submodule (from `reference_implementations/nunavut/submodules/unity/`)
- Create test harness that supports `#include <dsdl.c>` for internal testing
- Set up code coverage reporting (target: 90%+ line coverage)

### 0.3 Verification Infrastructure

- Script to run PyDSDL on test namespaces for reference output
- Script to generate Nunavut C code for cross-validation
- CI configuration (GitHub Actions) for automated testing

---

## Phase 1: PEG Parser Implementation

The parser will be implemented directly in `dsdl.c` as a hand-written recursive descent PEG parser. This approach is chosen because:
1. The grammar is well-defined (182 lines in `specs/grammar.peg`)
2. No external dependencies
3. Full control over memory allocation
4. Better error messages

### 1.1 Parser Foundation

**Internal structures:**
```c
// Parser state
typedef struct {
    const char* input;      // Input buffer
    size_t      len;        // Input length
    size_t      pos;        // Current position
    size_t      line;       // Current line number
    size_t      col;        // Current column
    dsdl_t*     dsdl;       // Parent state for memory allocation
} dsdl_parser_t;

// Parse result
typedef struct {
    bool        success;
    size_t      consumed;   // Bytes consumed on success
    const char* error;      // Error message on failure
} dsdl_parse_result_t;
```

**Core parsing utilities:**
- `_dsdl_peek(parser, offset)` - Look ahead without consuming
- `_dsdl_advance(parser, count)` - Consume characters
- `_dsdl_match(parser, str)` - Match exact string
- `_dsdl_match_regex(parser, pattern)` - Match pattern (simplified regex subset)
- `_dsdl_skip_ws(parser)` - Skip whitespace (space, tab)
- `_dsdl_skip_comment(parser)` - Skip `#...` to end of line

### 1.2 Literal Parsing

Following `grammar.peg` lines 142-182:

| Rule | Implementation |
|------|----------------|
| `literal_integer_binary` | `_dsdl_parse_int_binary()` - `0b[01_]+` |
| `literal_integer_octal` | `_dsdl_parse_int_octal()` - `0o[0-7_]+` |
| `literal_integer_hexadecimal` | `_dsdl_parse_int_hex()` - `0x[0-9a-fA-F_]+` |
| `literal_integer_decimal` | `_dsdl_parse_int_decimal()` - `[0-9_]+` |
| `literal_real` | `_dsdl_parse_real()` - point/exponent notation |
| `literal_string` | `_dsdl_parse_string()` - single/double quoted with escapes |
| `literal_boolean` | `_dsdl_parse_boolean()` - `true`/`false` |
| `literal_set` | `_dsdl_parse_set()` - `{expr, expr, ...}` |

**Rational number representation:**

Per the DSDL specification (section 3.1), all numeric values during expression evaluation must be
represented as rational numbers with exact arithmetic. The spec requires "unlimited" range, but
for practical embedded use we adopt the following compromise:

```c
// Rational number for expression evaluation.
// Per spec: numerator/denominator, denominator always positive, GCD(num,den)==1.
//
// C99-C17: Uses intmax_t/uintmax_t (typically 64-bit, may be larger).
// C23+:    Can use _BitInt(2048) for much larger range.
//
// FUTURE: If larger precision is needed, this may be revised to use arbitrary
// precision integers allocated via the user-provided realloc callback.
#if defined(__STDC_VERSION__) && (__STDC_VERSION__ >= 202311L)
    typedef struct {
        _BitInt(2048)          num;  // Numerator (signed)
        unsigned _BitInt(2048) den;  // Denominator (always positive)
    } dsdl_rational_t;
#else
    typedef struct {
        intmax_t  num;   // Numerator (signed)
        uintmax_t den;   // Denominator (always positive, 0 means NaN)
    } dsdl_rational_t;
#endif
```

**Note:** The `**` (power) operator with non-integer exponent has "implementation-defined accuracy"
per the spec, so we may use floating-point approximation for that specific case.

**Value representation:**
```c
typedef enum {
    DSDL_VALUE_RATIONAL,  // Numeric value (integer or real, stored as rational)
    DSDL_VALUE_STRING,    // Unicode string
    DSDL_VALUE_BOOL,      // Boolean
    DSDL_VALUE_SET,       // Set of values
    DSDL_VALUE_TYPE,      // Serializable metatype (for type expressions)
} dsdl_value_kind_t;

typedef struct dsdl_value_t {
    dsdl_value_kind_t kind;
    union {
        dsdl_rational_t rational;  // Numeric value
        wkv_str_t       string;    // String value (borrowed pointer)
        bool            boolean;   // Boolean value
        struct {                   // Set value
            size_t               count;
            struct dsdl_value_t* elements;
        } set;
        // Note: _offset_ is represented as a set of rationals {min, max} when
        // the offset is variable, or a single rational when fixed.
    };
} dsdl_value_t;
```

### 1.3 Expression Parsing

Following `grammar.peg` lines 74-140, implement operator precedence parsing:

| Precedence | Operators | Rule |
|------------|-----------|------|
| 1 (lowest) | `\|\|`, `&&` | `ex_logical` |
| 2 | `!` (unary) | `ex_logical_not` |
| 3 | `==`, `!=`, `<`, `<=`, `>`, `>=` | `ex_comparison` |
| 4 | `\|`, `^`, `&` | `ex_bitwise` |
| 5 | `+`, `-` | `ex_additive` |
| 6 | `*`, `/`, `%` | `ex_multiplicative` |
| 7 | `+`, `-` (unary) | `ex_inversion` |
| 8 | `**` | `ex_exponential` |
| 9 (highest) | `.` | `ex_attribute` |

**Implementation approach:** Pratt parser or recursive descent with precedence climbing.

**Special handling:**
- `_offset_` variable with `.min` and `.max` attributes
- Type names as expressions (for `@assert` validation)
- Set literals `{a, b, c}`

### 1.4 Type Parsing

Following `grammar.peg` lines 30-72:

**Primitive types:**
```c
// Parse: "bool", "byte", "utf8"
// Parse: "void" + bit_length (1-64)
// Parse: "int" + bit_length (2-64)
// Parse: "uint" + bit_length (1-64)
// Parse: "float" + bit_length (16, 32, 64)
// Parse: ("saturated" | "truncated")? primitive_name
```

**Array types:**
```c
// Parse: type "[" expression "]"           -> fixed array
// Parse: type "[" "<=" expression "]"      -> variable inclusive
// Parse: type "[" "<" expression "]"       -> variable exclusive
```

**Versioned composite types:**
```c
// Parse: identifier ("." identifier)* "." major "." minor
// Example: "uavcan.node.Heartbeat.1.0"
```

### 1.5 Statement Parsing

Following `grammar.peg` lines 9-28:

| Statement Type | Pattern | Example |
|----------------|---------|---------|
| Constant | `type identifier = expression` | `uint8 MAX = 255` |
| Field | `type identifier` | `uint32 timestamp` |
| Padding | `type_void` | `void7` |
| Directive | `@identifier [expression]` | `@sealed`, `@extent 64*8` |
| Service marker | `---+` | `---` |

### 1.6 Definition Parsing

Top-level parsing combining all components:

```c
// Parse entire DSDL file
static dsdl_composite_t* _dsdl_parse_definition(
    dsdl_parser_t* parser,
    wkv_str_t      full_type_name,  // e.g., "uavcan.node.Heartbeat"
    uint8_t        version_major,
    uint8_t        version_minor
);
```

**Service detection:** Presence of `---` marker indicates RPC type.

---

## Phase 2: Semantic Analysis

### 2.1 Type Resolution & Dependency Management

When parsing a composite type that references another type:

```c
// Given: "uavcan.time.SynchronizedTimestamp.1.0 timestamp"
// 1. Check if type exists in dsdl->types (WKV lookup)
// 2. If not, locate the .dsdl file in registered namespaces
// 3. Parse the dependency recursively
// 4. Add to dsdl->types cache
```

**Namespace resolution:**
- Split fully qualified name by `.`
- Map to filesystem path: `namespace/subns/Type.major.minor.dsdl`
- Search all registered namespace roots

**Version resolution:**
- If version specified: exact match required
- If only major: highest minor within major
- If none: highest major.minor available

### 2.2 Constant Expression Evaluation

Evaluate compile-time constant expressions:

```c
// Evaluate expression in context of current type definition
static dsdl_value_t _dsdl_eval_expression(
    dsdl_parser_t*       parser,
    dsdl_ast_expr_t*     expr,
    dsdl_eval_context_t* ctx  // Contains defined constants, _offset_ state
);
```

**Context tracking:**
- Previously defined constants in same file
- `_offset_` with current bit position (min/max for variable types)
- Referenced type constants (e.g., `OtherType.CONSTANT`)

### 2.3 Assertion Validation

Process `@assert` directives:

```c
// Validate: @assert _offset_ % 8 == {0}
// The expression must evaluate to true or a set containing true
```

### 2.4 Extent Calculation

Calculate serialized size bounds:

```c
typedef struct {
    size_t min_bits;  // Minimum serialized size
    size_t max_bits;  // Maximum serialized size (extent if specified)
} dsdl_extent_t;

static dsdl_extent_t _dsdl_calculate_extent(const dsdl_composite_t* type);
```

**Rules:**
- Primitives: fixed size = bit width
- Fixed arrays: element_size * count
- Variable arrays: length_prefix_bits + element_size * max_count
- Composites: sum of field extents (union: max of alternatives)
- `@extent` directive overrides calculated maximum

---

## Phase 3: Serialization & Deserialization

### 3.1 Bit-Level Buffer Operations

Core utilities for bit-level manipulation:

```c
// Bit buffer for serialization
typedef struct {
    uint8_t* data;
    size_t   capacity_bits;
    size_t   offset_bits;    // Current position
} dsdl_bitbuf_t;

// Write N bits (1-64) from value, little-endian
static void _dsdl_bitbuf_write(dsdl_bitbuf_t* buf, uint64_t value, size_t bits);

// Read N bits (1-64) to value, little-endian
static uint64_t _dsdl_bitbuf_read(dsdl_bitbuf_t* buf, size_t bits);

// Align to byte boundary with zero padding
static void _dsdl_bitbuf_align(dsdl_bitbuf_t* buf);
```

### 3.2 Primitive Serialization

| Type | Serialization |
|------|---------------|
| `voidN` | Write N zero bits |
| `uintN` | Write N bits, little-endian, saturate/truncate |
| `intN` | Write N bits, two's complement, little-endian, saturate/truncate |
| `float16` | IEEE 754 half-precision |
| `float32` | IEEE 754 single-precision |
| `float64` | IEEE 754 double-precision |
| `bool` | 1 bit |
| `byte` | 8 bits (alias for uint8) |

**Saturation vs Truncation:**
```c
// Saturated: clamp to representable range
// Truncated: keep lower N bits only
static uint64_t _dsdl_saturate_uint(uint64_t value, size_t bits);
static int64_t  _dsdl_saturate_int(int64_t value, size_t bits);
static uint64_t _dsdl_truncate_uint(uint64_t value, size_t bits);
```

### 3.3 Array Serialization

**Fixed arrays:**
```c
// No length prefix, serialize all elements sequentially
for (size_t i = 0; i < capacity; i++) {
    _dsdl_serialize_element(buf, type, &elements[i]);
}
```

**Variable arrays:**
```c
// Length prefix: ceil(log2(max_count+1)) bits for inclusive, ceil(log2(max_count)) for exclusive
size_t prefix_bits = _dsdl_array_length_prefix_bits(array_type);
_dsdl_bitbuf_write(buf, count, prefix_bits);
for (size_t i = 0; i < count; i++) {
    _dsdl_serialize_element(buf, element_type, &elements[i]);
}
```

### 3.4 Composite Serialization

**Structs:**
```c
// Serialize fields in declaration order
for (size_t i = 0; i < type->field_count; i++) {
    _dsdl_serialize_field(buf, &type->field_types[i], field_values[i]);
}
// Pad to byte boundary at end
_dsdl_bitbuf_align(buf);
```

**Unions:**
```c
// Union tag: ceil(log2(field_count)) bits, aligned to byte
_dsdl_bitbuf_align(buf);
_dsdl_bitbuf_write(buf, tag, _dsdl_union_tag_bits(type));
// Serialize selected variant
_dsdl_serialize_field(buf, &type->field_types[tag], value);
_dsdl_bitbuf_align(buf);
```

**Delimited (variable-extent) composites:**
```c
// 32-bit delimiter header (little-endian)
size_t header_pos = buf->offset_bits / 8;
_dsdl_bitbuf_write(buf, 0, 32);  // Placeholder
// Serialize content
_dsdl_serialize_composite(buf, inner_type, value);
// Update delimiter header with actual size
size_t content_size = (buf->offset_bits / 8) - header_pos - 4;
memcpy(&buf->data[header_pos], &content_size, 4);
```

### 3.5 Deserialization

Mirror of serialization with additional handling:
- **Implicit zero extension:** If input exhausted, treat remaining as zeros
- **Delimiter truncation:** If content exceeds type extent, skip excess bytes
- **Validation:** Check array lengths, union tags, delimiter headers

### 3.6 Public API Implementation

```c
size_t dsdl_serialize(
    const dsdl_composite_t* type,
    const size_t            output_size,
    void*                   output
) {
    dsdl_bitbuf_t buf = {output, output_size * 8, 0};
    _dsdl_serialize_composite(&buf, type, type->values);
    return (buf.offset_bits + 7) / 8;  // Bytes written
}

size_t dsdl_deserialize(
    dsdl_composite_t* type,
    const size_t      input_size,
    const void*       input
) {
    dsdl_bitbuf_t buf = {(uint8_t*)input, input_size * 8, 0};
    _dsdl_deserialize_composite(&buf, type);
    return (buf.offset_bits + 7) / 8;  // Bytes consumed
}
```

---

## Phase 4: Testing & Validation

### 4.1 Parser Unit Tests

Test each grammar rule with positive and negative cases:

```c
// test_parser.c
void test_parse_integer_literals(void) {
    TEST_ASSERT_EQUAL(255, parse_int("255"));
    TEST_ASSERT_EQUAL(0xFF, parse_int("0xFF"));
    TEST_ASSERT_EQUAL(0b1010, parse_int("0b1010"));
    TEST_ASSERT_EQUAL(0o777, parse_int("0o777"));
    TEST_ASSERT_EQUAL(1000000, parse_int("1_000_000"));
}

void test_parse_expressions(void) {
    TEST_ASSERT_EQUAL(14, eval("2 + 3 * 4"));        // Precedence
    TEST_ASSERT_EQUAL(20, eval("(2 + 3) * 4"));      // Grouping
    TEST_ASSERT_EQUAL(8, eval("2 ** 3"));            // Power
    TEST_ASSERT_TRUE(eval_bool("3 < 5 && 5 < 7"));   // Logical
}
```

### 4.2 Type Parsing Tests

Parse all 433 test DSDL files:

```c
void test_parse_all_test_types(void) {
    dsdl_t dsdl;
    dsdl_new(&dsdl, test_realloc);
    dsdl_add_namespace(&dsdl, wkv_key("test_dsdl_root_namespaces/uavcan"));
    dsdl_add_namespace(&dsdl, wkv_key("test_dsdl_root_namespaces/nunavut_test_types"));
    // ... add all namespaces

    // Parse each type and verify no errors
    const char* types[] = {
        "uavcan.node.Heartbeat.1.0",
        "uavcan.diagnostic.Record.1.1",
        "regulated.basics.Primitive.0.1",
        // ... all types
    };
    for (size_t i = 0; i < sizeof(types)/sizeof(types[0]); i++) {
        const dsdl_composite_t* t = dsdl_read(&dsdl, wkv_key(types[i]));
        TEST_ASSERT_NOT_NULL(t);
    }
    dsdl_destroy(&dsdl);
}
```

### 4.3 Serialization Roundtrip Tests

```c
void test_serialize_roundtrip_primitive(void) {
    // Similar to Nunavut's testPrimitive
    for (int i = 0; i < 100; i++) {
        // Create random values
        // Serialize
        // Deserialize
        // Compare fields
    }
}
```

### 4.4 Cross-Validation with Nunavut

For each test type:
1. Generate Nunavut C code for the type
2. Create identical test values
3. Serialize with both implementations
4. Compare byte-by-byte
5. Deserialize with both implementations
6. Compare field values

```c
void test_cross_validate_struct(void) {
    // Our implementation
    dsdl_composite_t* our_type = dsdl_read(&dsdl, wkv_key("regulated.basics.Struct_.0.1"));
    // Set fields...
    size_t our_size = dsdl_serialize(our_type, sizeof(our_buf), our_buf);

    // Nunavut implementation
    regulated_basics_Struct__0_1 nunavut_obj = {/* same values */};
    size_t nunavut_size = sizeof(nunavut_buf);
    regulated_basics_Struct__0_1_serialize_(&nunavut_obj, nunavut_buf, &nunavut_size);

    // Compare
    TEST_ASSERT_EQUAL(nunavut_size, our_size);
    TEST_ASSERT_EQUAL_MEMORY(nunavut_buf, our_buf, our_size);
}
```

### 4.5 C++20 Compatibility Test

```cpp
// test_api_cpp.cpp
extern "C" {
#include "dsdl.h"
}

TEST(CppCompatibility, CanUseAPI) {
    dsdl_t dsdl;
    dsdl_new(&dsdl, [](dsdl_t* self, void* ptr, size_t size) -> void* {
        (void)self;
        return realloc(ptr, size);
    });
    // ... use API
    dsdl_destroy(&dsdl);
}
```

### 4.6 Edge Cases

- Empty DSDL files (valid)
- Maximum nesting depth
- Very long type names
- Unicode in comments
- All primitive bit widths (1-64)
- Circular dependencies (error)
- Missing dependencies (error)
- Invalid syntax (proper error messages)

---

## Phase 5: Polish & Documentation

### 5.1 Error Handling

Comprehensive error reporting:

```c
typedef enum {
    DSDL_OK = 0,
    DSDL_ERROR_OOM,
    DSDL_ERROR_FILE_NOT_FOUND,
    DSDL_ERROR_PARSE_ERROR,
    DSDL_ERROR_TYPE_NOT_FOUND,
    DSDL_ERROR_INVALID_VERSION,
    DSDL_ERROR_CIRCULAR_DEPENDENCY,
    DSDL_ERROR_ASSERTION_FAILED,
    DSDL_ERROR_BUFFER_TOO_SMALL,
    DSDL_ERROR_INVALID_ARRAY_LENGTH,
    DSDL_ERROR_INVALID_UNION_TAG,
    DSDL_ERROR_INVALID_DELIMITER,
} dsdl_error_t;
```

### 5.2 Memory Optimization

- Compact type representation
- String interning for field names
- Single-allocation composite instances
- Optional: memory pool for parser temporaries

### 5.3 Documentation

- Doxygen comments for public API
- Usage examples in README
- Architecture documentation

---

## Implementation Order

| Step | Description | Dependencies |
|------|-------------|--------------|
| 0.1 | CMake build system | None |
| 0.2 | Unity test framework | 0.1 |
| 0.3 | CI setup | 0.1, 0.2 |
| 1.1 | Parser foundation | 0.1 |
| 1.2 | Literal parsing | 1.1 |
| 1.3 | Expression parsing | 1.2 |
| 1.4 | Type parsing | 1.3 |
| 1.5 | Statement parsing | 1.4 |
| 1.6 | Definition parsing | 1.5 |
| 2.1 | Type resolution | 1.6 |
| 2.2 | Constant evaluation | 1.3, 2.1 |
| 2.3 | Assertion validation | 2.2 |
| 2.4 | Extent calculation | 2.1 |
| 3.1 | Bit buffer utilities | None |
| 3.2 | Primitive serialization | 3.1 |
| 3.3 | Array serialization | 3.2 |
| 3.4 | Composite serialization | 3.3, 2.4 |
| 3.5 | Deserialization | 3.4 |
| 3.6 | Public API | 3.5 |
| 4.1-4.6 | Testing | All above |
| 5.1-5.3 | Polish | All above |

---

## Risk Mitigation

| Risk | Mitigation |
|------|------------|
| Complex bit-level serialization | Extensive unit tests, cross-validation with Nunavut |
| Expression evaluation edge cases | Port PyDSDL test cases |
| Memory management complexity | Consistent single-allocation pattern, valgrind testing |
| Parser performance | Memoization if needed, benchmark against PyDSDL |
| C++ compatibility | Early C++20 test, careful header design |
| Rational overflow | Detect overflow, report error; upgrade to `_BitInt(2048)` on C23; may add arbitrary precision later |

---

## Success Criteria

1. All 433 test DSDL files parse without error
2. Serialization matches Nunavut output byte-for-byte for all test types
3. 90%+ code coverage
4. No memory leaks (valgrind clean)
5. Compiles with `-Wall -Wextra -Werror` on GCC/Clang
6. C99 compliant library code
7. Header compiles cleanly with C++20
8. No stdio or heap dependencies (only user-provided realloc)
