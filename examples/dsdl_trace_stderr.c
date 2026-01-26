/// DSDL trace implementation for stderr
///
/// Add this file to your build to define dsdl_trace() that prints trace messages to stderr.
/// Only needed when DSDL_CONFIG_TRACE is defined and nonzero.

#include "dsdl.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

void dsdl_trace(dsdl_t* const       self,
                const char* const   file,
                const uint_fast16_t line,
                const char* const   func,
                const char* const   format,
                ...)
{
    (void)self;

    // Extract the file name from the path.
    const char* file_name = strrchr(file, '/');
    file_name             = (file_name == NULL) ? strrchr(file, '\\') : file_name;
    file_name             = (file_name != NULL) ? (file_name + 1) : file;

    // Print the header.
    (void)fprintf(stderr, "DSDL %s:%u %s: ", file_name, (unsigned)line, func);

    // Print the message.
    va_list args;
    va_start(args, format);
    (void)vfprintf(stderr, format, args);
    va_end(args);

    // Finalize.
    (void)fputc('\n', stderr);
    (void)fflush(stderr);
}
