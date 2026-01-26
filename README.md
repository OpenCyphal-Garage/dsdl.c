<div align="center">

# Cyphal DSDL Parser & Serializer in C

[![CI](https://github.com/pavel-kirienko/dsdl.c/actions/workflows/ci.yml/badge.svg)](https://github.com/pavel-kirienko/dsdl.c/actions/workflows/ci.yml)

</div>

-----

A compact baremetal-friendly C99 implementation of a [Cyphal](https://opencyphal.org/) DSDL parser that allows loading DSDL definitions at runtime without compile-time code generation.

Features:

- **Runtime type loading**: Parse DSDL files at runtime without code generation
- **Baremetal-friendly**: No stdio, no heap (user-provided `realloc` callback)
- **Minimal dependencies**: Single-file implementation (`dsdl.c`), only requires `lib/wkv.h`
- **C99 compliant**: Portable across all standard-compliant compilers
- **Platform-agnostic**: No assumptions about pointer width, endianness, or execution environment
- **Serialization/deserialization**: Full support for Cyphal message encoding/decoding
- **Cross-validated**: Tested against PyDSDL and Nunavut reference implementations
- **Comprehensive error reporting**: Detailed error codes for all failure modes

## Usage

To use the library, simply add `dsdl.c` to your build and add `dsdl.h` to your include paths. Also, add [`wkv.h`](https://github.com/pavel-kirienko/wild_key_value) to your include paths -- a single-header key-value container with pattern matching.

See the `examples/` directory for additional examples.

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
    dsdl_new(&dsdl, my_realloc, my_read_file, my_list_dir);
    
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

## API Reference

See [`dsdl.h`](dsdl.h) for the complete API documentation.

## Tools

The project includes command-line tools for working with DSDL.
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
build/tests/test_dsdl_x64
```

The following standalone tools live under `tools/` and use the public API; they are used for verification:

```sh
build/tools/dsdl_to_dsdl -r test_dsdl_root_namespaces/0 -r test_dsdl_root_namespaces/1 validation.Expressions.0.1
build/tools/dsdl_to_json -r test_dsdl_root_namespaces/0 -r test_dsdl_root_namespaces/1 validation.Expressions.0.1
```

`dsdl_to_dsdl` emits normalized DSDL using constant types from the public API.

## Code Coverage

Generate coverage report:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug -DDSDL_ENABLE_COVERAGE=ON
cmake --build build
ctest --test-dir build
cmake --build build --target coverage
```

Coverage reports are generated in `build/coverage/`.

## Design Constraints

- **C99 compliant**: No compiler extensions or platform-specific features
- **No stdio**: Library core doesn't use standard I/O
- **No heap**: Memory allocation via user-provided `realloc` callback
- **Single file**: Entire implementation in `dsdl.c`
- **Minimal dependencies**: Only `lib/wkv.h` for name lookups

## References

- [Cyphal Specification](https://opencyphal.org/specification/)
- [PyDSDL](https://github.com/OpenCyphal/pydsdl) - DSDL parser reference implementation in Python
- [Nunavut](https://github.com/OpenCyphal/nunavut) - DSDL code generator
