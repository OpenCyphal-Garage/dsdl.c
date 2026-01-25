# DSDL Invalid Test Suite

This namespace contains **79 DSDL files** that are intentionally **invalid** and **must fail to parse**. These files test error detection capabilities of DSDL parsers.

Each file is named to indicate the type of error it contains. Comments in each file describe the expected error.

All files have been verified to fail with PyDSDL 1.24.1.

## Error Categories

### 1. Invalid Primitive Types
- `Void0.0.1.dsdl` - void0 is invalid (minimum is void1)
- `Int1.0.1.dsdl` - int1 is invalid (minimum is int2)
- `Uint65.0.1.dsdl` - uint65 is invalid (maximum is uint64)
- `Int65.0.1.dsdl` - int65 is invalid (maximum is int64)
- `Void65.0.1.dsdl` - void65 is invalid (maximum is void64)
- `Float8.0.1.dsdl` - float8 is invalid (only 16, 32, 64)

### 2. Invalid Array Types
- `ArrayCapacity0.0.1.dsdl` - Array capacity 0 is invalid
- `ArrayCapacityNegative.0.1.dsdl` - Negative array capacity
- `ArrayCapacityNonInteger.0.1.dsdl` - Non-integer array capacity
- `NestedArray.0.1.dsdl` - Nested arrays are not allowed
- `VoidArray.0.1.dsdl` - Arrays of void are not allowed
- `Utf8FixedArray.0.1.dsdl` - utf8 in fixed-length array is invalid
- `Utf8Standalone.0.1.dsdl` - utf8 as standalone field is invalid
- `ByteStandalone.0.1.dsdl` - byte as standalone field is invalid

### 3. Invalid Cast Modes
- `TruncatedSigned.0.1.dsdl` - truncated on signed integer is invalid
- `TruncatedBool.0.1.dsdl` - truncated on bool is invalid
- `TruncatedComposite.0.1.dsdl` - truncated on composite type
- `SaturatedUtf8.0.1.dsdl` - saturated on utf8 is invalid
- `SaturatedByte.0.1.dsdl` - saturated on byte is invalid

### 4. Invalid Unions
- `UnionOneField.0.1.dsdl` - Union with only 1 field (minimum is 2)
- `UnionZeroFields.0.1.dsdl` - Union with no fields
- `UnionAfterField.0.1.dsdl` - @union after first field

### 5. Invalid Constants
- `BoolConstantInt.0.1.dsdl` - bool constant assigned integer
- `IntConstantBool.0.1.dsdl` - int constant assigned boolean
- `IntConstantFloat.0.1.dsdl` - int constant assigned non-integer
- `IntConstantOverflow.0.1.dsdl` - Value exceeds type range
- `CharConstantNonAscii.0.1.dsdl` - Character constant with non-ASCII
- `CharConstantWrongType.0.1.dsdl` - Character literal on wrong type
- `CompositeConstant.0.1.dsdl` - Constant of composite type
- `SetConstant.0.1.dsdl` - Constant assigned a set value

### 6. Invalid Assertions
- `AssertFalse.0.1.dsdl` - Assertion that evaluates to false
- `AssertNonBool.0.1.dsdl` - Assertion with non-boolean result
- `AssertUndefined.0.1.dsdl` - Assertion with undefined identifier

### 7. Invalid Directives
- `UnknownDirective.0.1.dsdl` - Unknown directive name
- `DeprecatedAfterField.0.1.dsdl` - @deprecated after first field
- `DeprecatedInResponse.0.1.dsdl` - @deprecated in service response
- `UnionRepeated.0.1.dsdl` - @union used twice
- `DeprecatedRepeated.0.1.dsdl` - @deprecated used twice
- `SealedRepeated.0.1.dsdl` - @sealed used twice
- `ExtentRepeated.0.1.dsdl` - @extent used twice
- `AssertNoExpr.0.1.dsdl` - @assert without expression
- `UnionWithExpr.0.1.dsdl` - @union with expression
- `DeprecatedWithExpr.0.1.dsdl` - @deprecated with expression
- `SealedWithExpr.0.1.dsdl` - @sealed with expression
- `ExtentNoExpr.0.1.dsdl` - @extent without expression
- `FieldAfterExtent.0.1.dsdl` - Field defined after @extent
- `FieldAfterSealed.0.1.dsdl` - Field defined after @sealed

