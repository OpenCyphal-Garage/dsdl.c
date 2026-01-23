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
        -Wno-error=padded
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

# ------------------------------------------------------------------------------
# Tests
# ------------------------------------------------------------------------------
if(DSDL_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

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
message(STATUS "")
