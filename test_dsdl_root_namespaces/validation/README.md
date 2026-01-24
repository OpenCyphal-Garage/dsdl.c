# DSDL Validation Test Suite

This namespace contains a comprehensive validation dataset designed to test **all** features of the DSDL language as defined in the Cyphal Specification. The types here cover every grammar rule, every semantic feature, and numerous edge cases including size extremes.

## Feature Coverage Checklist

### 1. Void Types (void1 - void64)

| Feature | Test File(s) | Status |
|---------|--------------|--------|
| void1 (minimum) | `Void.0.1.dsdl` | |
| void64 (maximum) | `Void.0.1.dsdl` | |
| void with various bit lengths (1-64) | `Void.0.1.dsdl` | |
| Void as padding between fields | `Padding.0.1.dsdl` | |
| Multiple consecutive void fields | `Padding.0.1.dsdl` | |

### 2. Primitive Types

#### 2.1 Boolean
| Feature | Test File(s) | Status |
|---------|--------------|--------|
| bool field | `Primitives.0.1.dsdl` | |
| bool constant (true) | `Constants.0.1.dsdl` | |
| bool constant (false) | `Constants.0.1.dsdl` | |
| bool in expressions | `Expressions.0.1.dsdl` | |

#### 2.2 Unsigned Integers (uint1 - uint64)
| Feature | Test File(s) | Status |
|---------|--------------|--------|
| uint1 (minimum width) | `Primitives.0.1.dsdl` | |
| uint64 (maximum width) | `Primitives.0.1.dsdl` | |
| All uint widths 1-64 | `AllUints.0.1.dsdl` | |
| Saturated uint (default) | `CastModes.0.1.dsdl` | |
| Truncated uint | `CastModes.0.1.dsdl` | |
| uint constant | `Constants.0.1.dsdl` | |

#### 2.3 Signed Integers (int2 - int64)
| Feature | Test File(s) | Status |
|---------|--------------|--------|
| int2 (minimum width) | `Primitives.0.1.dsdl` | |
| int64 (maximum width) | `Primitives.0.1.dsdl` | |
| All int widths 2-64 | `AllInts.0.1.dsdl` | |
| Saturated int (default) | `CastModes.0.1.dsdl` | |
| Negative int constants | `Constants.0.1.dsdl` | |

#### 2.4 Floating Point (float16, float32, float64)
| Feature | Test File(s) | Status |
|---------|--------------|--------|
| float16 | `Primitives.0.1.dsdl` | |
| float32 | `Primitives.0.1.dsdl` | |
| float64 | `Primitives.0.1.dsdl` | |
| Saturated float (default) | `CastModes.0.1.dsdl` | |
| Truncated float | `CastModes.0.1.dsdl` | |
| Float constant from integer | `Constants.0.1.dsdl` | |
| Float constant from real literal | `Constants.0.1.dsdl` | |

#### 2.5 Special Types
| Feature | Test File(s) | Status |
|---------|--------------|--------|
| byte (uint8 alias for arrays) | `ByteArrays.0.1.dsdl` | |
| utf8 (for UTF-8 string arrays) | `StringArrays.0.1.dsdl` | |

### 3. Cast Modes

| Feature | Test File(s) | Status |
|---------|--------------|--------|
| `saturated` keyword (explicit) | `CastModes.0.1.dsdl` | |
| Default saturation (no keyword) | `CastModes.0.1.dsdl` | |
| `truncated` keyword on uint | `CastModes.0.1.dsdl` | |
| `truncated` keyword on float | `CastModes.0.1.dsdl` | |

### 4. Array Types

#### 4.1 Fixed-Length Arrays
| Feature | Test File(s) | Status |
|---------|--------------|--------|
| Fixed array of primitives | `Arrays.0.1.dsdl` | |
| Fixed array of composites | `Arrays.0.1.dsdl` | |
| Fixed array size 1 | `Arrays.0.1.dsdl` | |
| Fixed array size 1000 (large) | `LargeArrays.0.1.dsdl` | |
| Nested composites in fixed array | `NestedArrays.0.1.dsdl` | |

