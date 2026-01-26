/// Example: Serializing DSDL types and JSON output
///
/// This example demonstrates how to:
/// 1. Initialize the DSDL parser with file I/O callbacks
/// 2. Register a namespace directory
/// 3. Load a type definition (mymsgs.Simple.1.0)
/// 4. Create field values for that type
/// 5. Serialize to binary buffer with dsdl_serialize()
/// 6. Print hex dump of serialized data
/// 7. Print JSON-like representation of the values
///
/// Usage:
///   ./serialize_to_json <namespace_path>
///
/// Example:
///   ./serialize_to_json test_dsdl_root_namespaces/0

#include <dsdl.h>

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// ============================================================================
// Memory allocator callback
// ============================================================================

static void* dsdl_realloc(dsdl_t* const self, void* const ptr, const size_t new_size)
{
    (void)self;
    if (new_size == 0U) {
        free(ptr);
        return NULL;
    }
    return realloc(ptr, new_size);
}

// ============================================================================
// File reader callback
// ============================================================================

static wkv_str_t dsdl_read_file(dsdl_t* const self, const wkv_str_t path)
{
    wkv_str_t result = { 0, NULL };

    char path_buf[DSDL_PATH_MAX];
    if (path.len >= sizeof(path_buf)) {
        return result;
    }
    (void)memcpy(path_buf, path.str, path.len);
    path_buf[path.len] = '\0';

    FILE* f = fopen(path_buf, "rb");
    if (f == NULL) {
        return result;
    }

    if (fseek(f, 0, SEEK_END) != 0) {
        (void)fclose(f);
        return result;
    }
    const long size = ftell(f);
    if (size < 0) {
        (void)fclose(f);
        return result;
    }
    if (fseek(f, 0, SEEK_SET) != 0) {
        (void)fclose(f);
        return result;
    }

    char* const buffer = (char*)self->realloc(self, NULL, (size_t)size);
    if (buffer == NULL) {
        (void)fclose(f);
        return result;
    }

    const size_t read_count = fread(buffer, 1U, (size_t)size, f);
    (void)fclose(f);
    if (read_count != (size_t)size) {
        self->realloc(self, buffer, 0U);
        return result;
    }

    result.len = (size_t)size;
    result.str = buffer;
    return result;
}

// ============================================================================
// Directory listing callback
// ============================================================================

static wkv_str_t* dsdl_list_dir(dsdl_t* const self, const wkv_str_t path)
{
    char path_buf[DSDL_PATH_MAX];
    if (path.len >= sizeof(path_buf)) {
        return NULL;
    }
    (void)memcpy(path_buf, path.str, path.len);
    path_buf[path.len] = '\0';

    DIR* const dir = opendir(path_buf);
    if (dir == NULL) {
        return NULL;
    }

    size_t         count = 0U;
    struct dirent* entry = NULL;
    while ((entry = readdir(dir)) != NULL) {
        if ((strcmp(entry->d_name, ".") == 0) || (strcmp(entry->d_name, "..") == 0)) {
            continue;
        }
        count++;
    }

    wkv_str_t* const result = (wkv_str_t*)self->realloc(self, NULL, (count + 1U) * sizeof(wkv_str_t));
    if (result == NULL) {
        closedir(dir);
        return NULL;
    }

    (void)rewinddir(dir);
    size_t idx = 0U;
    while ((entry = readdir(dir)) != NULL) {
        if ((strcmp(entry->d_name, ".") == 0) || (strcmp(entry->d_name, "..") == 0)) {
            continue;
        }

        const size_t name_len = strlen(entry->d_name);
        char* const  name_buf = (char*)self->realloc(self, NULL, name_len);
        if (name_buf == NULL) {
            for (size_t i = 0; i < idx; i++) {
                self->realloc(self, (void*)result[i].str, 0U);
            }
            self->realloc(self, result, 0U);
            (void)closedir(dir);
            return NULL;
        }
        (void)memcpy(name_buf, entry->d_name, name_len);
        result[idx].len = name_len;
        result[idx].str = name_buf;
        idx++;
    }

    (void)closedir(dir);
    result[idx].len = 0U;
    result[idx].str = NULL;
    return result;
}

// ============================================================================
// Main program
// ============================================================================

int main(int argc, char* argv[])
{
    if (argc < 2) {
        (void)fprintf(stderr, "Usage: %s <namespace_path>\n", argv[0]);
        (void)fprintf(stderr, "\nExample:\n");
        (void)fprintf(stderr, "  %s test_dsdl_root_namespaces/0\n", argv[0]);
        return 1;
    }

    // Initialize DSDL parser
    dsdl_t dsdl;
    dsdl_new(&dsdl, dsdl_realloc);
    dsdl.read = dsdl_read_file;
    dsdl.list = dsdl_list_dir;

    // Add namespace
    const wkv_str_t ns_path = wkv_key(argv[1]);
    if (!dsdl_add_namespace(&dsdl, ns_path)) {
        (void)fprintf(stderr, "Failed to add namespace: %s\n", argv[1]);
        dsdl_destroy(&dsdl);
        return 1;
    }
    (void)printf("Added namespace: %s\n\n", argv[1]);

    // Load mymsgs.Simple.1.0: int32 a, float16 b, bool c
    (void)printf("Loading type: mymsgs.Simple.1.0\n");
    const dsdl_type_composite_t* simple = dsdl_read(&dsdl, wkv_key("mymsgs.Simple.1.0"));
    if (simple == NULL) {
        (void)fprintf(stderr, "Failed to load type: error %d\n", dsdl.error);
        dsdl_destroy(&dsdl);
        return 1;
    }

    (void)printf("Type loaded successfully!\n");
    (void)printf("  Name: %.*s\n", (int)simple->name.len, simple->name.str);
    (void)printf("  Version: %u.%u\n", simple->version[0], simple->version[1]);
    (void)printf("  Extent: %llu bytes\n", (unsigned long long)simple->extent);
    (void)printf("  Field count: %zu\n\n", simple->field_count);

    // Create values: a=42, b=3.14, c=true
    int_least32_t       a        = 42;
    float               b        = 3.14F; // float16 stored as float
    bool                c        = true;
    void*               fields[] = { &a, &b, &c };
    dsdl_value_struct_t val      = { .values = fields };

    // Serialize
    uint8_t      buffer[64];
    dsdl_error_t err  = dsdl_error_none;
    size_t       size = dsdl_serialize(simple, &val, sizeof(buffer), buffer, &err);

    if (size == SIZE_MAX) {
        (void)fprintf(stderr, "Serialization failed: error %d\n", err);
        dsdl_destroy(&dsdl);
        return 1;
    }

    // Print hex dump
    (void)printf("Serialized %zu bytes:\n", size);
    (void)printf("Hex: ");
    for (size_t i = 0; i < size; i++) {
        (void)printf("%02X ", buffer[i]);
    }
    (void)printf("\n\n");

    // Print JSON-like representation
    (void)printf("{\n");
    (void)printf("  \"a\": %d,\n", (int)a);
    (void)printf("  \"b\": %.2f,\n", (double)b);
    (void)printf("  \"c\": %s\n", c ? "true" : "false");
    (void)printf("}\n");

    // Cleanup
    dsdl_destroy(&dsdl);
    (void)printf("\nSuccess!\n");
    return 0;
}