### 8. Invalid Extent/Sealing
- `SealedAndExtent.0.1.dsdl` - Both @sealed and @extent
- `ExtentTooSmall.0.1.dsdl` - Extent smaller than type size
- `ExtentNonInteger.0.1.dsdl` - Extent with non-integer value
- `ExtentNegative.0.1.dsdl` - Negative extent value
- `NoSealingOrExtent.0.1.dsdl` - Missing both @sealed and @extent

### 9. Invalid Version Numbers
- `Version0_0.0.0.dsdl` - Version 0.0 is invalid
- `VersionTooLarge.256.0.dsdl` - Major version > 255
- `VersionMinorTooLarge.0.256.dsdl` - Minor version > 255

### 10. Invalid Type References
- `UndefinedType.0.1.dsdl` - Reference to non-existent type
- `UndefinedConstant.0.1.dsdl` - Reference to non-existent constant
- `CircularDepA.0.1.dsdl` - Circular dependency (with B)
- `CircularDepB.0.1.dsdl` - Circular dependency (with A)

### 11. Invalid Service Definitions
- `ServiceTripleMarker.0.1.dsdl` - Three service markers
- `ServiceMissingSealing.0.1.dsdl` - Service part without seal/extent

### 12. Deprecated Type Usage
- `UsesDeprecated.0.1.dsdl` - Non-deprecated uses deprecated type
- `DeprecatedHelper.0.1.dsdl` - Helper deprecated type

### 13. Invalid Expressions
- `DivisionByZero.0.1.dsdl` - Division by zero
- `InvalidOperands.0.1.dsdl` - Type mismatch in operation
- `UndefinedAttribute.0.1.dsdl` - Access to undefined attribute

### 14. Syntax Errors
- `SyntaxMissingType.0.1.dsdl` - Missing field type
- `SyntaxBadArraySyntax.0.1.dsdl` - Invalid array syntax (type after brackets)

### 15. Void Field Naming
- `VoidNamed.0.1.dsdl` - Void field with a name

### 16. Expression Type Errors
- `LogicalAndTypeMismatch.0.1.dsdl` - && with non-boolean operand
- `LogicalOrTypeMismatch.0.1.dsdl` - || with non-boolean operand
- `LogicalNotTypeMismatch.0.1.dsdl` - ! with non-boolean operand
- `UnaryPlusBool.0.1.dsdl` - + with boolean operand
- `UnaryMinusString.0.1.dsdl` - - with string operand
- `BitwiseOrBool.0.1.dsdl` - | on booleans (not integers/sets)
- `BitwiseAndBool.0.1.dsdl` - & on booleans (not integers/sets)

### 17. Set Errors
- `SetDivision.0.1.dsdl` - Division between sets
- `EmptySetComparison.0.1.dsdl` - Empty set in expression
- `HeterogeneousSet.0.1.dsdl` - Set with mixed types
- `SetAttributeNonexistent.0.1.dsdl` - Invalid set attribute

### 18. Name Collisions
- `AttributeNameCollision.0.1.dsdl` - Duplicate attribute name

### 19. Union Offset Access
- `OffsetInUnionBefore.0.1.dsdl` - _offset_ accessed before first union field

### 20. Service Type Attribute Access
- `ServiceBitLengthAccess.0.1.dsdl` - Helper service type
- `ServiceBitLengthRef.0.1.dsdl` - _bit_length_ on service type

## Usage

A DSDL parser should reject all files in this namespace. The parser test suite should verify that:

1. Each file produces a parse error
2. The error type/message is appropriate for the specific issue
3. Line numbers in error messages are correct

## Validation

To verify these files are truly invalid, run PyDSDL:

```python
import pydsdl
for file in invalid_files:
    try:
        pydsdl.read_namespace('invalid')
        assert False, f"{file} should have failed"
    except pydsdl.InvalidDefinitionError:
        pass  # Expected
```
