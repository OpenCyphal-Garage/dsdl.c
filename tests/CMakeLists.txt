# Test suite for dsdl.c
# Uses ThrowTheSwitch Unity framework

set(unity_root "${CMAKE_CURRENT_SOURCE_DIR}/unity")

function(gen_test name files compile_flags link_flags c_standard is_cpp)
    # Unity
    add_library("${name}_unity" STATIC "${unity_root}/src/unity.c")
    target_include_directories("${name}_unity" SYSTEM PUBLIC "${unity_root}/src")
    set_target_properties(
        "${name}_unity"
        PROPERTIES
        COMPILE_FLAGS "${compile_flags} -w"
        LINK_FLAGS "${link_flags}"
        C_STANDARD "${c_standard}"
        C_EXTENSIONS OFF
    )

    # Test executable
    add_executable(${name} ${files})
    target_include_directories(${name} PUBLIC
        ${CMAKE_SOURCE_DIR}
        ${CMAKE_SOURCE_DIR}/lib
        ${unity_root}/src
    )
    target_compile_definitions(${name} PUBLIC DSDL_CONFIG_TRACE=1 DSDL_TEST_ROOT=\"${CMAKE_SOURCE_DIR}\")
    target_link_libraries(${name} "${name}_unity" m)
    set_target_properties(
        ${name}
        PROPERTIES
        COMPILE_FLAGS "${compile_flags}"
        LINK_FLAGS "${link_flags}"
    )

    if (is_cpp)
        set_target_properties(${name} PROPERTIES
            CXX_STANDARD 20
            CXX_STANDARD_REQUIRED ON
            CXX_EXTENSIONS OFF
        )
    else()
        set_target_properties(${name} PROPERTIES
            C_STANDARD "${c_standard}"
            C_EXTENSIONS OFF
        )
    endif()

    add_test(NAME "run_${name}" COMMAND ${name})
endfunction()

function(gen_test_matrix name files is_cpp)
    gen_test("${name}_x64" "${files}" "-m64" "-m64" "99" ${is_cpp})
    if(DSDL_BUILD_X32_TESTS)
        gen_test("${name}_x32" "${files}" "-m32" "-m32" "99" ${is_cpp})
    endif()
endfunction()

set(trace_source "${CMAKE_CURRENT_SOURCE_DIR}/dsdl_trace_stderr.c")
set(dsdl_source "${CMAKE_SOURCE_DIR}/dsdl.c")

# Main API tests (public API)
gen_test_matrix(test_dsdl "${dsdl_source};${trace_source};${CMAKE_CURRENT_SOURCE_DIR}/test_main.c" FALSE)

# Serialization tests (include dsdl.c directly)
gen_test_matrix(test_dsdl_serialization "${trace_source};${CMAKE_CURRENT_SOURCE_DIR}/test_serialization.c" FALSE)

# Parser tests (include dsdl.c directly)
gen_test_matrix(test_dsdl_parser "${trace_source};${CMAKE_CURRENT_SOURCE_DIR}/test_parser.c" FALSE)

# Rational arithmetic tests (include dsdl.c directly)
gen_test_matrix(test_dsdl_rational "${trace_source};${CMAKE_CURRENT_SOURCE_DIR}/test_rational.c" FALSE)

# C++20 API compatibility test
gen_test_matrix(test_dsdl_cpp "${dsdl_source};${trace_source};${CMAKE_CURRENT_SOURCE_DIR}/test_api_cpp.cpp" TRUE)

# Internal tests (include dsdl.c directly)
gen_test_matrix(test_dsdl_internals "${trace_source};${CMAKE_CURRENT_SOURCE_DIR}/test_internals.c" FALSE)

# Type resolution tests (include dsdl.c directly)
gen_test_matrix(test_dsdl_type_resolution "${trace_source};${CMAKE_CURRENT_SOURCE_DIR}/test_type_resolution.c" FALSE)

# Bit Length Set tests (include dsdl.c directly)
gen_test_matrix(test_dsdl_bls "${trace_source};${CMAKE_CURRENT_SOURCE_DIR}/test_bls.c" FALSE)

# Error path tests (include dsdl.c directly)
gen_test_matrix(test_dsdl_error_paths "${trace_source};${CMAKE_CURRENT_SOURCE_DIR}/test_error_paths.c" FALSE)

# Nunavut cross-validation tests
find_package(Python3 COMPONENTS Interpreter)
if(Python3_Interpreter_FOUND)
    set(nunavut_out_dir "${CMAKE_BINARY_DIR}/nunavut_out")
    
    add_custom_command(
         OUTPUT "${nunavut_out_dir}/.generated"
         COMMAND ${CMAKE_COMMAND} -E env --unset=CYPHAL_PATH --unset=DSDL_INCLUDE_PATH
             ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tools/generate_nunavut_code.py
             --output-dir ${nunavut_out_dir}
             --test-root ${CMAKE_SOURCE_DIR}
         COMMAND ${CMAKE_COMMAND} -E touch "${nunavut_out_dir}/.generated"
         DEPENDS ${CMAKE_SOURCE_DIR}/tools/generate_nunavut_code.py
         COMMENT "Generating Nunavut C code for cross-validation"
     )
    
    add_custom_target(generate_nunavut_code
        DEPENDS "${nunavut_out_dir}/.generated"
    )
    
    function(gen_nunavut_test name files compile_flags link_flags c_standard)
        add_library("${name}_unity" STATIC "${unity_root}/src/unity.c")
        target_include_directories("${name}_unity" SYSTEM PUBLIC "${unity_root}/src")
        set_target_properties(
            "${name}_unity"
            PROPERTIES
            COMPILE_FLAGS "${compile_flags} -w"
            LINK_FLAGS "${link_flags}"
            C_STANDARD "${c_standard}"
            C_EXTENSIONS OFF
        )
        
        add_executable(${name} ${files})
        target_include_directories(${name} PUBLIC
            ${CMAKE_SOURCE_DIR}
            ${CMAKE_SOURCE_DIR}/lib
            ${unity_root}/src
            ${nunavut_out_dir}
        )
        target_compile_definitions(${name} PUBLIC DSDL_CONFIG_TRACE=1 DSDL_TEST_ROOT=\"${CMAKE_SOURCE_DIR}\")
        target_link_libraries(${name} "${name}_unity" m)
        set_target_properties(
            ${name}
            PROPERTIES
            COMPILE_FLAGS "${compile_flags}"
            LINK_FLAGS "${link_flags}"
            C_STANDARD "${c_standard}"
            C_EXTENSIONS OFF
        )
        
        add_dependencies(${name} generate_nunavut_code)
        add_test(NAME "run_${name}" COMMAND ${name})
    endfunction()
    
    gen_nunavut_test(test_dsdl_nunavut_crossval_x64 "${trace_source};${CMAKE_CURRENT_SOURCE_DIR}/test_nunavut_crossval.c" "-m64" "-m64" "11")
    if(DSDL_BUILD_X32_TESTS)
        gen_nunavut_test(test_dsdl_nunavut_crossval_x32 "${trace_source};${CMAKE_CURRENT_SOURCE_DIR}/test_nunavut_crossval.c" "-m32" "-m32" "11")
    endif()
else()
    message(STATUS "Python3 not found; Nunavut cross-validation test disabled")
endif()

if(DSDL_BUILD_TOOLS)
    find_package(Python3 COMPONENTS Interpreter)
    if(Python3_Interpreter_FOUND)
        add_test(NAME run_dsdl_pydsdl_parity
             COMMAND ${CMAKE_COMMAND} -E env --unset=CYPHAL_PATH --unset=DSDL_INCLUDE_PATH
                 ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tools/dsdl_compare_pydsdl.py
                 --dsdl-to-json $<TARGET_FILE:dsdl_to_json>
                 --dsdl-to-dsdl $<TARGET_FILE:dsdl_to_dsdl>
                 -r ${CMAKE_SOURCE_DIR}/test_dsdl_root_namespaces/0
                 -r ${CMAKE_SOURCE_DIR}/test_dsdl_root_namespaces/1
         )
    else()
        message(STATUS "Python3 not found; PyDSDL parity test disabled")
    endif()
endif()
