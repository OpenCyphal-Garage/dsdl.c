/// Cyphal DSDL Parser in C
///
/// A compact and baremetal-friendly C99+ implementation of a Cyphal DSDL parser that allows loading
/// DSDL definitions at runtime without compile-time code generation. Can be used in small MCUs.
///
/// Copyright (c) OpenCyphal Development Team
/// SPDX-License-Identifier: MIT

#ifndef DSDL_H_INCLUDED
#define DSDL_H_INCLUDED

#include <wkv.h>

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

// ============================================================================
// Configuration macros (can be overridden before including this header)
// ============================================================================

/// Path separator character. Override for platforms with different conventions.
#ifndef DSDL_PATH_SEP
#define DSDL_PATH_SEP '/'
#endif

/// Maximum path length for file operations. Override for platforms with different limits.
#ifndef DSDL_PATH_MAX
#define DSDL_PATH_MAX 1024
#endif

/// If enabled, the library will log trace events via dsdl_trace(), which must be implemented in the application.
/// It is best not to use this in production.
#ifndef DSDL_CONFIG_TRACE
#define DSDL_CONFIG_TRACE 0
#endif

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

static inline bool          dsdl_type_is_void(dsdl_type_t t) { return (t & DSDL_TYPE_CATEGORY_MASK) == 0x0000U; }
static inline bool          dsdl_type_is_int(dsdl_type_t t) { return (t & DSDL_TYPE_CATEGORY_MASK) == 0x0100U; }
static inline bool          dsdl_type_is_uint(dsdl_type_t t) { return (t & DSDL_TYPE_CATEGORY_MASK) == 0x0200U; }
static inline bool          dsdl_type_is_float(dsdl_type_t t) { return (t & DSDL_TYPE_CATEGORY_MASK) == 0x0500U; }
static inline bool          dsdl_type_is_array(dsdl_type_t t) { return (t & DSDL_TYPE_CATEGORY_MASK) == 0x0A00U; }
static inline bool          dsdl_type_is_composite(dsdl_type_t t) { return (t & DSDL_TYPE_CATEGORY_MASK) == 0x0F00U; }
static inline bool          dsdl_type_is_alias(dsdl_type_t t) { return (t & DSDL_TYPE_ALIAS_MASK) != 0; }
static inline uint_least8_t dsdl_type_bit_width(dsdl_type_t t) { return (uint_least8_t)(t & DSDL_TYPE_BITWIDTH_MASK); }

typedef struct dsdl_type_array_t
{
    dsdl_type_t        type;        ///< Always the first field; here DSDL_ARRAY_*
    uint64_t           capacity;    ///< Maximum number of elements (from type definition)
    dsdl_type_t*       member_type; ///< Points to any of dsdl_type_*; castable to dsdl_type_t* for type identification.
    struct dsdl_bls_t* bls;         ///< Internal: symbolic bit length set.
} dsdl_type_array_t;

/// Represents the absence of a fixed port-ID. Few types have it.
#define DSDL_FIXED_PORT_ID_NONE 0xFFFFU

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

    uint64_t extent;     ///< Maximum serialized size in bytes.
    bool     sealed;     ///< True if @sealed directive present
    bool     deprecated; ///< True if @deprecated directive present

    size_t        field_count; ///< Number of fields
    wkv_str_t*    field_names; ///< Array of field names
    dsdl_type_t** field_types; ///< Array of pointers to dsdl_type_t*, dsdl_type_array_t*, dsdl_type_composite_t*, ...

    size_t              constant_count;  ///< Number of constants
    wkv_str_t*          constant_names;  ///< Array of constant names
    struct dsdl_value_t* constant_values; ///< Array of evaluated constant values

    struct dsdl_type_composite_t* response; ///< In RPC-service types contains the response type. NULL otherwise.

    uint16_t fixed_port_id; ///< If not set, DSDL_FIXED_PORT_ID_NONE.

    struct dsdl_bls_t* bls; ///< Internal: symbolic bit length set.
} dsdl_type_composite_t;

// ============================================================================
// Values
// ============================================================================

