# Semantic Analysis - Missing Features Plan

Based on the implementation plan (docs/IMPLEMENTATION_PLAN.md) and current code state in dsdl.c.

## Missing Semantic Analysis Features

### 1. **Constant Evaluation** (Phase 2.2 - Not Implemented)

Currently constants are **parsed and stored** but **not evaluated**. Looking at dsdl.c:3199-3208:

```c
case dsdl_stmt_constant: {
    const size_t idx = out_def->const_count++;
    out_def->const_values[idx] = stmt.value;  // Stores raw AST, not evaluated!
    ...
}
```

**Missing:**
- Evaluate constant expressions that reference other constants:
  - `PIXELS_PER_IMAGE = PIXELS_PER_ROW * ROWS_PER_IMAGE`
  - `CONSTANT_TRUTH = CONSTANT_MINUS_THREE < 0`
- Evaluation context tracking (previously defined constants)
- Constant expressions are currently stored as parsed AST values, not evaluated rationals/bools

**Test Examples:**
- `test_dsdl_root_namespaces/nunavut_test_types/test0/regulated/RGB888_3840x2748.0.1.dsdl`
- `test_dsdl_root_namespaces/nunavut_test_types/test0/regulated/basics/7000.Struct_.0.1.dsdl`

### 2. **Type Attribute Access** (Not Implemented)

Line 2450 shows: `// TODO: Implement other attribute access (e.g., type attributes)`

**Missing:**
- Access to type constants: `Simple.0.1.VALUE`
- Access to type attributes: `Type._extent_`, `Type._bit_length_`
- These appear in test files like `validation/ComplexRefExpr.0.1.dsdl`:
  ```
  @assert Simple.0.1.VALUE == 42
  @assert Simple.0.1._extent_ == 32
  uint8[Simple.0.1.VALUE / 7] expr_capacity_array
  ```

**Implementation Notes:**
- Need to parse type names in expressions (already done, stored as dsdl_value_type)
- Need to resolve type names to actual type descriptors
- Need to access constants and attributes from resolved types
- Attributes needed: `_extent_`, `_bit_length_`

**Test Examples:**
- `test_dsdl_root_namespaces/validation/ComplexRefExpr.0.1.dsdl`
- `test_dsdl_root_namespaces/reg/udral/physics/kinematics/geodetic/Point.0.1.dsdl`

### 3. **Constant Storage in Type Descriptors** (Not Implemented)

The `dsdl_type_composite_t` structure has no fields for constants. Constants are parsed but **discarded** after parsing - they're not stored in the final type descriptor.

**Missing:**
- Add constant fields to the composite type structure
- Store evaluated constants in the type descriptor
- Make constants accessible via public API

**Required Changes to dsdl.h:**
```c
typedef struct dsdl_type_composite_t {
    // ... existing fields ...

    size_t        constant_count;  // Number of constants
    wkv_str_t*    constant_names;  // Array of constant names
    dsdl_value_t* constant_values; // Array of evaluated constant values (rationals/bools/strings)

    // ... rest of fields ...
} dsdl_type_composite_t;
```

**Implementation:**
- Include constants in size calculation during single-allocation
- Copy constant names and values into the composite structure
- Constants must be evaluated before storage

### 4. **Service Response Type Creation** (Not Implemented)

Services parse request/response fields separately, but the response type is never created:
- `composite->response = NULL;` (line 4059) - always NULL
- Response fields are parsed into `def.response_field_*` arrays
- Response assertions are tracked in `def.assert_in_response`
- But no response composite type is created

**Missing:**
- Create a separate `dsdl_type_composite_t` for the response
- Recursively create type descriptors for response fields
- Process response-section assertions (_offset_ for response)
- Store in `composite->response`

**Implementation Notes:**
- Response type should have its own name (e.g., "TypeName.Response")
- Response assertions need separate _offset_ tracking
- Response extent and sealed directives apply to response only
- May need separate allocation or include in main block

**Test Examples:**
- `test_dsdl_root_namespaces/nunavut_test_types/test0/regulated/basics/300.Service.0.1.dsdl`
- `test_dsdl_root_namespaces/zubax/low_level_io/Access.0.1.dsdl`
- `test_dsdl_root_namespaces/uavcan/pnp/cluster/391.RequestVote.1.0.dsdl`

