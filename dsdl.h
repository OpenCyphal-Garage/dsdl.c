/// An approximation of what the API should roughly look like.

typedef enum dsdl_type_t : uint16_t
{
    // void1..void64
    dsdl_void1  = 0x0001,
    dsdl_void2  = 0x0002,
    dsdl_void3  = 0x0003,
    dsdl_void4  = 0x0004,
    dsdl_void5  = 0x0005,
    dsdl_void6  = 0x0006,
    dsdl_void7  = 0x0007,
    dsdl_void8  = 0x0008,
    dsdl_void9  = 0x0009,
    dsdl_void10 = 0x000A,
    dsdl_void11 = 0x000B,
    dsdl_void12 = 0x000C,
    dsdl_void13 = 0x000D,
    dsdl_void14 = 0x000E,
    dsdl_void15 = 0x000F,
    dsdl_void16 = 0x0010,
    dsdl_void17 = 0x0011,
    dsdl_void18 = 0x0012,
    dsdl_void19 = 0x0013,
    dsdl_void20 = 0x0014,
    dsdl_void21 = 0x0015,
    dsdl_void22 = 0x0016,
    dsdl_void23 = 0x0017,
    dsdl_void24 = 0x0018,
    dsdl_void25 = 0x0019,
    dsdl_void26 = 0x001A,
    dsdl_void27 = 0x001B,
    dsdl_void28 = 0x001C,
    dsdl_void29 = 0x001D,
    dsdl_void30 = 0x001E,
    dsdl_void31 = 0x001F,
    dsdl_void32 = 0x0020,
    dsdl_void33 = 0x0021,
    dsdl_void34 = 0x0022,
    dsdl_void35 = 0x0023,
    dsdl_void36 = 0x0024,
    dsdl_void37 = 0x0025,
    dsdl_void38 = 0x0026,
    dsdl_void39 = 0x0027,
    dsdl_void40 = 0x0028,
    dsdl_void41 = 0x0029,
    dsdl_void42 = 0x002A,
    dsdl_void43 = 0x002B,
    dsdl_void44 = 0x002C,
    dsdl_void45 = 0x002D,
    dsdl_void46 = 0x002E,
    dsdl_void47 = 0x002F,
    dsdl_void48 = 0x0030,
    dsdl_void49 = 0x0031,
    dsdl_void50 = 0x0032,
    dsdl_void51 = 0x0033,
    dsdl_void52 = 0x0034,
    dsdl_void53 = 0x0035,
    dsdl_void54 = 0x0036,
    dsdl_void55 = 0x0037,
    dsdl_void56 = 0x0038,
    dsdl_void57 = 0x0039,
    dsdl_void58 = 0x003A,
    dsdl_void59 = 0x003B,
    dsdl_void60 = 0x003C,
    dsdl_void61 = 0x003D,
    dsdl_void62 = 0x003E,
    dsdl_void63 = 0x003F,
    dsdl_void64 = 0x0040,

    // int2..int64
    dsdl_int2  = 0x0102,
    dsdl_int3  = 0x0103,
    dsdl_int4  = 0x0104,
    dsdl_int5  = 0x0105,
    dsdl_int6  = 0x0106,
    dsdl_int7  = 0x0107,
    dsdl_int8  = 0x0108,
    dsdl_int9  = 0x0109,
    dsdl_int10 = 0x010A,
    dsdl_int11 = 0x010B,
    dsdl_int12 = 0x010C,
    dsdl_int13 = 0x010D,
    dsdl_int14 = 0x010E,
    dsdl_int15 = 0x010F,
    dsdl_int16 = 0x0110,
    dsdl_int17 = 0x0111,
    dsdl_int18 = 0x0112,
    dsdl_int19 = 0x0113,
    dsdl_int20 = 0x0114,
    dsdl_int21 = 0x0115,
    dsdl_int22 = 0x0116,
    dsdl_int23 = 0x0117,
    dsdl_int24 = 0x0118,
    dsdl_int25 = 0x0119,
    dsdl_int26 = 0x011A,
    dsdl_int27 = 0x011B,
    dsdl_int28 = 0x011C,
    dsdl_int29 = 0x011D,
    dsdl_int30 = 0x011E,
    dsdl_int31 = 0x011F,
    dsdl_int32 = 0x0120,
    dsdl_int33 = 0x0121,
    dsdl_int34 = 0x0122,
    dsdl_int35 = 0x0123,
    dsdl_int36 = 0x0124,
    dsdl_int37 = 0x0125,
    dsdl_int38 = 0x0126,
    dsdl_int39 = 0x0127,
    dsdl_int40 = 0x0128,
    dsdl_int41 = 0x0129,
    dsdl_int42 = 0x012A,
    dsdl_int43 = 0x012B,
    dsdl_int44 = 0x012C,
    dsdl_int45 = 0x012D,
    dsdl_int46 = 0x012E,
    dsdl_int47 = 0x012F,
    dsdl_int48 = 0x0130,
    dsdl_int49 = 0x0131,
    dsdl_int50 = 0x0132,
    dsdl_int51 = 0x0133,
    dsdl_int52 = 0x0134,
    dsdl_int53 = 0x0135,
    dsdl_int54 = 0x0136,
    dsdl_int55 = 0x0137,
    dsdl_int56 = 0x0138,
    dsdl_int57 = 0x0139,
    dsdl_int58 = 0x013A,
    dsdl_int59 = 0x013B,
    dsdl_int60 = 0x013C,
    dsdl_int61 = 0x013D,
    dsdl_int62 = 0x013E,
    dsdl_int63 = 0x013F,
    dsdl_int64 = 0x0140,

    // uint1..uint64
    dsdl_uint1  = 0x0201,
    dsdl_uint2  = 0x0202,
    dsdl_uint3  = 0x0203,
    dsdl_uint4  = 0x0204,
    dsdl_uint5  = 0x0205,
    dsdl_uint6  = 0x0206,
    dsdl_uint7  = 0x0207,
    dsdl_uint8  = 0x0208,
    dsdl_uint9  = 0x0209,
    dsdl_uint10 = 0x020A,
    dsdl_uint11 = 0x020B,
    dsdl_uint12 = 0x020C,
    dsdl_uint13 = 0x020D,
    dsdl_uint14 = 0x020E,
    dsdl_uint15 = 0x020F,
    dsdl_uint16 = 0x0210,
    dsdl_uint17 = 0x0211,
    dsdl_uint18 = 0x0212,
    dsdl_uint19 = 0x0213,
    dsdl_uint20 = 0x0214,
    dsdl_uint21 = 0x0215,
    dsdl_uint22 = 0x0216,
    dsdl_uint23 = 0x0217,
    dsdl_uint24 = 0x0218,
    dsdl_uint25 = 0x0219,
    dsdl_uint26 = 0x021A,
    dsdl_uint27 = 0x021B,
    dsdl_uint28 = 0x021C,
    dsdl_uint29 = 0x021D,
    dsdl_uint30 = 0x021E,
    dsdl_uint31 = 0x021F,
    dsdl_uint32 = 0x0220,
    dsdl_uint33 = 0x0221,
    dsdl_uint34 = 0x0222,
    dsdl_uint35 = 0x0223,
    dsdl_uint36 = 0x0224,
    dsdl_uint37 = 0x0225,
    dsdl_uint38 = 0x0226,
    dsdl_uint39 = 0x0227,
    dsdl_uint40 = 0x0228,
    dsdl_uint41 = 0x0229,
    dsdl_uint42 = 0x022A,
    dsdl_uint43 = 0x022B,
    dsdl_uint44 = 0x022C,
    dsdl_uint45 = 0x022D,
    dsdl_uint46 = 0x022E,
    dsdl_uint47 = 0x022F,
    dsdl_uint48 = 0x0230,
    dsdl_uint49 = 0x0231,
    dsdl_uint50 = 0x0232,
    dsdl_uint51 = 0x0233,
    dsdl_uint52 = 0x0234,
    dsdl_uint53 = 0x0235,
    dsdl_uint54 = 0x0236,
    dsdl_uint55 = 0x0237,
    dsdl_uint56 = 0x0238,
    dsdl_uint57 = 0x0239,
    dsdl_uint58 = 0x023A,
    dsdl_uint59 = 0x023B,
    dsdl_uint60 = 0x023C,
    dsdl_uint61 = 0x023D,
    dsdl_uint62 = 0x023E,
    dsdl_uint63 = 0x023F,
    dsdl_uint64 = 0x0240,

    // float16, float32, float64
    dsdl_float16 = 0x0510,
    dsdl_float32 = 0x0520,
    dsdl_float64 = 0x0540,

    // arrays
    dsdl_array_fixed    = 0x0A00,
    dsdl_array_variable = 0x0A01,

    // composites
    dsdl_composite_struct = 0x0F00,
    dsdl_composite_union  = 0x0F01,
    dsdl_composite_rpc    = 0x0F02,

    // aliases; each maps to a basic alternative when masked 0x0FFF
    dsdl_bool = 0x1201,
    dsdl_byte = 0x1208,
    dsdl_utf8 = 0x2208,
} dsdl_type_t;

