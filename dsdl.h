/// Cyphal DSDL Parser in C
///
/// A compact and baremetal-friendly C99+ implementation of a Cyphal DSDL parser that allows loading
/// DSDL definitions at runtime without compile-time code generation. Can be used in small MCUs.
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
extern "C"
{
#endif

// ============================================================================
// Types
// ============================================================================

/// Type identifier encoding:
/// - Bits 0-7:   Bit width (for primitives) or subtype
/// - Bits 8-11:  Type category
/// - Bits 12-15: Alias flag
///
/// There is a number of structs named `dsdl_type_*_t`; all have a field of type dsdl_type_t as the first element for
/// runtime type identification. One can interpret a pointer to a type descriptor as dsdl_type_t to find out its
/// category, and then cast to the specific struct type to obtain further details.
typedef uint16_t dsdl_type_t;

// Void types (void1..void64)
#define DSDL_VOID(w) ((dsdl_type_t)(0x0000 + (w)))

// Signed integer types (int2..int64)
#define DSDL_INT(w) ((dsdl_type_t)(0x0100 + (w)))

// Unsigned integer types (uint1..uint64)
#define DSDL_UINT(w) ((dsdl_type_t)(0x0200 + (w)))

// Floating point types
#define DSDL_FLOAT(w) ((dsdl_type_t)(0x0500 + (w)))
#define DSDL_FLOAT16  DSDL_FLOAT(16)
#define DSDL_FLOAT32  DSDL_FLOAT(32)
#define DSDL_FLOAT64  DSDL_FLOAT(64)

// Array types
#define DSDL_ARRAY_FIXED    ((dsdl_type_t)0x0A00)
#define DSDL_ARRAY_VARIABLE ((dsdl_type_t)0x0A01)

// Composite types
#define DSDL_COMPOSITE_STRUCT ((dsdl_type_t)0x0F00)
#define DSDL_COMPOSITE_UNION  ((dsdl_type_t)0x0F01)
#define DSDL_COMPOSITE_RPC    ((dsdl_type_t)0x0F02)

// Aliases (bits above 0xFFF indicate alias, mask with 0x0FFF for base type)
#define DSDL_BOOL ((dsdl_type_t)(0x1000U + DSDL_UINT(1)))
#define DSDL_BYTE ((dsdl_type_t)(0x1000U + DSDL_UINT(8)))
#define DSDL_UTF8 ((dsdl_type_t)(0x2000U + DSDL_UINT(8)))

// Type category masks and checks
#define DSDL_TYPE_CATEGORY_MASK ((dsdl_type_t)0x0F00)
#define DSDL_TYPE_BITWIDTH_MASK ((dsdl_type_t)0x00FF)
#define DSDL_TYPE_ALIAS_MASK    ((dsdl_type_t)0xF000)

static inline bool    dsdl_type_is_void(dsdl_type_t t) { return (t & DSDL_TYPE_CATEGORY_MASK) == 0x0000U; }
static inline bool    dsdl_type_is_int(dsdl_type_t t) { return (t & DSDL_TYPE_CATEGORY_MASK) == 0x0100U; }
static inline bool    dsdl_type_is_uint(dsdl_type_t t) { return (t & DSDL_TYPE_CATEGORY_MASK) == 0x0200U; }
static inline bool    dsdl_type_is_float(dsdl_type_t t) { return (t & DSDL_TYPE_CATEGORY_MASK) == 0x0500U; }
static inline bool    dsdl_type_is_array(dsdl_type_t t) { return (t & DSDL_TYPE_CATEGORY_MASK) == 0x0A00U; }
static inline bool    dsdl_type_is_composite(dsdl_type_t t) { return (t & DSDL_TYPE_CATEGORY_MASK) == 0x0F00U; }
static inline bool    dsdl_type_is_alias(dsdl_type_t t) { return (t & DSDL_TYPE_ALIAS_MASK) != 0; }
static inline uint8_t dsdl_type_bit_width(dsdl_type_t t) { return (uint8_t)(t & DSDL_TYPE_BITWIDTH_MASK); }

typedef struct dsdl_type_array_t
{
    dsdl_type_t  type;        ///< Always the first field; here DSDL_ARRAY_*
    size_t       capacity;    ///< Maximum number of elements (from type definition)
    dsdl_type_t* member_type; ///< Points to any of dsdl_type_*; castable to dsdl_type_t* for type identification.
} dsdl_type_array_t;

/// Composite type descriptor (struct, union, or RPC-service).
///
/// Instances are heap-allocated such that the instance is at the beginning of the
/// allocated block, and all pointees (name, fields, etc.) are contained in the same
/// allocated block after the instance. This enables simple memory management.
typedef struct dsdl_type_composite_t
{
    dsdl_type_t   type;       ///< Always the first field; here DSDL_COMPOSITE_*
    wkv_str_t     name;       ///< Fully qualified type name
    uint_least8_t version[2]; ///< [major, minor]

    size_t extent; ///< Maximum serialized size in bytes.
    bool   sealed; ///< True if @sealed directive present

    size_t        field_count; ///< Number of fields
    wkv_str_t*    field_names; ///< Array of field names
    dsdl_type_t** field_types; ///< Array of pointers to dsdl_type_t*, dsdl_type_array_t*, dsdl_type_composite_t*, ...

    dsdl_type_composite_t* response; ///< In RPC-service types this field contains the response type. NULL otherwise.
} dsdl_type_composite_t;

// ============================================================================
// Values
// ============================================================================

// DSDL values are represented natively as follows:
//
//      void1..void64    -- no representation; corresponding data fields are omitted.
//
//      bool             -- bool
//
//      int2..int8       -- int_least8_t
//      int9..int16      -- int_least16_t
//      int17..int32     -- int_least32_t
//      int33..int64     -- int_least64_t
//
//      uint1..uint8     -- uint_least8_t
//      uint9..uint16    -- uint_least16_t
//      uint17..uint32   -- uint_least32_t
//      uint33..uint64   -- uint_least64_t
//
//      byte             -- unsigned char
//      utf8             -- char
//
//      float16          -- float
//      float32          -- float
//      float64          -- double
//
//      [n]              -- void* (pointer to the native array storage of fixed size)
//      [<=n] [<n]       -- dsdl_value_array_variable_t
//
//      struct           -- dsdl_value_struct_t
//      union            -- dsdl_value_union_t

/// There is no counterpart for fixed arrays because they are just raw pointers.
typedef struct dsdl_value_array_variable_t
{
    /// When deserializing, the count specifies the length of the destination array.
    /// If the deserialized message contains more elements, deserialization will fail.
    size_t count;
    void*  members;
} dsdl_value_array_variable_t;

typedef struct dsdl_value_struct_t
{
    /// Points to an array of pointers, where the size of the array equals the number of fields,
    /// and each element points to the field value according to its type. For example, given fields:
    ///
    ///     uint24               integer
    ///     utf8[<=16]           text
    ///     cyphal.Heartbeat.1.0 object
    ///
    /// The array would contain three elements, each a pointer, as follows:
    ///
    ///     #0 points to: uint_least32_t
    ///     #1 points to: dsdl_value_array_variable_t { count, *members }; members point to char*
    ///     #2 points to: dsdl_value_struct_t { **values }
    void** values;
} dsdl_value_struct_t;

typedef struct dsdl_value_union_t
{
    size_t tag;   ///< Which field is selected; must be in [0, field_count)
    void*  value; ///< Currently selected field value; see dsdl_value_struct_t
} dsdl_value_union_t;

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
    /// Compatible with standard realloc(), or use O1Heap for deterministic real-time allocation.
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
const dsdl_type_composite_t* dsdl_read(dsdl_t* self, wkv_str_t type_name);

/// Get the maximum serialized size in bytes for a type.
/// This is less than or equal the extent. For sealed types, equals the extent.
///
/// @param type  Type descriptor
/// @return Maximum serialized size in bytes, which is NOT the same as the extent.
size_t dsdl_serialized_footprint(const dsdl_type_composite_t* type);

/// Serialize a composite type instance to a byte buffer.
///
/// The value memory layout is explained above; summary:
/// - Primitives: smallest [u]int_leastX_t where X >= the original width.
/// - Fixed arrays: Pointer to the native C array.
/// - Variable arrays: { size_t count; ElementType* elements; }
/// - Structs: dsdl_value_struct_t
/// - Unions: dsdl_value_union_t
///
/// @param type         Type descriptor (must be struct or union, not RPC)
/// @param value        Pointer to dsdl_value_struct_t or dsdl_value_union_t, depending on the type.
/// @param output_size  Size of output buffer in bytes
/// @param output       Output buffer
/// @return Number of bytes written, or 0 on error
size_t dsdl_serialize(const dsdl_type_composite_t* type, const void* value, size_t output_size, void* output);

/// Deserialize a byte buffer into a composite type instance.
///
/// Value memory layout: Same as dsdl_serialize().
/// The values buffer must be pre-allocated with sufficient size for the type.
///
/// @param type        Type descriptor (must be struct or union, not RPC)
/// @param value       Pointer to dsdl_value_struct_t or dsdl_value_union_t, depending on the type.
/// @param input_size  Size of input buffer in bytes
/// @param input       Input buffer
/// @return Number of bytes consumed, or 0 on error
size_t dsdl_deserialize(const dsdl_type_composite_t* type, void* value, size_t input_size, const void* input);

#ifdef __cplusplus
}
#endif

#endif // DSDL_H_INCLUDED