#### 4.2 Variable-Length Arrays (Inclusive `<=`)
| Feature | Test File(s) | Status |
|---------|--------------|--------|
| Variable array `[<=N]` | `Arrays.0.1.dsdl` | |
| Capacity 1 (1-bit length prefix, rounded to 8) | `Arrays.0.1.dsdl` | |
| Capacity 255 (8-bit length prefix) | `Arrays.0.1.dsdl` | |
| Capacity 256 (16-bit length prefix) | `Arrays.0.1.dsdl` | |
| Capacity 65535 (16-bit length prefix) | `LargeArrays.0.1.dsdl` | |
| Capacity 65536 (32-bit length prefix) | `LargeArrays.0.1.dsdl` | |
| Capacity 1000 elements | `LargeArrays.0.1.dsdl` | |
| byte arrays (raw data) | `ByteArrays.0.1.dsdl` | |
| utf8 arrays (UTF-8 strings) | `StringArrays.0.1.dsdl` | |

#### 4.3 Variable-Length Arrays (Exclusive `<`)
| Feature | Test File(s) | Status |
|---------|--------------|--------|
| Variable array `[<N]` | `Arrays.0.1.dsdl` | |
| `[<2]` (capacity 1) | `Arrays.0.1.dsdl` | |
| `[<256]` (capacity 255) | `Arrays.0.1.dsdl` | |
| `[<257]` (capacity 256, 16-bit prefix) | `Arrays.0.1.dsdl` | |

### 5. Composite Types

#### 5.1 Structures (Non-Union)
| Feature | Test File(s) | Status |
|---------|--------------|--------|
| Empty structure (no fields) | `Empty.0.1.dsdl` | |
| Single field structure | `Simple.0.1.dsdl` | |
| Multiple fields | `Struct.0.1.dsdl` | |
| Structure with 1000 fields | `HugeStruct.0.1.dsdl` | |
| Mixed primitive types | `Struct.0.1.dsdl` | |
| Nested structures | `Nested.0.1.dsdl` | |
| Deeply nested (10+ levels) | `DeepNesting.0.1.dsdl` | |

#### 5.2 Tagged Unions
| Feature | Test File(s) | Status |
|---------|--------------|--------|
| Union with 2 fields (min) | `Union.0.1.dsdl` | |
| Union with 255 fields (8-bit tag) | `LargeUnion.0.1.dsdl` | |
| Union with 256 fields (16-bit tag) | `LargeUnion.0.1.dsdl` | |
| Union with heterogeneous types | `Union.0.1.dsdl` | |
| Union with nested composites | `Union.0.1.dsdl` | |
| Union with arrays | `Union.0.1.dsdl` | |

#### 5.3 Services
| Feature | Test File(s) | Status |
|---------|--------------|--------|
| Service with request and response | `Service.0.1.dsdl` | |
| Service with empty request | `Service.0.1.dsdl` | |
| Service with empty response | `Service.0.1.dsdl` | |
| Service with union in request | `ServiceUnion.0.1.dsdl` | |
| Service with union in response | `ServiceUnion.0.1.dsdl` | |
| Service response marker `---` | `Service.0.1.dsdl` | |
| Service response marker `----` (multiple dashes) | `Service.0.1.dsdl` | |

#### 5.4 Sealed vs Delimited
| Feature | Test File(s) | Status |
|---------|--------------|--------|
| `@sealed` directive | `Sealed.0.1.dsdl` | |
| `@extent` directive | `Delimited.0.1.dsdl` | |
| Extent with expression | `Delimited.0.1.dsdl` | |
| Extent using `_offset_` | `Delimited.0.1.dsdl` | |
| Sealed nesting delimited | `MixedSealing.0.1.dsdl` | |
| Delimited nesting sealed | `MixedSealing.0.1.dsdl` | |
| Delimiter header serialization | `Delimited.0.1.dsdl` | |

### 6. Directives

