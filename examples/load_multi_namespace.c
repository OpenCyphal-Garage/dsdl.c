/// Example: Load a DSDL type from multiple namespaces
///
/// This example demonstrates how to:
/// 1. Initialize the DSDL parser with file I/O callbacks
/// 2. Register multiple namespace directories
/// 3. Load a type definition that may depend on types from other namespaces
/// 4. Print type information
/// 5. Clean up resources
///
/// Usage:
///   ./load_multi_namespace <namespace_dir>... <type_name>
///
/// Example:
///   ./load_multi_namespace test_dsdl_root_namespaces/0 test_dsdl_root_namespaces/1 mymsgs.Simple.1.0

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <dsdl.h>

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
    if (argc < 3) {
        (void)fprintf(stderr,
                      "Usage: %s <namespace_dir>... <type_name>\n"
                      "\n"
                      "Example:\n"
                      "  %s test_dsdl_root_namespaces/0 test_dsdl_root_namespaces/1 mymsgs.Simple.1.0\n",
                      argv[0],
                      argv[0]);
        return 1;
    }

    // Extract type name (last argument)
    const char* type_name = argv[argc - 1];

    // Initialize DSDL parser
    dsdl_t dsdl;
    dsdl_new(&dsdl, dsdl_realloc);
    dsdl.read = dsdl_read_file;
    dsdl.list = dsdl_list_dir;

    // Register namespace directories (all arguments except the last one)
    for (int i = 1; i < argc - 1; i++) {
        const wkv_str_t ns_path = wkv_key(argv[i]);
        if (!dsdl_add_namespace(&dsdl, ns_path)) {
            (void)fprintf(stderr, "Failed to add namespace: %s\n", argv[i]);
            dsdl_destroy(&dsdl);
            return 1;
        }
        (void)printf("Added namespace: %s\n", argv[i]);
    }

    // Load the type
    (void)printf("Loading type: %s\n", type_name);
    const dsdl_type_composite_t* type = dsdl_read(&dsdl, wkv_key(type_name));
    if (type == NULL) {
        (void)fprintf(stderr, "Failed to load type: error %d\n", dsdl.error);
        dsdl_destroy(&dsdl);
        return 1;
    }

    // Print type information
    (void)printf("\n=== Type Information ===\n");
    (void)printf("Name: %.*s\n", (int)type->name.len, type->name.str);
    (void)printf("Versioned name: %.*s\n", (int)type->name_versioned.len, type->name_versioned.str);
    (void)printf("Version: %u.%u\n", type->version[0], type->version[1]);
    (void)printf("Extent: %llu bytes\n", (unsigned long long)type->extent);
    (void)printf("Sealed: %s\n", type->sealed ? "yes" : "no");
    (void)printf("Deprecated: %s\n", type->deprecated ? "yes" : "no");
    (void)printf("Field count: %zu\n", type->field_count);

    if (type->field_count > 0) {
        (void)printf("\nFields:\n");
        for (size_t i = 0; i < type->field_count; i++) {
            (void)printf("  [%zu] %.*s\n", i, (int)type->field_names[i].len, type->field_names[i].str);
        }
    }

    if (type->constant_count > 0) {
        (void)printf("\nConstants:\n");
        for (size_t i = 0; i < type->constant_count; i++) {
            (void)printf("  [%zu] %.*s\n", i, (int)type->constant_names[i].len, type->constant_names[i].str);
        }
    }

    // Cleanup
    dsdl_destroy(&dsdl);
    (void)printf("\nSuccess!\n");
    return 0;
}
