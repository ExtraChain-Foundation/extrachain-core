# ExtraChain Core — C FFI Bindings
# Builds libextrachainc as a shared library

set(EXC_C_SOURCES
    src/exc_bridge.cpp
    src/exc_node.cpp
    src/exc_account.cpp
    src/exc_crypto.cpp
    src/exc_dag.cpp
    src/exc_dfs.cpp
    src/exc_network.cpp
    src/exc_chat.cpp
    src/exc_janus.cpp
    src/exc_token.cpp
    src/exc_contract.cpp
)

set(EXC_C_HEADERS
    extrachain_c.h
    extrachain_c_types.h
    src/exc_internal.h
)

add_library(extrachainc SHARED ${EXC_C_SOURCES} ${EXC_C_HEADERS})

target_compile_definitions(extrachainc PRIVATE EXTRACHAIN_C_BUILDING)

target_include_directories(extrachainc
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${CMAKE_SOURCE_DIR}/headers
        ${CMAKE_SOURCE_DIR}
)

target_link_libraries(extrachainc PRIVATE extrachain)

target_precompile_headers(extrachainc PRIVATE
    "${CMAKE_SOURCE_DIR}/headers/precompiled.h"
)

set_target_properties(extrachainc PROPERTIES
    C_STANDARD 11
    CXX_STANDARD 23
    POSITION_INDEPENDENT_CODE ON
    PUBLIC_HEADER "extrachain_c.h;extrachain_c_types.h"
    VERSION ${PROJECT_VERSION}
    SOVERSION ${PROJECT_VERSION_MAJOR}
)

# Install rules
install(TARGETS extrachainc
    LIBRARY DESTINATION lib
    ARCHIVE DESTINATION lib
    RUNTIME DESTINATION bin
    PUBLIC_HEADER DESTINATION include/extrachain
)

# Example (optional, not built by default)
if(EXTRACHAIN_BUILD_CBINDINGS_EXAMPLE)
    add_executable(extrachain_c_example example/main.c)
    target_link_libraries(extrachain_c_example PRIVATE extrachainc)
    target_include_directories(extrachain_c_example PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
endif()
