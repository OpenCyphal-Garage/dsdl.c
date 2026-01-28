/// Temporary profiling tool to count allocations during DSDL parsing
///
/// This tool measures the number of allocations that occur when parsing
/// various DSDL files. Results are used to design OOM injection tests.
///
/// Usage: profile_allocations <test_root_dir>
/// Example: profile_allocations test_dsdl_root_namespaces

#include "dsdl.c"

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// ============================================================================
// Allocation tracking
// ============================================================================

typedef struct
{
    size_t count;
    size_t total_bytes;
} alloc_stats_t;

static alloc_stats_t g_stats = { 0, 0 };

static void* tracking_realloc(dsdl_t* self, void* ptr, size_t new_size)
{
    (void)self;

    if (new_size == 0) {
        free(ptr);
        return NULL;
    }

    g_stats.count++;
    g_stats.total_bytes += new_size;

    return realloc(ptr, new_size);
}

// ============================================================================
// File I/O callbacks
// ============================================================================

static wkv_str_t test_read_file(dsdl_t* self, wkv_str_t path)
{
    wkv_str_t result = { 0, NULL };

    char path_buf[512];
    if (path.len >= sizeof(path_buf)) {
        return result;
    }
    memcpy(path_buf, path.str, path.len);
    path_buf[path.len] = '\0';

    FILE* f = fopen(path_buf, "rb");
    if (f == NULL) {
        return result;
    }

    fseek(f, 0, SEEK_END);
    const long size = ftell(f);
    fseek(f, 0, SEEK_SET);

    if (size < 0) {
        fclose(f);
        return result;
    }

    char* buffer = (char*)self->realloc(self, NULL, (size_t)size);
    if (buffer == NULL) {
        fclose(f);
        return result;
    }

    const size_t read_count = fread(buffer, 1, (size_t)size, f);
    fclose(f);

    if (read_count != (size_t)size) {
        self->realloc(self, buffer, 0);
        return result;
    }

    result.len = (size_t)size;
    result.str = buffer;
    return result;
}

static wkv_str_t* test_list_dir(dsdl_t* self, wkv_str_t path)
{
    char path_buf[512];
    if (path.len >= sizeof(path_buf)) {
        return NULL;
    }
    memcpy(path_buf, path.str, path.len);
    path_buf[path.len] = '\0';

    DIR* dir = opendir(path_buf);
    if (dir == NULL) {
        return NULL;
    }

    size_t         count = 0;
    struct dirent* entry = NULL;
    while ((entry = readdir(dir)) != NULL) {
        if ((strcmp(entry->d_name, ".") == 0) || (strcmp(entry->d_name, "..") == 0)) {
            continue;
        }
        count++;
    }

    wkv_str_t* result = (wkv_str_t*)self->realloc(self, NULL, (count + 1) * sizeof(wkv_str_t));
    if (result == NULL) {
        closedir(dir);
        return NULL;
    }

    rewinddir(dir);
    size_t idx = 0;
    while ((entry = readdir(dir)) != NULL) {
        if ((strcmp(entry->d_name, ".") == 0) || (strcmp(entry->d_name, "..") == 0)) {
            continue;
        }
        const size_t name_len  = strlen(entry->d_name);
        char*        name_copy = (char*)self->realloc(self, NULL, name_len);
        if (name_copy == NULL) {
            for (size_t i = 0; i < idx; i++) {
                self->realloc(self, (void*)result[i].str, 0);
            }
            self->realloc(self, result, 0);
            closedir(dir);
            return NULL;
        }
        memcpy(name_copy, entry->d_name, name_len);
        result[idx].len = name_len;
        result[idx].str = name_copy;
        idx++;
    }

    result[idx].len = 0;
    result[idx].str = NULL;
    closedir(dir);
    return result;
}

// ============================================================================
// Profiling
// ============================================================================

static void profile_type(dsdl_t* dsdl, const char* type_name)
{
    g_stats.count       = 0;
    g_stats.total_bytes = 0;

    const dsdl_type_composite_t* type = dsdl_read(dsdl, wkv_key(type_name));

    if (type == NULL) {
        printf("  %-50s: FAILED (error %d)\n", type_name, dsdl->error);
    } else {
        printf("  %-50s: %6zu allocations, %8zu bytes\n", type_name, g_stats.count, g_stats.total_bytes);
    }
}

int main(int argc, char* argv[])
{
    if (argc != 2) {
        fprintf(stderr, "Usage: %s <test_root_dir>\n", argv[0]);
        fprintf(stderr, "Example: %s test_dsdl_root_namespaces\n", argv[0]);
        return 1;
    }

    const char* test_root = argv[1];

    // Initialize DSDL parser
    dsdl_t dsdl;
    dsdl_new(&dsdl, tracking_realloc, test_read_file, test_list_dir);

    // Add namespace roots
    char ns_path[512];

    snprintf(ns_path, sizeof(ns_path), "%s/0", test_root);
    if (!dsdl_add_namespace(&dsdl, wkv_key(ns_path))) {
        fprintf(stderr, "Failed to add namespace 0: error %d\n", dsdl.error);
        dsdl_destroy(&dsdl);
        return 1;
    }

    snprintf(ns_path, sizeof(ns_path), "%s/1", test_root);
    if (!dsdl_add_namespace(&dsdl, wkv_key(ns_path))) {
        fprintf(stderr, "Failed to add namespace 1: error %d\n", dsdl.error);
        dsdl_destroy(&dsdl);
        return 1;
    }

    printf("Profiling allocations for DSDL parsing\n");
    printf("======================================\n\n");

    // Profile simple types
    printf("Simple types:\n");
    profile_type(&dsdl, "validation.Simple.0.1");
    profile_type(&dsdl, "mymsgs.Simple.1.0");

    // Profile complex types
    printf("\nComplex types:\n");
    profile_type(&dsdl, "validation.LargeUnion.0.1");
    profile_type(&dsdl, "validation.Service.0.1");
    profile_type(&dsdl, "validation.Expressions.0.1");

    // Profile types with nested structures
    printf("\nNested/complex structures:\n");
    profile_type(&dsdl, "validation.HugeStruct.0.1");
    profile_type(&dsdl, "validation.ComplexExpressions.0.1");
    profile_type(&dsdl, "validation.NestedExpressions.0.1");

    // Profile types with references
    printf("\nTypes with references:\n");
    profile_type(&dsdl, "validation.subns.ShortRef.0.1");
    profile_type(&dsdl, "validation.subns.deep.DeepRef.0.1");

    printf("\n");
    dsdl_destroy(&dsdl);

    return 0;
}