static inline bool dsdl_type_is_void(const dsdl_type_t t)      { return (t & 0x0F00U ) == 0x0000U; }
static inline bool dsdl_type_is_int (const dsdl_type_t t)      { return (t & 0x0F00U ) == 0x0100U; }
static inline bool dsdl_type_is_uint(const dsdl_type_t t)      { return (t & 0x0F00U ) == 0x0200U; }
static inline bool dsdl_type_is_float(const dsdl_type_t t)     { return (t & 0x0F00U ) == 0x0500U; }
static inline bool dsdl_type_is_array(const dsdl_type_t t)     { return (t & 0x0F00U ) == 0x0A00U; }
static inline bool dsdl_type_is_composite(const dsdl_type_t t) { return (t & 0x0F00U ) == 0x0F00U; }

typedef struct dsdl_array_t
{
    size_t      capacity;
    dsdl_type_t member_type;
    size_t      member_count;
    void*       members;
} dsdl_array_t;

/// Instances are heap-allocated, such that the instance is at the beginning of the allocated block,
/// and all pointees (name, fields, etc) are contained in the same allocated block after the instance.
/// This enables very simple memory management.
typedef struct dsdl_composite_t
{
    dsdl_type_t   type;          ///< Differentiates struct/union/RPC.
    wkv_str_t     name;
    uint_least8_t version[2];    ///< [major, minor]

    size_t        extent;
    bool          sealed;

    size_t       field_count;
    wkv_str_t    field_names;
    dsdl_type_t* field_types;
} dsdl_composite_t;