| Feature | Test File(s) | Status |
|---------|--------------|--------|
| `@union` | `Union.0.1.dsdl` | |
| `@sealed` | `Sealed.0.1.dsdl` | |
| `@extent <expression>` | `Delimited.0.1.dsdl` | |
| `@deprecated` | `Deprecated.0.1.dsdl` | |
| `@assert <bool_expr>` | `Assertions.0.1.dsdl` | |
| `@print <expression>` | `Print.0.1.dsdl` | |
| `@print` (no expression) | `Print.0.1.dsdl` | |

### 7. Literals

#### 7.1 Integer Literals
| Feature | Test File(s) | Status |
|---------|--------------|--------|
| Decimal: `123` | `Literals.0.1.dsdl` | |
| Decimal with underscores: `1_000_000` | `Literals.0.1.dsdl` | |
| Binary: `0b1010` | `Literals.0.1.dsdl` | |
| Binary: `0B1010` (uppercase B) | `Literals.0.1.dsdl` | |
| Binary with underscores: `0b1010_1010` | `Literals.0.1.dsdl` | |
| Octal: `0o777` | `Literals.0.1.dsdl` | |
| Octal: `0O777` (uppercase O) | `Literals.0.1.dsdl` | |
| Octal with underscores: `0o7_7_7` | `Literals.0.1.dsdl` | |
| Hexadecimal: `0xFF` | `Literals.0.1.dsdl` | |
| Hexadecimal: `0XFF` (uppercase X) | `Literals.0.1.dsdl` | |
| Hexadecimal with underscores: `0xFF_FF` | `Literals.0.1.dsdl` | |
| Zero: `0` | `Literals.0.1.dsdl` | |
| Large integer | `Literals.0.1.dsdl` | |

#### 7.2 Real Literals
| Feature | Test File(s) | Status |
|---------|--------------|--------|
| Point notation: `3.14` | `Literals.0.1.dsdl` | |
| Point notation: `.5` (no integer part) | `Literals.0.1.dsdl` | |
| Point notation: `5.` (no fraction) | `Literals.0.1.dsdl` | |
| Exponent notation: `1e10` | `Literals.0.1.dsdl` | |
| Exponent notation: `1E10` | `Literals.0.1.dsdl` | |
| Exponent with sign: `1e+10` | `Literals.0.1.dsdl` | |
| Exponent with sign: `1e-10` | `Literals.0.1.dsdl` | |
| Combined: `1.5e10` | `Literals.0.1.dsdl` | |
| Underscores in real: `1_000.5` | `Literals.0.1.dsdl` | |

#### 7.3 Boolean Literals
| Feature | Test File(s) | Status |
|---------|--------------|--------|
| `true` | `Literals.0.1.dsdl` | |
| `false` | `Literals.0.1.dsdl` | |

#### 7.4 String Literals
| Feature | Test File(s) | Status |
|---------|--------------|--------|
| Single-quoted: `'hello'` | `Literals.0.1.dsdl` | |
| Double-quoted: `"hello"` | `Literals.0.1.dsdl` | |
| Empty string: `''` | `Literals.0.1.dsdl` | |
| Escape: `\\` (backslash) | `Literals.0.1.dsdl` | |
| Escape: `\'` (single quote) | `Literals.0.1.dsdl` | |
| Escape: `\"` (double quote) | `Literals.0.1.dsdl` | |
| Escape: `\n` (newline) | `Literals.0.1.dsdl` | |
| Escape: `\r` (carriage return) | `Literals.0.1.dsdl` | |
| Escape: `\t` (tab) | `Literals.0.1.dsdl` | |
| String with 1000 characters | `LargeStrings.0.1.dsdl` | |
| Unicode in string | `Literals.0.1.dsdl` | |

#### 7.5 Set Literals
| Feature | Test File(s) | Status |
|---------|--------------|--------|
| Set with one element: `{1}` | `Sets.0.1.dsdl` | |
| Set with multiple elements: `{1, 2, 3}` | `Sets.0.1.dsdl` | |
| Set with 1000 elements | `LargeSets.0.1.dsdl` | |
| Set of booleans: `{true, false}` | `Sets.0.1.dsdl` | |
| Set in comparison: `x == {0}` | `Sets.0.1.dsdl` | |
| Empty set behavior (via `_offset_`) | `Sets.0.1.dsdl` | |

