# Test suite for dsdl.c
# Uses ThrowTheSwitch Unity framework

# ------------------------------------------------------------------------------
# Unity test framework
# ------------------------------------------------------------------------------
add_library(unity STATIC
    unity/src/unity.c
)

target_include_directories(unity PUBLIC
    unity/src
)

# Disable warnings for Unity (third-party code)
target_compile_options(unity PRIVATE
    -w
)

# ------------------------------------------------------------------------------
# Test helper library (for internal access to dsdl.c)
# ------------------------------------------------------------------------------
# Some tests need access to internal functions. We achieve this by
# including dsdl.c directly into test files that need it, similar to
# libcanard/libudpard test patterns.

add_library(dsdl_test_internals INTERFACE)
target_include_directories(dsdl_test_internals INTERFACE
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/lib
)

# ------------------------------------------------------------------------------
# Test executable - Main test suite (C) - Public API tests
# ------------------------------------------------------------------------------
add_executable(test_dsdl
    test_main.c
)

target_link_libraries(test_dsdl PRIVATE
    dsdl
    unity
)

target_include_directories(test_dsdl PRIVATE
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/lib
)

add_test(NAME dsdl_tests COMMAND test_dsdl)

# ------------------------------------------------------------------------------
# Test executable - Serialization tests (includes dsdl.c directly)
# ------------------------------------------------------------------------------
add_executable(test_dsdl_serialization
    test_serialization.c
)

target_link_libraries(test_dsdl_serialization PRIVATE
    unity
    dsdl_test_internals
)

target_compile_definitions(test_dsdl_serialization PRIVATE
    DSDL_INTERNAL_TESTING=1
)

add_test(NAME dsdl_serialization_tests COMMAND test_dsdl_serialization
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
)

# ------------------------------------------------------------------------------
# Test executable - Parser tests (includes dsdl.c directly)
# ------------------------------------------------------------------------------
add_executable(test_dsdl_parser
    test_parser.c
)

target_link_libraries(test_dsdl_parser PRIVATE
    unity
    dsdl_test_internals
)

target_compile_definitions(test_dsdl_parser PRIVATE
    DSDL_INTERNAL_TESTING=1
)

add_test(NAME dsdl_parser_tests COMMAND test_dsdl_parser)

# ------------------------------------------------------------------------------
# Test executable - Rational arithmetic tests (includes dsdl.c directly)
# ------------------------------------------------------------------------------
add_executable(test_dsdl_rational
    test_rational.c
)

target_link_libraries(test_dsdl_rational PRIVATE
    unity
    dsdl_test_internals
)

target_compile_definitions(test_dsdl_rational PRIVATE
    DSDL_INTERNAL_TESTING=1
)

add_test(NAME dsdl_rational_tests COMMAND test_dsdl_rational)

# ------------------------------------------------------------------------------
# Test executable - C++20 API compatibility test
# ------------------------------------------------------------------------------
add_executable(test_dsdl_cpp
    test_api_cpp.cpp
)

target_link_libraries(test_dsdl_cpp PRIVATE
    dsdl
    unity
)

target_include_directories(test_dsdl_cpp PRIVATE
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/lib
)

# Ensure C++20 for this test
set_target_properties(test_dsdl_cpp PROPERTIES
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED ON
)

add_test(NAME dsdl_cpp_compatibility COMMAND test_dsdl_cpp)

# ------------------------------------------------------------------------------
# Test that links against dsdl.c directly for internal testing
# ------------------------------------------------------------------------------
add_executable(test_dsdl_internals
    test_internals.c
)

target_link_libraries(test_dsdl_internals PRIVATE
    unity
    dsdl_test_internals
)

# Don't link against dsdl library - we include dsdl.c directly
target_compile_definitions(test_dsdl_internals PRIVATE
    DSDL_INTERNAL_TESTING=1
)

add_test(NAME dsdl_internal_tests COMMAND test_dsdl_internals)

# ------------------------------------------------------------------------------
# Test executable - Type resolution tests (integration tests)
# ------------------------------------------------------------------------------
add_executable(test_dsdl_type_resolution
    test_type_resolution.c
)

target_link_libraries(test_dsdl_type_resolution PRIVATE
    unity
    dsdl_test_internals
)

target_compile_definitions(test_dsdl_type_resolution PRIVATE
    DSDL_INTERNAL_TESTING=1
)

add_test(NAME dsdl_type_resolution_tests COMMAND test_dsdl_type_resolution
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
)
