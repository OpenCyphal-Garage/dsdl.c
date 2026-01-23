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

# Add coverage flags
if(CMAKE_C_COMPILER_ID MATCHES "GNU")
    add_compile_options(--coverage -fprofile-arcs -ftest-coverage)
    add_link_options(--coverage)
elseif(CMAKE_C_COMPILER_ID MATCHES "Clang")
    add_compile_options(-fprofile-instr-generate -fcoverage-mapping)
    add_link_options(-fprofile-instr-generate -fcoverage-mapping)
endif()

# Find coverage tools
find_program(GCOVR_EXE NAMES gcovr)
find_program(LCOV_EXE NAMES lcov)
find_program(GENHTML_EXE NAMES genhtml)

if(GCOVR_EXE)
    message(STATUS "gcovr found: ${GCOVR_EXE}")

    # HTML coverage report
    add_custom_target(coverage
        COMMAND ${GCOVR_EXE}
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
