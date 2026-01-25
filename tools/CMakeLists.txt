# Developer tools for dsdl.c

add_executable(dsdl_to_dsdl
    dsdl_to_dsdl.c
    dsdl_tool_common.c
)

target_include_directories(dsdl_to_dsdl
    PRIVATE
        ${CMAKE_SOURCE_DIR}
        ${CMAKE_SOURCE_DIR}/lib
        ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(dsdl_to_dsdl
    PRIVATE
        dsdl
)

set_target_properties(dsdl_to_dsdl PROPERTIES
    C_STANDARD 99
    C_EXTENSIONS OFF
)

add_executable(dsdl_to_json
    dsdl_to_json.c
    dsdl_tool_common.c
)

target_include_directories(dsdl_to_json
    PRIVATE
        ${CMAKE_SOURCE_DIR}
        ${CMAKE_SOURCE_DIR}/lib
        ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(dsdl_to_json
    PRIVATE
        dsdl
)

set_target_properties(dsdl_to_json PROPERTIES
    C_STANDARD 99
    C_EXTENSIONS OFF
)