typedef struct dsdl_struct_t
{
    dsdl_composite_t base;
    void*            values;
} dsdl_struct_t;

typedef struct dsdl_union_t
{
    dsdl_composite_t base;
    void*  value;
    size_t tag;  ///< Which field is currently selected; must be in [0, field_count)
} dsdl_union_t;

typedef struct dsdl_rpc_t
{
    dsdl_composite_t  base;     ///< All properties -- name, extent, etc -- default to those of the request part.
    dsdl_composite_t* request;
    dsdl_composite_t* response;
} dsdl_rpc_t;


typedef struct dsdl_t
{
    /// Contains all types read either explicitly or as dependencies of explicitly read types. Initially empty.
    /// For example, if "foo.Bar" depends on "foo.Baz", reading the Bar will also load Baz.
    /// Name prefix tree with `.` separator, including the version numbers. Values are dsdl_composite_t.
    /// For example, "cyphal.Heartbeat.1.0": dsdl_struct_t.
    /// The list of all namespaces is the list of all top-level name components.
    wkv_t types;

    /// Root namespace directories with `/` separator.
    /// For example, "/home/user/foo", "/home/user/projects/namespaces/foo" -- both refer to the same namespace.
    wkv_t namespaces;

    /// Memory alloc/free/realloc. Use o1heap or the standard library realloc().
    /// new_size==0 to free the memory; pointer==nullptr to allocate new memory.
    void* (*realloc)(struct dsdl_t* self, void* pointer, size_t new_size);

    /// Read the specified file from the filesystem, in its entirety. The read data is heap-allocated and will be
    /// freed by the library when done parsing. Returns NULL on OOM or if not readable.
    void* (*read)(struct dsdl_t* self, wkv_str_t path, size_t* out_size);
} dsdl_t;


void dsdl_new(dsdl_t* const self, void* (*realloc)(dsdl_t*, void*, size_t));

/// Simply deallocates all memory.
void dsdl_destroy(dsdl_t* const self);


/// Adds the specified namespace root directory to the state, but does not read it.
/// Namespaces are only read via dsdl_read() as needed.
/// False if oom.
bool dsdl_add_namespace(dsdl_t* const self, const wkv_str_t root_directory);


/// If the type is already read (in dsdl_t::types), it is immediately returned.
/// Otherwise, the type is parsed, along with all dependent types, and all parsed types are added to dsdl_read::types.
/// The returned type is owned by dsdl_t (not by the user).
///
/// The type name may optionally specify both version numbers, only the major, or none.
/// Missing version numbers default to the highest available.
/// Returns NULL on OOM or if the type is not found or is invalid.
const dsdl_composite_t* dsdl_read(dsdl_t* const self, const wkv_str_t type_name);

/// Returns the space, in bytes, needed to store the largest possible serialized representation.
size_t dsdl_serialized_footprint(const dsdl_composite_t* const type);

/// Constructs the serialized representation of the given type and values.
/// The type must be struct or union; RPC is not serializable.
size_t dsdl_serialize(const dsdl_composite_t* const type, const size_t output_size, void* const output);

/// Returns the number of bytes consumed from the input.
/// TODO: accept scatter buffers!
size_t dsdl_deserialize(dsdl_composite_t* const type, const size_t input_size, const void* const input);
