# Cyphal DSDL Parser in C
# https://github.com/OpenCyphal/dsdl.c
#
# CMake build configuration for dsdl.c library and tests.
#
# Usage:
#   mkdir build && cd build
#   cmake .. -DCMAKE_BUILD_TYPE=Debug
#   cmake --build .
#   ctest --output-on-failure

cmake_minimum_required(VERSION 3.16)
project(dsdl_c
    VERSION 0.1.0
    LANGUAGES C CXX
    DESCRIPTION "Cyphal DSDL parser in C"
)

# ------------------------------------------------------------------------------
# Build options
# ------------------------------------------------------------------------------
option(DSDL_BUILD_TESTS     "Build the test suite"           ON)
option(DSDL_ENABLE_COVERAGE "Enable code coverage (GCC/Clang)" OFF)
option(DSDL_ENABLE_SANITIZERS "Enable ASan/UBSan"            OFF)
option(DSDL_BUILD_TOOLS     "Build standalone tools"         ON)
option(DSDL_BUILD_EXAMPLES  "Build example programs"         ON)
option(DSDL_BUILD_X32_TESTS "Build 32-bit test variants (requires gcc-multilib)" ON)

# ------------------------------------------------------------------------------
# Language standards
# ------------------------------------------------------------------------------
set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Export compile_commands.json for tooling (clang-tidy, IDEs)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# ------------------------------------------------------------------------------
# Compiler warnings
# ------------------------------------------------------------------------------
if(CMAKE_C_COMPILER_ID MATCHES "Clang|GNU")
    add_compile_options(
        -Wall
        -Wextra
        -Werror
        -Wpedantic
        -Wcast-align
        -Wdouble-promotion
        -Wfloat-equal
        -Wformat=2
        -Wpointer-arith
        -Wshadow
        -Wswitch-enum
        -Wundef
        -Wconversion
        -Wsign-conversion
        -Wno-unused-function  # Several helper functions are intentionally unused; they may be used in future extensions or by library users.
    )
    if(CMAKE_C_COMPILER_ID MATCHES "GNU")
        add_compile_options(
            -Wlogical-op
            -Wstrict-overflow=2
        )
    endif()
endif()

# ------------------------------------------------------------------------------
# Sanitizers (optional)
# ------------------------------------------------------------------------------
if(DSDL_ENABLE_SANITIZERS)
    if(CMAKE_C_COMPILER_ID MATCHES "Clang|GNU")
        add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer)
        add_link_options(-fsanitize=address,undefined)
    endif()
endif()

# ------------------------------------------------------------------------------
# Code coverage (optional)
# ------------------------------------------------------------------------------
if(DSDL_ENABLE_COVERAGE)
    add_compile_definitions(NDEBUG)
    include(cmake/Coverage.cmake)
endif()

# ------------------------------------------------------------------------------
# dsdl library (header-only with single implementation file)
# ------------------------------------------------------------------------------
add_library(dsdl STATIC
    dsdl.c
)

target_include_directories(dsdl
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
        $<INSTALL_INTERFACE:include>
)

# WKV header library
target_include_directories(dsdl
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/lib
)

# Apply stricter warnings for the library
target_compile_options(dsdl PRIVATE
    $<$<C_COMPILER_ID:GNU,Clang>:-Wcast-qual>
)

# Link math library (required for pow, floor, etc.)
target_link_libraries(dsdl PUBLIC m)

# ------------------------------------------------------------------------------
# Tests
# ------------------------------------------------------------------------------
if(DSDL_BUILD_TESTS)
    enable_testing()
    set(CMAKE_CTEST_ARGUMENTS "-V")  # Enable test outputs when running `make test`
    add_subdirectory(tests)
endif()

# Examples
# ------------------------------------------------------------------------------
if(DSDL_BUILD_EXAMPLES)
    add_subdirectory(examples)
endif()

# Tools
# ------------------------------------------------------------------------------
if(DSDL_BUILD_TOOLS)
    add_subdirectory(tools)
endif()

# ------------------------------------------------------------------------------
# Clang-Format integration
# ------------------------------------------------------------------------------
find_program(clang_format NAMES clang-format)
if (NOT clang_format)
    message(STATUS "Could not locate clang-format")
else ()
    set(format_patterns
            "${CMAKE_CURRENT_SOURCE_DIR}/dsdl.[ch]"
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_*.[ch]"
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_*.cpp"
            "${CMAKE_CURRENT_SOURCE_DIR}/examples/*.[ch]"
            "${CMAKE_CURRENT_SOURCE_DIR}/examples/*.cpp"
            "${CMAKE_CURRENT_SOURCE_DIR}/tools/*.[ch]"
            "${CMAKE_CURRENT_SOURCE_DIR}/tools/*.cpp")
    set(format_files "")
    foreach(pattern IN LISTS format_patterns)
        file(GLOB files
                CONFIGURE_DEPENDS
                ${pattern})
        list(APPEND format_files ${files})
    endforeach()
    list(REMOVE_DUPLICATES format_files)

    message(STATUS "Using clang-format: ${clang_format}")
    add_custom_target(format
            COMMAND ${clang_format}
                    -i
                    -fallback-style=none
                    -style=file
                    --verbose
                    ${format_files})
    add_custom_target(check-format
            COMMAND ${clang_format}
                    --dry-run
                    --Werror
                    -fallback-style=none
                    -style=file
                    --verbose
                    ${format_files})
endif ()

# ------------------------------------------------------------------------------
# Clang-tidy integration
# ------------------------------------------------------------------------------
find_program(CLANG_TIDY_EXE NAMES clang-tidy)
if(CLANG_TIDY_EXE)
    set(CMAKE_C_CLANG_TIDY "${CLANG_TIDY_EXE}")
    message(STATUS "clang-tidy found: ${CLANG_TIDY_EXE}")
else()
    message(STATUS "clang-tidy not found, static analysis disabled")
endif()

# ------------------------------------------------------------------------------
# Summary
# ------------------------------------------------------------------------------
message(STATUS "")
message(STATUS "dsdl.c configuration:")
message(STATUS "  Build type:      ${CMAKE_BUILD_TYPE}")
message(STATUS "  C standard:      ${CMAKE_C_STANDARD}")
message(STATUS "  C++ standard:    ${CMAKE_CXX_STANDARD}")
message(STATUS "  Build tests:     ${DSDL_BUILD_TESTS}")
message(STATUS "  Coverage:        ${DSDL_ENABLE_COVERAGE}")
message(STATUS "  Sanitizers:      ${DSDL_ENABLE_SANITIZERS}")
message(STATUS "  Build examples:  ${DSDL_BUILD_EXAMPLES}")
message(STATUS "  Build tools:     ${DSDL_BUILD_TOOLS}")
message(STATUS "")
