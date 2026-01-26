#ifndef DSDL_TOOL_COMMON_H
#define DSDL_TOOL_COMMON_H

#include "dsdl.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>

typedef struct
{
    size_t       root_count;
    const char** roots;
    size_t       type_count;
    const char** types;
    bool         help_requested;
} dsdl_tool_args_t;

bool dsdl_tool_parse_args(dsdl_tool_args_t* out, int argc, char** argv);
void dsdl_tool_free_args(dsdl_tool_args_t* args);
void dsdl_tool_print_usage(FILE* out, const char* tool_name, const char* summary);

void dsdl_tool_init(dsdl_t* dsdl);
bool dsdl_tool_add_roots(dsdl_t* dsdl, const dsdl_tool_args_t* args);

#endif