### 8. Expressions

#### 8.1 Arithmetic Operators
| Feature | Test File(s) | Status |
|---------|--------------|--------|
| Addition: `+` | `Expressions.0.1.dsdl` | |
| Subtraction: `-` | `Expressions.0.1.dsdl` | |
| Multiplication: `*` | `Expressions.0.1.dsdl` | |
| Division: `/` | `Expressions.0.1.dsdl` | |
| Modulo: `%` | `Expressions.0.1.dsdl` | |
| Power: `**` | `Expressions.0.1.dsdl` | |
| Power with integer exponent | `Expressions.0.1.dsdl` | |
| Unary plus: `+x` | `Expressions.0.1.dsdl` | |
| Unary minus: `-x` | `Expressions.0.1.dsdl` | |

#### 8.2 Comparison Operators
| Feature | Test File(s) | Status |
|---------|--------------|--------|
| Equal: `==` | `Expressions.0.1.dsdl` | |
| Not equal: `!=` | `Expressions.0.1.dsdl` | |
| Less than: `<` | `Expressions.0.1.dsdl` | |
| Less or equal: `<=` | `Expressions.0.1.dsdl` | |
| Greater than: `>` | `Expressions.0.1.dsdl` | |
| Greater or equal: `>=` | `Expressions.0.1.dsdl` | |

#### 8.3 Logical Operators
| Feature | Test File(s) | Status |
|---------|--------------|--------|
| Logical AND: `&&` | `Expressions.0.1.dsdl` | |
| Logical OR: `\|\|` | `Expressions.0.1.dsdl` | |
| Logical NOT: `!` | `Expressions.0.1.dsdl` | |

#### 8.4 Bitwise Operators
| Feature | Test File(s) | Status |
|---------|--------------|--------|
| Bitwise AND: `&` | `Expressions.0.1.dsdl` | |
| Bitwise OR: `\|` | `Expressions.0.1.dsdl` | |
| Bitwise XOR: `^` | `Expressions.0.1.dsdl` | |

#### 8.5 Operator Precedence
| Feature | Test File(s) | Status |
|---------|--------------|--------|
| `**` binds tighter than `*` | `Precedence.0.1.dsdl` | |
| `*` binds tighter than `+` | `Precedence.0.1.dsdl` | |
| Parentheses override precedence | `Precedence.0.1.dsdl` | |
| Right associativity of `**` | `Precedence.0.1.dsdl` | |
| Left associativity of `+`, `-` | `Precedence.0.1.dsdl` | |
| Complex nested expressions | `Precedence.0.1.dsdl` | |

#### 8.6 Set Operations
| Feature | Test File(s) | Status |
|---------|--------------|--------|
| Set equality: `==` | `SetOperations.0.1.dsdl` | |
| Set inequality: `!=` | `SetOperations.0.1.dsdl` | |
| Subset: `<=` | `SetOperations.0.1.dsdl` | |
| Superset: `>=` | `SetOperations.0.1.dsdl` | |
| Proper subset: `<` | `SetOperations.0.1.dsdl` | |
| Proper superset: `>` | `SetOperations.0.1.dsdl` | |
| Set union: `\|` | `SetOperations.0.1.dsdl` | |
| Set intersection: `&` | `SetOperations.0.1.dsdl` | |
| Disjunctive union: `^` | `SetOperations.0.1.dsdl` | |
| Elementwise arithmetic on sets | `SetOperations.0.1.dsdl` | |
| `.min` attribute | `SetOperations.0.1.dsdl` | |
| `.max` attribute | `SetOperations.0.1.dsdl` | |
| `.count` attribute | `SetOperations.0.1.dsdl` | |

#### 8.7 String Operations
| Feature | Test File(s) | Status |
|---------|--------------|--------|
| String concatenation: `+` | `StringOps.0.1.dsdl` | |
| String equality: `==` | `StringOps.0.1.dsdl` | |
| String inequality: `!=` | `StringOps.0.1.dsdl` | |
| String to uint8 (ASCII char) | `StringOps.0.1.dsdl` | |