### 5. **Fixed Port ID Directive** (Not Implemented)

The header defines `uint16_t fixed_port_id` in `dsdl_type_composite_t` but there's no parsing for `@fixed-port-id` directive.

**Missing:**
- Parse `@fixed-port-id <expression>` directive in dsdl_parse_definition
- Evaluate the expression (must be integer constant)
- Store in `composite->fixed_port_id`
- Initialize to `DSDL_FIXED_PORT_ID_NONE` (0xFFFF) if not specified

**Note:** The fixed-port-id directive is only valid for message types (not service types or nested types).

---

## Current Semantic Analysis Status

**Implemented:**
- ✅ Type resolution and dependency loading
- ✅ Assertion validation (`@assert` with `_offset_`)
- ✅ Extent validation (max size vs declared extent)
- ✅ Sealed type validation (extent must equal max size)
- ✅ Symbolic bit length set computation
- ✅ `_offset_` tracking during parsing
- ✅ `_offset_.min`, `_offset_.max` attribute access

**Not Implemented:**
- ❌ Constant expression evaluation
- ❌ Type constant/attribute access (e.g., `Type.CONSTANT`, `Type._extent_`)
- ❌ Constant storage in type descriptors
- ❌ Service response type creation
- ❌ Fixed port ID directive parsing

---

## Implementation Priority Order

Based on test coverage and dependency chain:

### Priority 1: **Constant Evaluation**
- Critical for expressions in assertions, array sizes, and extent
- Required before type attribute access
- Many test files use constant references

### Priority 2: **Type Attribute Access**
- Required for cross-type constant references
- Depends on constant evaluation
- Required for advanced assertions

### Priority 3: **Response Type Creation**
- Required for service types to work
- Independent of constant evaluation
- Can be done in parallel with Priority 1-2

### Priority 4: **Constant Storage**
- Required for constants to be accessible via API
- Required for type attribute access
- Depends on constant evaluation

### Priority 5: **Fixed Port ID**
- Lower priority, mainly metadata
- Independent of other features
- Simple to implement

---

## Implementation Notes

### Constant Evaluation Context

Need an evaluation context that tracks:
```c
typedef struct {
    dsdl_t*              dsdl;           // For type lookups
    wkv_str_t            current_namespace; // For relative type refs
    const dsdl_value_t*  constants;      // Previously defined constants
    const wkv_str_t*     const_names;    // Constant names
    size_t               const_count;    // Number of constants
    dsdl_bls_t*          offset;         // Current _offset_ (for assertions)
} dsdl_eval_context_t;
```

### Evaluation Function

```c
static bool dsdl_eval_constant_expr(
    dsdl_eval_context_t* ctx,
    const dsdl_value_t*  expr,
    dsdl_value_t*        result
);
```

This function should:
1. Handle already-evaluated values (rational, bool, string)
2. Resolve identifiers to constants
3. Resolve type references and access attributes
4. Apply operators recursively
5. Validate types and report errors

---

## Files to Modify

1. **dsdl.h** - Add constant fields to `dsdl_type_composite_t`
2. **dsdl.c** - Implement constant evaluation and type attribute access
3. **Tests** - Verify with existing test DSDL files that use these features

---

## Test Coverage

The following test files exercise the missing features:

**Constants:**
- `nunavut_test_types/test0/regulated/RGB888_3840x2748.0.1.dsdl`
- `nunavut_test_types/test0/regulated/basics/7000.Struct_.0.1.dsdl`

**Type Attributes:**
- `validation/ComplexRefExpr.0.1.dsdl`
- `reg/udral/physics/kinematics/geodetic/Point.0.1.dsdl`

**Services:**
- `nunavut_test_types/test0/regulated/basics/300.Service.0.1.dsdl`
- `zubax/low_level_io/Access.0.1.dsdl`
- `uavcan/pnp/cluster/391.RequestVote.1.0.dsdl`

**Deprecated:**
- Multiple files use `@deprecated` directive (already parsed, just needs storage)
