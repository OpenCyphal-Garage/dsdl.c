# Cyphal DSDL Parser in C

A compact, baremetal-friendly C99 implementation of a [Cyphal](https://opencyphal.org/) DSDL parser that allows loading DSDL definitions at runtime without compile-time code generation.

## Features

- **Runtime type loading**: Parse DSDL files at runtime without code generation
- **Baremetal-friendly**: No stdio, no heap (user-provided `realloc` callback)
- **Minimal dependencies**: Single-file implementation (`dsdl.c`), only requires `lib/wkv.h`
- **C99 compliant**: Portable across all standard-compliant compilers
- **Platform-agnostic**: No assumptions about pointer width, endianness, or execution environment
- **Serialization/deserialization**: Full support for Cyphal message encoding/decoding
- **Cross-validated**: Tested against PyDSDL and Nunavut reference implementations
- **Comprehensive error reporting**: Detailed error codes for all failure modes

## Building

```bash
# Configure
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release

# Build
cmake --build build -j$(nproc)

# Test
ctest --test-dir build --output-on-failure
```

### Build Options

- `DSDL_BUILD_TESTS`: Build the test suite (default: ON)
- `DSDL_ENABLE_COVERAGE`: Enable code coverage (default: OFF)
- `DSDL_ENABLE_SANITIZERS`: Enable ASan/UBSan (default: OFF)
- `DSDL_BUILD_TOOLS`: Build standalone tools (default: ON)

## Usage

### Basic Example

```c
#include <dsdl.h>
#include <stdio.h>
#include <stdlib.h>

// Memory allocator callback (can use standard realloc or O1Heap)
static void* my_realloc(dsdl_t* self, void* ptr, size_t size)
{
    (void)self;
    if (size == 0) {
        free(ptr);
        return NULL;
    }
    return realloc(ptr, size);
}

// File reader callback
static wkv_str_t my_read_file(dsdl_t* self, wkv_str_t path)
{
    (void)self;
    FILE* f = fopen(path.str, "rb");
    if (!f) return (wkv_str_t){NULL, 0};
    
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    
    char* buf = malloc((size_t)size);
    if (!buf) {
        fclose(f);
        return (wkv_str_t){NULL, 0};
    }
    
    fread(buf, 1, (size_t)size, f);
    fclose(f);
    return (wkv_str_t){buf, (size_t)size};
}

// Directory listing callback
static wkv_str_t* my_list_dir(dsdl_t* self, wkv_str_t path)
{
    (void)self;
    (void)path;
    // Implementation depends on platform (POSIX: opendir/readdir, Windows: FindFirstFile)
    // Return NULL-terminated array of wkv_str_t
    return NULL;  // Simplified for example
}

int main(void)
{
    // Initialize parser
    dsdl_t dsdl;
    dsdl_new(&dsdl, my_realloc);
    dsdl.read = my_read_file;
    dsdl.list = my_list_dir;
    
    // Register namespace directories
    if (!dsdl_add_namespace(&dsdl, WKV_STR("/path/to/public_regulated_data_types/uavcan"))) {
        fprintf(stderr, "Failed to add namespace: error %d\n", dsdl.error);
        dsdl_destroy(&dsdl);
        return 1;
    }
    
    // Load a type definition
    const dsdl_type_composite_t* type = dsdl_read(&dsdl, WKV_STR("uavcan.node.Heartbeat.1.0"));
    if (!type) {
        fprintf(stderr, "Failed to read type: error %d\n", dsdl.error);
        dsdl_destroy(&dsdl);
        return 1;
    }
    
    printf("Loaded type: %s\n", type->name.str);
    printf("Version: %u.%u\n", type->version[0], type->version[1]);
    printf("Extent: %llu bytes\n", (unsigned long long)type->extent);
    
    // Cleanup
    dsdl_destroy(&dsdl);
    return 0;
}
```

### Serialization Example

```c
#include <dsdl.h>
#include <string.h>

// Serialize a Heartbeat message
void serialize_heartbeat(const dsdl_type_composite_t* heartbeat_type)
{
    // Prepare message data
    // Heartbeat has fields: uptime (uint32), health (uint2), mode (uint3), vendor_specific_status_code (uint8)
    uint_least32_t uptime = 12345;
    uint_least8_t health = 0;  // NOMINAL
    uint_least8_t mode = 0;    // OPERATIONAL
    uint_least8_t vendor_code = 0;
    
    // Create struct value
    void* field_values[] = {&uptime, &health, &mode, &vendor_code};
    dsdl_value_struct_t msg = {.values = field_values};
    
    // Serialize
    uint8_t buffer[256];
    size_t size = dsdl_serialize(heartbeat_type, &msg, sizeof(buffer), buffer);
    
    if (size == SIZE_MAX) {
        fprintf(stderr, "Serialization failed\n");
        return;
    }
    
    printf("Serialized %zu bytes\n", size);
}
```

### Deserialization Example

```c
#include <dsdl.h>

// Deserialize a Heartbeat message
void deserialize_heartbeat(const dsdl_type_composite_t* heartbeat_type, 
                          const uint8_t* buffer, size_t buffer_size)
{
    // Prepare storage for deserialized data
    uint_least32_t uptime;
    uint_least8_t health;
    uint_least8_t mode;
    uint_least8_t vendor_code;
    
    void* field_values[] = {&uptime, &health, &mode, &vendor_code};
    dsdl_value_struct_t msg = {.values = field_values};
    
    // Deserialize
    size_t consumed = dsdl_deserialize(heartbeat_type, &msg, buffer_size, buffer);
    
    if (consumed == SIZE_MAX) {
        fprintf(stderr, "Deserialization failed\n");
        return;
    }
    
    printf("Deserialized %zu bytes\n", consumed);
    printf("Uptime: %u seconds\n", (unsigned)uptime);
    printf("Health: %u\n", (unsigned)health);
    printf("Mode: %u\n", (unsigned)mode);
}
```

### Error Handling

The library provides detailed error codes through the `dsdl_t.error` field:

```c
dsdl_t dsdl;
dsdl_new(&dsdl, my_realloc);

const dsdl_type_composite_t* type = dsdl_read(&dsdl, WKV_STR("nonexistent.Type.1.0"));
if (!type) {
    switch (dsdl.error) {
        case dsdl_error_out_of_memory:
            fprintf(stderr, "Out of memory\n");
            break;
        case dsdl_error_file_not_found:
            fprintf(stderr, "Type definition not found\n");
            break;
        case dsdl_error_parse:
            fprintf(stderr, "Syntax error in DSDL file\n");
            break;
        case dsdl_error_semantic:
            fprintf(stderr, "Semantic error (type resolution failed)\n");
            break;
        default:
            fprintf(stderr, "Unknown error: %d\n", dsdl.error);
            break;
    }
}

dsdl_destroy(&dsdl);
```

### Error Codes

| Code | Description |
|------|-------------|
| `dsdl_error_none` | No error (success) |
| `dsdl_error_out_of_memory` | Memory allocation failed |
| `dsdl_error_file_not_found` | DSDL file or namespace not found |
| `dsdl_error_parse` | Syntax error in DSDL file |
| `dsdl_error_semantic` | Type resolution or expression evaluation failure |
| `dsdl_error_serialization` | Buffer too small or invalid value during serialization |
| `dsdl_error_deserialization` | Truncated input or invalid data during deserialization |
| `dsdl_error_array_capacity` | Array length exceeds capacity |
| `dsdl_error_union_tag` | Union tag exceeds available options |

## Tools

The project includes command-line tools for working with DSDL:

### dsdl_to_json

Convert DSDL types to JSON representation:

```bash
./build/tools/dsdl_to_json \
  -r /path/to/public_regulated_data_types/uavcan \
  -- uavcan.node.Heartbeat.1.0
```

### dsdl_to_dsdl

Emit canonicalized DSDL:

```bash
./build/tools/dsdl_to_dsdl \
  -r /path/to/public_regulated_data_types/uavcan \
  -- uavcan.node.Heartbeat.1.0
```

See [`tools/README.md`](tools/README.md) for more details.

## Testing

The project includes comprehensive tests:

- **Unit tests**: Parser, type resolution, serialization, deserialization
- **PyDSDL parity**: Cross-validation against PyDSDL reference implementation
- **Nunavut cross-validation**: Byte-for-byte serialization comparison with Nunavut
- **C++20 compatibility**: Ensures header works with C++20 and later
- **x86/x64 matrix**: Tests on both 32-bit and 64-bit architectures

Run all tests:

```bash
ctest --test-dir build --output-on-failure
```

Run specific test:

```bash
./build/tests/test_dsdl_x64
```

## Code Coverage

Generate coverage report:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug -DDSDL_ENABLE_COVERAGE=ON
cmake --build build
ctest --test-dir build
cmake --build build --target coverage
```

Coverage reports are generated in `build/coverage/`.

## API Reference

See [`dsdl.h`](dsdl.h) for the complete API documentation. Key functions:

- `dsdl_new()`: Initialize parser state
- `dsdl_destroy()`: Free all allocated memory
- `dsdl_add_namespace()`: Register namespace directory
- `dsdl_read()`: Load and parse DSDL type definition
- `dsdl_serialize()`: Serialize message to byte buffer
- `dsdl_deserialize()`: Deserialize byte buffer to message
- `dsdl_serialized_footprint()`: Get maximum serialized size

## Design Constraints

- **C99 compliant**: No compiler extensions or platform-specific features
- **No stdio**: Library core doesn't use standard I/O
- **No heap**: Memory allocation via user-provided `realloc` callback
- **Single file**: Entire implementation in `dsdl.c`
- **Minimal dependencies**: Only `lib/wkv.h` for name lookups

## License

MIT License. See LICENSE file for details.

## Contributing

Contributions are welcome! Please follow the [Zubax Style Guide](specs/CODING_CONVENTIONS.md) and run `clang-format` before submitting.

## References

- [Cyphal Specification](https://opencyphal.org/specification/)
- [PyDSDL](https://github.com/OpenCyphal/pydsdl) - Reference implementation
- [Nunavut](https://github.com/OpenCyphal/nunavut) - DSDL code generator
- [libcanard](https://github.com/OpenCyphal/libcanard) - Cyphal protocol stack in C

## Support

- [Cyphal Forum](https://forum.opencyphal.org/)
- [GitHub Issues](https://github.com/OpenCyphal/dsdl.c/issues)