### 9. Attributes

#### 9.1 Constant Attributes
| Feature | Test File(s) | Status |
|---------|--------------|--------|
| uint constant | `Constants.0.1.dsdl` | |
| int constant | `Constants.0.1.dsdl` | |
| float constant | `Constants.0.1.dsdl` | |
| bool constant | `Constants.0.1.dsdl` | |
| Constant from expression | `Constants.0.1.dsdl` | |
| Constant referencing prior constant | `Constants.0.1.dsdl` | |
| Constant from string (ASCII to uint8) | `Constants.0.1.dsdl` | |

#### 9.2 Field Attributes
| Feature | Test File(s) | Status |
|---------|--------------|--------|
| Primitive field | `Struct.0.1.dsdl` | |
| Array field | `Arrays.0.1.dsdl` | |
| Composite field | `Nested.0.1.dsdl` | |
| Padding field (void) | `Padding.0.1.dsdl` | |

#### 9.3 Intrinsic Attributes
| Feature | Test File(s) | Status |
|---------|--------------|--------|
| `_offset_` basic usage | `Offset.0.1.dsdl` | |
| `_offset_` as set | `Offset.0.1.dsdl` | |
| `_offset_.min` | `Offset.0.1.dsdl` | |
| `_offset_.max` | `Offset.0.1.dsdl` | |
| `_offset_` in unions | `UnionOffset.0.1.dsdl` | |
| `_offset_` alignment checking | `Offset.0.1.dsdl` | |

### 10. Type References

| Feature | Test File(s) | Status |
|---------|--------------|--------|
| Full qualified name | `References.0.1.dsdl` | |
| Short name (same namespace) | `subns/ShortRef.0.1.dsdl` | |
| Multi-level namespace | `subns/deep/DeepRef.0.1.dsdl` | |
| Type constant access | `References.0.1.dsdl` | |
| Type in expression context | `References.0.1.dsdl` | |

### 11. Comments

| Feature | Test File(s) | Status |
|---------|--------------|--------|
| Line comment `#` | `Comments.0.1.dsdl` | |
| Comment at end of line | `Comments.0.1.dsdl` | |
| Comment-only lines | `Comments.0.1.dsdl` | |
| Unicode in comments | `Comments.0.1.dsdl` | |
| Comment with special characters | `Comments.0.1.dsdl` | |

### 12. Whitespace and Formatting

| Feature | Test File(s) | Status |
|---------|--------------|--------|
| Spaces as separators | `Whitespace.0.1.dsdl` | |
| Tabs as separators | `Whitespace.0.1.dsdl` | |
| Multiple whitespace | `Whitespace.0.1.dsdl` | |
| No whitespace around operators | `Whitespace.0.1.dsdl` | |
| Excessive whitespace | `Whitespace.0.1.dsdl` | |
| Empty lines | `Whitespace.0.1.dsdl` | |
| Unix line endings (LF) | `Whitespace.0.1.dsdl` | |
| Windows line endings (CRLF) | `WhitespaceCRLF.0.1.dsdl` | |

### 13. Identifiers and Naming

| Feature | Test File(s) | Status |
|---------|--------------|--------|
| Lowercase identifier | `Identifiers.0.1.dsdl` | |
| Uppercase identifier | `Identifiers.0.1.dsdl` | |
| Mixed case identifier | `Identifiers.0.1.dsdl` | |
| Identifier with underscore | `Identifiers.0.1.dsdl` | |
| Identifier starting with underscore | `Identifiers.0.1.dsdl` | |
| Long identifier (100+ chars) | `Identifiers.0.1.dsdl` | |
| Single character identifier | `Identifiers.0.1.dsdl` | |

### 14. Serialization Edge Cases

| Feature | Test File(s) | Status |
|---------|--------------|--------|
| Bit-level field alignment | `BitAlignment.0.1.dsdl` | |
| Cross-byte field packing | `BitAlignment.0.1.dsdl` | |
| Final padding to byte boundary | `BitAlignment.0.1.dsdl` | |
| Union tag sizes (8, 16, 32 bits) | `UnionTags.0.1.dsdl` | |
| Array length prefix sizes | `ArrayPrefixes.0.1.dsdl` | |
| Delimiter header (32-bit) | `Delimited.0.1.dsdl` | |

