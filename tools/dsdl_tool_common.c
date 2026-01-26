#include "dsdl_tool_common.h"

#include <dirent.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>

static void* dsdl_tool_realloc(dsdl_t* const self, void* const ptr, const size_t new_size)
{
    (void)self;
    if (new_size == 0U) {
        free(ptr);
        return NULL;
    }
    return realloc(ptr, new_size);
}

static wkv_str_t dsdl_tool_read_file(dsdl_t* const self, const wkv_str_t path)
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
        fclose(f);
        return result;
    }
    const long size = ftell(f);
    if (size < 0) {
        fclose(f);
        return result;
    }
    if (fseek(f, 0, SEEK_SET) != 0) {
        fclose(f);
        return result;
    }

    char* const buffer = (char*)self->realloc(self, NULL, (size_t)size);
    if (buffer == NULL) {
        fclose(f);
        return result;
    }

    const size_t read_count = fread(buffer, 1U, (size_t)size, f);
    fclose(f);
    if (read_count != (size_t)size) {
        self->realloc(self, buffer, 0U);
        return result;
    }

    result.len = (size_t)size;
    result.str = buffer;
    return result;
}

static wkv_str_t* dsdl_tool_list_dir(dsdl_t* const self, const wkv_str_t path)
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

    rewinddir(dir);
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
            closedir(dir);
            return NULL;
        }
        (void)memcpy(name_buf, entry->d_name, name_len);
        result[idx].len = name_len;
        result[idx].str = name_buf;
        idx++;
    }

    closedir(dir);
    result[idx].len = 0U;
    result[idx].str = NULL;
    return result;
}

static bool dsdl_tool_args_push(const char* const value, const char*** const items, size_t* const count)
{
    const size_t new_count = *count + 1U;
    const char** new_items = (const char**)realloc((void*)(*items), new_count * sizeof(*new_items));
    if (new_items == NULL) {
        return false;
    }
    new_items[*count] = value;
    *items            = new_items;
    *count            = new_count;
    return true;
}

bool dsdl_tool_parse_args(dsdl_tool_args_t* const out, const int argc, char** const argv)
{
    if ((out == NULL) || (argv == NULL)) {
        return false;
    }
    (void)memset(out, 0, sizeof(*out));

    bool stop_options = false;
    for (int i = 1; i < argc; i++) {
        const char* const arg = argv[i];
        if (!stop_options && ((strcmp(arg, "-h") == 0) || (strcmp(arg, "--help") == 0))) {
            out->help_requested = true;
            return true;
        }
        if (!stop_options && (strcmp(arg, "--") == 0)) {
            stop_options = true;
            continue;
        }
        if (!stop_options && ((strcmp(arg, "-r") == 0) || (strcmp(arg, "--root") == 0))) {
            if (i + 1 >= argc) {
                return false;
            }
            if (!dsdl_tool_args_push(argv[++i], &out->roots, &out->root_count)) {
                return false;
            }
            continue;
        }
        if (!stop_options && ((strcmp(arg, "-t") == 0) || (strcmp(arg, "--type") == 0))) {
            if (i + 1 >= argc) {
                return false;
            }
            if (!dsdl_tool_args_push(argv[++i], &out->types, &out->type_count)) {
                return false;
            }
            continue;
        }
        if (!stop_options && (arg[0] == '-')) {
            return false;
        }
        if (!dsdl_tool_args_push(arg, &out->types, &out->type_count)) {
            return false;
        }
    }

    return true;
}

void dsdl_tool_free_args(dsdl_tool_args_t* const args)
{
    if (args == NULL) {
        return;
    }
    free((void*)args->roots);
    free((void*)args->types);
    (void)memset(args, 0, sizeof(*args));
}

void dsdl_tool_print_usage(FILE* const out, const char* const tool_name, const char* const summary)
{
    const char* const name = (tool_name != NULL) ? tool_name : "dsdl_tool";
    if ((summary != NULL) && (summary[0] != '\0')) {
        (void)fprintf(out, "%s\n\n", summary);
    }
    (void)fprintf(out,
                  "Usage: %s -r <root>... [--] <type>...\n"
                  "Options:\n"
                  "  -r, --root <dir>   Add a root namespace directory (repeatable)\n"
                  "  -t, --type <name>  Add a type name to process (repeatable)\n"
                  "  -h, --help         Show this help text\n",
                  name);
}

void dsdl_tool_init(dsdl_t* const dsdl)
{
    dsdl_new(dsdl, dsdl_tool_realloc);
    dsdl->read = dsdl_tool_read_file;
    dsdl->list = dsdl_tool_list_dir;
}

bool dsdl_tool_add_roots(dsdl_t* const dsdl, const dsdl_tool_args_t* const args)
{
    if ((dsdl == NULL) || (args == NULL)) {
        return false;
    }
    for (size_t i = 0; i < args->root_count; i++) {
        if (!dsdl_add_namespace(dsdl, wkv_key(args->roots[i]))) {
            return false;
        }
    }
    return true;
}
