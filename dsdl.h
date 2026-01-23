/// Cyphal DSDL Parser in C
///
/// A compact C99+ implementation of a Cyphal DSDL parser that allows loading
/// DSDL definitions at runtime without compile-time code generation.
///
/// Copyright (c) OpenCyphal Development Team
/// SPDX-License-Identifier: MIT

#ifndef DSDL_H_INCLUDED
#define DSDL_H_INCLUDED

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#include "lib/wkv.h"

#ifdef __cplusplus
extern "C" {
#endif

// ============================================================================
// Type identifiers
// ============================================================================

/// Type identifier encoding:
/// - Bits 0-7:   Bit width (for primitives) or subtype
/// - Bits 8-11:  Type category
/// - Bits 12-15: Alias flag
///
/// Categories:
///   0x0 = void
///   0x1 = signed integer
///   0x2 = unsigned integer
///   0x5 = floating point
///   0xA = array
///   0xF = composite
typedef uint16_t dsdl_type_t;

// Void types (void1..void64)
#define DSDL_VOID1   ((dsdl_type_t)0x0001)
#define DSDL_VOID2   ((dsdl_type_t)0x0002)
#define DSDL_VOID3   ((dsdl_type_t)0x0003)
#define DSDL_VOID4   ((dsdl_type_t)0x0004)
#define DSDL_VOID5   ((dsdl_type_t)0x0005)
#define DSDL_VOID6   ((dsdl_type_t)0x0006)
#define DSDL_VOID7   ((dsdl_type_t)0x0007)
#define DSDL_VOID8   ((dsdl_type_t)0x0008)
#define DSDL_VOID16  ((dsdl_type_t)0x0010)
#define DSDL_VOID32  ((dsdl_type_t)0x0020)
#define DSDL_VOID64  ((dsdl_type_t)0x0040)

// Signed integer types (int2..int64)
#define DSDL_INT2    ((dsdl_type_t)0x0102)
#define DSDL_INT3    ((dsdl_type_t)0x0103)
#define DSDL_INT4    ((dsdl_type_t)0x0104)
#define DSDL_INT5    ((dsdl_type_t)0x0105)
#define DSDL_INT6    ((dsdl_type_t)0x0106)
#define DSDL_INT7    ((dsdl_type_t)0x0107)
#define DSDL_INT8    ((dsdl_type_t)0x0108)
#define DSDL_INT16   ((dsdl_type_t)0x0110)
#define DSDL_INT32   ((dsdl_type_t)0x0120)
#define DSDL_INT64   ((dsdl_type_t)0x0140)

// Unsigned integer types (uint1..uint64)
#define DSDL_UINT1   ((dsdl_type_t)0x0201)
#define DSDL_UINT2   ((dsdl_type_t)0x0202)
#define DSDL_UINT3   ((dsdl_type_t)0x0203)
#define DSDL_UINT4   ((dsdl_type_t)0x0204)
#define DSDL_UINT5   ((dsdl_type_t)0x0205)
#define DSDL_UINT6   ((dsdl_type_t)0x0206)
#define DSDL_UINT7   ((dsdl_type_t)0x0207)
#define DSDL_UINT8   ((dsdl_type_t)0x0208)
#define DSDL_UINT16  ((dsdl_type_t)0x0210)
#define DSDL_UINT32  ((dsdl_type_t)0x0220)
#define DSDL_UINT64  ((dsdl_type_t)0x0240)

// Floating point types
#define DSDL_FLOAT16 ((dsdl_type_t)0x0510)
#define DSDL_FLOAT32 ((dsdl_type_t)0x0520)
#define DSDL_FLOAT64 ((dsdl_type_t)0x0540)

// Array types
#define DSDL_ARRAY_FIXED    ((dsdl_type_t)0x0A00)
#define DSDL_ARRAY_VARIABLE ((dsdl_type_t)0x0A01)

// Composite types
#define DSDL_COMPOSITE_STRUCT ((dsdl_type_t)0x0F00)
#define DSDL_COMPOSITE_UNION  ((dsdl_type_t)0x0F01)
#define DSDL_COMPOSITE_RPC    ((dsdl_type_t)0x0F02)

// Aliases (bit 12 set indicates alias, mask with 0x0FFF for base type)
#define DSDL_BOOL ((dsdl_type_t)0x1201)   ///< Alias for uint1
#define DSDL_BYTE ((dsdl_type_t)0x1208)   ///< Alias for uint8
#define DSDL_UTF8 ((dsdl_type_t)0x2208)   ///< Alias for variable uint8 array

// Type category masks and checks
#define DSDL_TYPE_CATEGORY_MASK  ((dsdl_type_t)0x0F00)
#define DSDL_TYPE_BITWIDTH_MASK  ((dsdl_type_t)0x00FF)
#define DSDL_TYPE_ALIAS_MASK     ((dsdl_type_t)0xF000)

static inline bool dsdl_type_is_void(dsdl_type_t t)      { return (t & DSDL_TYPE_CATEGORY_MASK) == 0x0000U; }
static inline bool dsdl_type_is_int(dsdl_type_t t)       { return (t & DSDL_TYPE_CATEGORY_MASK) == 0x0100U; }
static inline bool dsdl_type_is_uint(dsdl_type_t t)      { return (t & DSDL_TYPE_CATEGORY_MASK) == 0x0200U; }
static inline bool dsdl_type_is_float(dsdl_type_t t)     { return (t & DSDL_TYPE_CATEGORY_MASK) == 0x0500U; }
static inline bool dsdl_type_is_array(dsdl_type_t t)     { return (t & DSDL_TYPE_CATEGORY_MASK) == 0x0A00U; }
static inline bool dsdl_type_is_composite(dsdl_type_t t) { return (t & DSDL_TYPE_CATEGORY_MASK) == 0x0F00U; }
static inline bool dsdl_type_is_alias(dsdl_type_t t)     { return (t & DSDL_TYPE_ALIAS_MASK) != 0; }
static inline uint8_t dsdl_type_bit_width(dsdl_type_t t) { return (uint8_t)(t & DSDL_TYPE_BITWIDTH_MASK); }

// ============================================================================
// Data structures
// ============================================================================

/// Array type descriptor and storage.
typedef struct dsdl_array_t
{
    size_t      capacity;      ///< Maximum number of elements (from type definition)
    dsdl_type_t member_type;   ///< Type of array elements
    size_t      member_count;  ///< Current number of elements (for variable arrays)
    void*       members;       ///< Pointer to element storage
} dsdl_array_t;

/// Forward declaration for recursive types.
typedef struct dsdl_composite_t dsdl_composite_t;

/// Composite type descriptor (struct, union, or service).
///
/// Instances are heap-allocated such that the instance is at the beginning of the
/// allocated block, and all pointees (name, fields, etc.) are contained in the same
/// allocated block after the instance. This enables simple memory management.
struct dsdl_composite_t
{
    dsdl_type_t   type;          ///< Differentiates struct/union/RPC (DSDL_COMPOSITE_*)
    wkv_str_t     name;          ///< Fully qualified type name
    uint_least8_t version[2];    ///< [major, minor]

    size_t extent;   ///< Maximum serialized size in bits
    bool   sealed;   ///< True if @sealed directive present

    size_t       field_count;   ///< Number of fields
    wkv_str_t*   field_names;   ///< Array of field names
    dsdl_type_t* field_types;   ///< Array of field types
};

/// Struct instance with field values.
typedef struct dsdl_struct_t
{
    dsdl_composite_t base;     ///< Type descriptor
    void*            values;   ///< Packed field values
} dsdl_struct_t;

/// Union instance with selected variant.
typedef struct dsdl_union_t
{
    dsdl_composite_t base;    ///< Type descriptor
    void*            value;   ///< Currently selected field value
    size_t           tag;     ///< Which field is selected; must be in [0, field_count)
} dsdl_union_t;

/// Service (RPC) type with request and response parts.
typedef struct dsdl_rpc_t
{
    dsdl_composite_t  base;       ///< Type descriptor (defaults to request properties)
    dsdl_composite_t* request;    ///< Request type
    dsdl_composite_t* response;   ///< Response type
} dsdl_rpc_t;

// ============================================================================
// Parser state
// ============================================================================

/// Forward declaration.
typedef struct dsdl_t dsdl_t;

/// Main parser/runtime state.
///
/// Create with dsdl_new(), destroy with dsdl_destroy().
struct dsdl_t
{
    /// Contains all types read either explicitly or as dependencies.
    /// Name prefix tree with '.' separator, including version numbers.
    /// Values are pointers to dsdl_composite_t.
    /// Example key: "uavcan.node.Heartbeat.1.0"
    wkv_t types;

    /// Root namespace directories with '/' separator.
    /// Example: "/home/user/dsdl_namespaces/uavcan"
    wkv_t namespaces;

    /// Memory allocator callback.
    /// - pointer==NULL, new_size>0: allocate new memory
    /// - pointer!=NULL, new_size>0: reallocate
    /// - pointer!=NULL, new_size==0: free memory
    ///
    /// Compatible with standard realloc() when self is ignored, or use
    /// O1Heap for deterministic real-time allocation.
    void* (*realloc)(dsdl_t* self, void* pointer, size_t new_size);

    /// File reader callback.
    /// Read the entire file at the given path. The returned buffer is allocated
    /// via the realloc callback and will be freed by the library after parsing.
    /// Returns NULL on error (file not found, OOM, etc.).
    void* (*read)(dsdl_t* self, wkv_str_t path, size_t* out_size);
};

// ============================================================================
// Public API
// ============================================================================

/// Initialize a new DSDL parser state.
///
/// @param self         Pointer to state structure (caller-allocated)
/// @param realloc_func Memory allocator callback (required)
void dsdl_new(dsdl_t* self, void* (*realloc_func)(dsdl_t*, void*, size_t));

/// Destroy parser state and free all allocated memory.
///
/// @param self  Pointer to state structure
void dsdl_destroy(dsdl_t* self);

/// Register a namespace root directory for type lookup.
///
/// Types are not loaded until dsdl_read() is called. Multiple namespace
/// directories can be registered; they will be searched in order.
///
/// @param self            Parser state
/// @param root_directory  Path to namespace root directory
/// @return true on success, false on OOM
bool dsdl_add_namespace(dsdl_t* self, wkv_str_t root_directory);

/// Read and parse a DSDL type definition.
///
/// If the type is already cached, returns immediately. Otherwise, locates the
/// .dsdl file in registered namespaces, parses it, and recursively loads all
/// dependencies.
///
/// @param self       Parser state
/// @param type_name  Fully or partially qualified type name
///                   Examples: "uavcan.node.Heartbeat.1.0" (exact version)
///                            "uavcan.node.Heartbeat.1" (latest minor in v1)
///                            "uavcan.node.Heartbeat" (latest version)
/// @return Pointer to type descriptor (owned by dsdl_t), or NULL on error
const dsdl_composite_t* dsdl_read(dsdl_t* self, wkv_str_t type_name);

/// Get the maximum serialized size in bytes for a type.
///
/// @param type  Type descriptor
/// @return Maximum serialized size in bytes (ceil of extent/8)
size_t dsdl_serialized_footprint(const dsdl_composite_t* type);

/// Serialize a composite type instance to a byte buffer.
///
/// @param type         Type descriptor (must be struct or union, not RPC)
/// @param output_size  Size of output buffer in bytes
/// @param output       Output buffer
/// @return Number of bytes written, or 0 on error
size_t dsdl_serialize(const dsdl_composite_t* type, size_t output_size, void* output);

/// Deserialize a byte buffer into a composite type instance.
///
/// @param type        Type descriptor (must be struct or union, not RPC)
/// @param input_size  Size of input buffer in bytes
/// @param input       Input buffer
/// @return Number of bytes consumed, or 0 on error
size_t dsdl_deserialize(dsdl_composite_t* type, size_t input_size, const void* input);

#ifdef __cplusplus
}
#endif

#endif  // DSDL_H_INCLUDED