### 15. Size Edge Cases

| Feature | Test File(s) | Status |
|---------|--------------|--------|
| Empty type (0 fields) | `Empty.0.1.dsdl` | |
| Single-bit type (bool only) | `MinimalSize.0.1.dsdl` | |
| Type with 1000 fields | `HugeStruct.0.1.dsdl` | |
| Deeply nested (10+ levels) | `DeepNesting.0.1.dsdl` | |
| Large array (65535 elements) | `LargeArrays.0.1.dsdl` | |
| Maximum extent value | `MaxExtent.0.1.dsdl` | |
| Complex variable-size type | `ComplexSize.0.1.dsdl` | |

### 16. Version Numbers

| Feature | Test File(s) | Status |
|---------|--------------|--------|
| Version 0.1 | All files use various versions | |
| Version 1.0 | `Versioned.1.0.dsdl` | |
| Version 255.255 (maximum) | `Versioned.255.255.dsdl` | |
| Multiple versions of same type | `Versioned.0.1.dsdl`, `Versioned.1.0.dsdl` | |

### 17. Expression Type Coercions

| Feature | Test File(s) | Status |
|---------|--------------|--------|
| Integer to rational | `Coercion.0.1.dsdl` | |
| Real to rational | `Coercion.0.1.dsdl` | |
| Rational to integer constant | `Coercion.0.1.dsdl` | |
| Rational to float constant | `Coercion.0.1.dsdl` | |
| String char to uint8 | `Coercion.0.1.dsdl` | |

### 18. Rational Arithmetic Precision

| Feature | Test File(s) | Status |
|---------|--------------|--------|
| Exact integer arithmetic | `RationalPrecision.0.1.dsdl` | |
| Exact fractional arithmetic | `RationalPrecision.0.1.dsdl` | |
| Large numerator/denominator | `RationalPrecision.0.1.dsdl` | |
| Division producing fraction | `RationalPrecision.0.1.dsdl` | |
| Modulo with fractions | `RationalPrecision.0.1.dsdl` | |

---

## File Organization

**71 DSDL files total** - All validated with PyDSDL 1.24.1

