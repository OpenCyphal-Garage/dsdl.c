# Code coverage configuration for GCC/Clang
#
# Usage:
#   cmake .. -DCMAKE_BUILD_TYPE=Debug -DDSDL_ENABLE_COVERAGE=ON
#   cmake --build .
#   ctest
#   cmake --build . --target coverage
#
# Requires: gcovr or lcov

if(NOT CMAKE_BUILD_TYPE STREQUAL "Debug")
    message(WARNING "Code coverage requires Debug build type for accurate results")
endif()

# Add coverage flags (gcov-style for GCC/Clang)
if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    add_compile_options(--coverage -fprofile-arcs -ftest-coverage)
    add_link_options(--coverage)
endif()

# Find coverage tools
find_program(GCOVR_EXE NAMES gcovr)
find_program(LCOV_EXE NAMES lcov)
find_program(GENHTML_EXE NAMES genhtml)
find_program(LLVM_COV_EXE NAMES llvm-cov)

set(GCOVR_GCOV_EXE "")
if(CMAKE_C_COMPILER_ID MATCHES "Clang")
    if(LLVM_COV_EXE)
        set(GCOVR_GCOV_EXE "${LLVM_COV_EXE} gcov")
    else()
        message(STATUS "llvm-cov not found; clang coverage may require a compatible gcov")
    endif()
endif()

if(GCOVR_EXE)
    message(STATUS "gcovr found: ${GCOVR_EXE}")
    set(GCOVR_GCOV_ARGS "")
    if(GCOVR_GCOV_EXE)
        list(APPEND GCOVR_GCOV_ARGS --gcov-executable "${GCOVR_GCOV_EXE}")
    endif()

    # HTML coverage report
    add_custom_target(coverage
        COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_BINARY_DIR}/coverage
        COMMAND ${GCOVR_EXE}
            ${GCOVR_GCOV_ARGS}
            --root ${CMAKE_SOURCE_DIR}
            --exclude '${CMAKE_SOURCE_DIR}/tests/.*'
            --exclude '${CMAKE_SOURCE_DIR}/reference_implementations/.*'
            --html-details ${CMAKE_BINARY_DIR}/coverage/index.html
            --print-summary
        WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
        COMMENT "Generating HTML coverage report with gcovr"
    )

    # Text coverage summary
    add_custom_target(coverage-summary
        COMMAND ${GCOVR_EXE}
            ${GCOVR_GCOV_ARGS}
            --root ${CMAKE_SOURCE_DIR}
            --exclude '${CMAKE_SOURCE_DIR}/tests/.*'
            --exclude '${CMAKE_SOURCE_DIR}/reference_implementations/.*'
            --print-summary
        WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
        COMMENT "Generating coverage summary with gcovr"
    )

elseif(LCOV_EXE AND GENHTML_EXE)
    message(STATUS "lcov found: ${LCOV_EXE}")

    add_custom_target(coverage
        COMMAND ${LCOV_EXE} --capture --directory . --output-file coverage.info
        COMMAND ${LCOV_EXE} --remove coverage.info '/usr/*' '*/tests/*' --output-file coverage.info
        COMMAND ${GENHTML_EXE} coverage.info --output-directory coverage
        WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
        COMMENT "Generating HTML coverage report with lcov"
    )
else()
    message(STATUS "No coverage tool found (gcovr or lcov), coverage target disabled")
endif()
