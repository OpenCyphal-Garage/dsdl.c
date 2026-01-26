# Examples for dsdl.c

# Copy trace implementation from tests
configure_file(
    ${CMAKE_SOURCE_DIR}/tests/dsdl_trace_stderr.c
    ${CMAKE_CURRENT_SOURCE_DIR}/dsdl_trace_stderr.c
    COPYONLY
)

# Helper function to add examples
function(add_dsdl_example name)
    add_executable(${name} ${name}.c dsdl_trace_stderr.c)
    target_include_directories(${name} PRIVATE
        ${CMAKE_SOURCE_DIR}
        ${CMAKE_SOURCE_DIR}/lib
    )
    target_compile_definitions(${name} PRIVATE DSDL_CONFIG_TRACE=1)
    target_link_libraries(${name} PRIVATE dsdl)
    set_target_properties(${name} PROPERTIES 
        C_STANDARD 99 
        C_EXTENSIONS OFF
    )
endfunction()

add_dsdl_example(load_multi_namespace)
add_dsdl_example(serialize_to_json)