/// Rational number for exact arithmetic during expression evaluation.
/// Per DSDL spec section 3.1: rationals must be stored normalized with
/// positive denominator and GCD(num, den) == 1.
typedef struct
{
    intmax_t  num; ///< Numerator (signed)
    uintmax_t den; ///< Denominator (always positive, 0 means NaN, 1 for integers)
} dsdl_rational_t;

/// Forward declaration for recursive type.
typedef struct dsdl_value_t dsdl_value_t;

/// The context needs to be freed afterward.
/// The result is true on success, false on error.
typedef struct dsdl_closure_t
{
    void* context;
    bool (*fun)(struct dsdl_closure_t* self, dsdl_value_t* out);
    void (*cleanup)(struct dsdl_closure_t* self);
    bool (*clone)(const struct dsdl_closure_t* self, struct dsdl_closure_t* out);
} dsdl_closure_t;

/// Expression value types for compile-time evaluation.
/// A closure may return another closure, which needs to be evaluated in turn; repeat the loop until you get a concrete
/// value.
typedef enum
{
    dsdl_value_rational, ///< Numeric value (integer or rational)
    dsdl_value_string,   ///< Unicode string
    dsdl_value_bool,     ///< Boolean
    dsdl_value_set,      ///< Set of values
    dsdl_value_type,     ///< Serializable metatype reference
    dsdl_value_deferred, ///< A closure that needs to be evaluated, that returns a value.
} dsdl_value_kind_t;

/// Runtime value during expression evaluation.
/// Constant values stored in composite descriptors are expected to be fully resolved (not deferred).
struct dsdl_value_t
{
    dsdl_value_kind_t kind;
    uint8_t           flags; ///< Internal ownership flags.
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
        void*          type_ref; ///< dsdl_value_type (pointer to dsdl_type_composite_t)
        dsdl_closure_t deferred; ///< dsdl_value_deferred
    } as;
};

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

    /// Root namespace directories. Example:
    ///     - "/home/user/dsdl_namespaces/uavcan"
    ///     - "/home/user/dsdl_namespaces/zubax"
    /// New namespace are added to the end. Type lookup checks namespaces starting from the first, and uses the
    /// first match, thus entries added earlier take precedence.
    size_t     namespace_count;
    wkv_str_t* namespaces;

    /// Memory allocator callback.
    /// - pointer==NULL, new_size>0: allocate new memory
    /// - pointer!=NULL, new_size>0: reallocate
    /// - pointer!=NULL, new_size==0: free memory
    /// Compatible with standard realloc(), or use O1Heap for deterministic real-time allocation.
    void* (*realloc)(dsdl_t* self, void* pointer, size_t new_size);

    /// Read the entire file at the given path.
    /// The returned str buffer is allocated via the realloc callback and will be freed by the library after parsing.
    /// Returns {.str=NULL, .len=0} on error (file not found, OOM, etc.).
    wkv_str_t (*read)(dsdl_t* self, wkv_str_t path);

    /// List directory. Returns a heap-allocated array of heap-allocated names.
    /// Returns NULL on error (not a directory, bad path, OOM, etc.).
    /// The last entry in the array has {.str=NULL, .len=0}.
    /// The library will free each item's .str and the array itself.
    wkv_str_t* (*list)(dsdl_t* self, wkv_str_t path);
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
uint64_t dsdl_serialized_footprint(const dsdl_type_composite_t* type);

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
/// @return Number of bytes written, or SIZE_MAX on error
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
/// @return Number of bytes consumed, or SIZE_MAX on error
size_t dsdl_deserialize(const dsdl_type_composite_t* type, void* value, size_t input_size, const void* input);

/// For diagnostics and logging only. Usage in production is not recommended.
/// This function is only required if DSDL_CONFIG_TRACE is defined and is nonzero; otherwise it should be left
/// undefined.
extern void dsdl_trace(dsdl_t* const       self,
                       const char* const   file,
                       const uint_fast16_t line,
                       const char* const   func,
                       const char* const   format,
                       ...)
#if defined(__GNUC__) || defined(__clang__)
  __attribute__((__format__(__printf__, 5, 6)))
#endif
  ;

#ifdef __cplusplus
}
#endif

#endif // DSDL_H_INCLUDED