```
validation/
├── README.md                     # This file
├── Empty.0.1.dsdl               # Empty types
├── Primitives.0.1.dsdl          # All primitive types
├── AllUints.0.1.dsdl            # All uint widths 1-64
├── AllInts.0.1.dsdl             # All int widths 2-64
├── CastModes.0.1.dsdl           # Saturated and truncated
├── AllVoids.0.1.dsdl            # All void widths 1-64
├── Padding.0.1.dsdl             # Padding field usage
├── Arrays.0.1.dsdl              # Fixed and variable arrays
├── LargeArrays.0.1.dsdl         # Large array capacities
├── ByteArrays.0.1.dsdl          # byte[] arrays for raw binary data
├── StringArrays.0.1.dsdl        # utf8[] arrays for UTF-8 strings
├── NestedArrays.0.1.dsdl        # Arrays of composites
├── BasicStruct.0.1.dsdl         # Basic structures (renamed from Struct)
├── HugeStruct.0.1.dsdl          # Structure with 1000 fields
├── Union.0.1.dsdl               # Basic unions
├── LargeUnion.0.1.dsdl          # Union with 260 fields (16-bit tag)
├── Nested.0.1.dsdl              # Nested composites
├── DeepNesting.0.1.dsdl         # Deeply nested types (10 levels)
├── Level1.0.1.dsdl ... Level10.0.1.dsdl  # Nesting levels
├── Service.0.1.dsdl             # Service types
├── ServiceEmpty.0.1.dsdl        # Service with empty req/resp
├── ServiceUnion.0.1.dsdl        # Service with unions
├── Sealed.0.1.dsdl              # Sealed types
├── Delimited.0.1.dsdl           # Delimited types with extent
├── DelimitedOffset.0.1.dsdl     # Extent using _offset_
├── MixedSealing.0.1.dsdl        # Mixed sealed/delimited nesting
├── Constants.0.1.dsdl           # Constant attributes
├── Literals.0.1.dsdl            # All literal types
├── LargeStrings.0.1.dsdl        # Large string literals (500+ chars)
├── Sets.0.1.dsdl                # Set literals
├── LargeSets.0.1.dsdl           # Large set literals (50 elements)
├── Expressions.0.1.dsdl         # Expression operators
├── Precedence.0.1.dsdl          # Operator precedence
├── SetOperations.0.1.dsdl       # Set-specific operations
├── StringOps.0.1.dsdl           # String operations
├── Offset.0.1.dsdl              # _offset_ usage
├── UnionOffset.0.1.dsdl         # _offset_ in unions
├── Assertions.0.1.dsdl          # @assert directive
├── Print.0.1.dsdl               # @print directive
├── Deprecated.0.1.dsdl          # @deprecated directive
├── DeprecatedService.0.1.dsdl   # Deprecated service
├── References.0.1.dsdl          # Type references
├── Comments.0.1.dsdl            # Comment syntax
├── Whitespace.0.1.dsdl          # Whitespace handling
├── WhitespaceCRLF.0.1.dsdl      # Windows line endings
├── Identifiers.0.1.dsdl         # Identifier naming
├── BitAlignment.0.1.dsdl        # Bit-level alignment
├── UnionTags.0.1.dsdl           # Union tag sizes
├── ArrayPrefixes.0.1.dsdl       # Array length prefix sizes
├── MinimalSize.0.1.dsdl         # Minimal serialized size
├── MaxExtent.0.1.dsdl           # Maximum extent
├── ComplexSize.0.1.dsdl         # Complex variable sizes
├── Versioned.0.1.dsdl           # Version 0.1
├── Versioned.1.0.dsdl           # Version 1.0
├── Versioned.255.255.dsdl       # Maximum version
├── Coercion.0.1.dsdl            # Type coercions
├── RationalPrecision.0.1.dsdl   # Rational arithmetic
├── Simple.0.1.dsdl              # Simple single-field
├── subns/                       # Subdirectory for namespace tests
│   ├── Helper.0.1.dsdl          # Helper type in subnamespace
│   ├── ShortRef.0.1.dsdl        # Short name references
│   └── deep/
│       ├── DeepHelper.0.1.dsdl  # Helper in deep namespace
│       └── DeepRef.0.1.dsdl     # Deep namespace nesting
```

## Implementation Notes

1. **All uint widths (1-64)**: The `AllUints.0.1.dsdl` file defines fields for every uint width from uint1 to uint64.

2. **All int widths (2-64)**: The `AllInts.0.1.dsdl` file defines fields for every int width from int2 to int64 (int1 is invalid per spec).

3. **All void widths (1-64)**: The `Void.0.1.dsdl` file includes void fields of all widths from void1 to void64.

4. **1000-field structure**: `HugeStruct.0.1.dsdl` contains exactly 1000 field declarations to test parser and serialization performance.

5. **256-field union**: `LargeUnion.0.1.dsdl` contains enough fields to require a 16-bit union tag.

6. **1000-element set**: `LargeSets.0.1.dsdl` defines sets with 1000 elements to test set handling.

7. **1000-character string**: `LargeStrings.0.1.dsdl` uses string constants with 1000+ characters.

8. **Deep nesting**: `DeepNesting.0.1.dsdl` creates a chain of 10+ nested composite types.

9. **Cross-validation**: Each type should be tested against PyDSDL and Nunavut-generated code.

## Expected Test Outcomes

For each DSDL file in this namespace:

1. **Parse successfully**: The parser must accept all valid definitions without error.
2. **Correct AST**: The parsed AST must match expected structure.
3. **Correct serialization footprint**: Min/max bit lengths must be calculated correctly.
4. **Correct serialization**: Serialized output must match Nunavut byte-for-byte.
5. **Correct deserialization**: Deserialized values must match original input.
6. **Round-trip integrity**: serialize(deserialize(serialize(x))) == serialize(x).
