cmake_minimum_required(VERSION 3.19)

if (${CMAKE_CURRENT_LIST_DIR} MATCHES ${CMAKE_SOURCE_DIR})
    project(extrachain LANGUAGES CXX DESCRIPTION "ExtraChain Core")
    add_compile_definitions(EXTRACHAIN_LIBRARY_ACTIVATE)
endif()

set(CMAKE_INCLUDE_CURRENT_DIR ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if (EXTRACHAIN_STATIC_BUILD)
    message("Build static ExtraChain")
endif()

# c++ settings
message("Compiler: ${CMAKE_CXX_COMPILER_ID} | front: ${CMAKE_CXX_COMPILER_FRONTEND_VARIANT} | build: ${CMAKE_BUILD_TYPE}")

if (CMAKE_CXX_COMPILER_ID STREQUAL "GNU" OR CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wpedantic -Werror=return-type -Werror=implicit-fallthrough")
    if (UNIX)
        set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fPIC")
    endif (UNIX)
endif()
if(MSVC)
    # add_compile_options(/W4 /analyze)
endif()


find_package(Qt6 COMPONENTS Core Network WebSockets REQUIRED)
find_package(unofficial-sodium CONFIG REQUIRED)
find_package(unofficial-sqlite3 CONFIG REQUIRED)
find_package(Boost REQUIRED COMPONENTS system thread mp11 describe json)
find_package(fmt CONFIG REQUIRED)
find_package(magic_enum CONFIG REQUIRED)
find_package(unofficial-hash-library CONFIG REQUIRED)
find_package(msgpack-cxx CONFIG REQUIRED)
find_package(blake3 CONFIG REQUIRED)

set(EXTRACHAIN_CORE_INCLUDES
    ${CMAKE_CURRENT_LIST_DIR}
    ${CMAKE_CURRENT_LIST_DIR}/headers
    )

set(EXTRACHAIN_CORE_SOURCES
    ${CMAKE_CURRENT_LIST_DIR}/extrachain_version.h
    ${CMAKE_CURRENT_LIST_DIR}/headers/dfs/dfs_controller.h
    ${CMAKE_CURRENT_LIST_DIR}/headers/dfs/dirs_manager.h
    ${CMAKE_CURRENT_LIST_DIR}/headers/dfs/load_manager.h
    ${CMAKE_CURRENT_LIST_DIR}/headers/dfs/version_controller.h
    ${CMAKE_CURRENT_LIST_DIR}/headers/dfs/historical_collection.h
    # ${CMAKE_CURRENT_LIST_DIR}/headers/dfs/permission_manasger.h
    ${CMAKE_CURRENT_LIST_DIR}/headers/dfs/dfs_utils.h
    ${CMAKE_CURRENT_LIST_DIR}/headers/dfs/name_validator.h
    ${CMAKE_CURRENT_LIST_DIR}/headers/dfs/collection_template.h
    ${CMAKE_CURRENT_LIST_DIR}/headers/dfs/dfs_vector.h
    ${CMAKE_CURRENT_LIST_DIR}/headers/chain/actor.h
    ${CMAKE_CURRENT_LIST_DIR}/headers/chain/actor_id.h
    ${CMAKE_CURRENT_LIST_DIR}/headers/chain/actor_index.h
    ${CMAKE_CURRENT_LIST_DIR}/headers/chain/actor_filter.h
    ${CMAKE_CURRENT_LIST_DIR}/headers/chain/transaction.h
    ${CMAKE_CURRENT_LIST_DIR}/headers/chain/transaction_cache.h
    ${CMAKE_CURRENT_LIST_DIR}/headers/chain/private_profile.h
    ${CMAKE_CURRENT_LIST_DIR}/headers/chain/dag.h
    ${CMAKE_CURRENT_LIST_DIR}/headers/chain/dag_cache.h
    ${CMAKE_CURRENT_LIST_DIR}/headers/encryption/encryption_tools.h
    ${CMAKE_CURRENT_LIST_DIR}/headers/encryption/key_private.h
    ${CMAKE_CURRENT_LIST_DIR}/headers/encryption/key_public.h
    ${CMAKE_CURRENT_LIST_DIR}/headers/managers/account_controller.h
    ${CMAKE_CURRENT_LIST_DIR}/headers/managers/extrachain_node.h
    ${CMAKE_CURRENT_LIST_DIR}/headers/managers/luminance_manager.h
    ${CMAKE_CURRENT_LIST_DIR}/headers/managers/data_mining_manager.h
    ${CMAKE_CURRENT_LIST_DIR}/headers/managers/logs_manager.h
    ${CMAKE_CURRENT_LIST_DIR}/headers/managers/thread_pool.h
    ${CMAKE_CURRENT_LIST_DIR}/headers/managers/data_mining_manager.h
    ${CMAKE_CURRENT_LIST_DIR}/headers/managers/file_data_manager.h
    ${CMAKE_CURRENT_LIST_DIR}/headers/managers/token_manager.h
    ${CMAKE_CURRENT_LIST_DIR}/headers/managers/thoth_manager.h
    ${CMAKE_CURRENT_LIST_DIR}/headers/managers/janus_manager.h
    ${CMAKE_CURRENT_LIST_DIR}/headers/metatypes.h
    ${CMAKE_CURRENT_LIST_DIR}/headers/extrachain_global.h
    ${CMAKE_CURRENT_LIST_DIR}/headers/network/discovery_service.h
    ${CMAKE_CURRENT_LIST_DIR}/headers/network/network_manager.h
    ${CMAKE_CURRENT_LIST_DIR}/headers/network/network_status.h
    ${CMAKE_CURRENT_LIST_DIR}/headers/network/message_body.h
    ${CMAKE_CURRENT_LIST_DIR}/headers/network/isocket_service.h
    ${CMAKE_CURRENT_LIST_DIR}/headers/network/websocket_service.h
    ${CMAKE_CURRENT_LIST_DIR}/headers/network/upnpconnection.h
    ${CMAKE_CURRENT_LIST_DIR}/headers/network/upnpconnector.h
    ${CMAKE_CURRENT_LIST_DIR}/headers/utils/autologinhash.h
    ${CMAKE_CURRENT_LIST_DIR}/headers/utils/bignumber.h
    ${CMAKE_CURRENT_LIST_DIR}/headers/utils/bignumber_float.h
    ${CMAKE_CURRENT_LIST_DIR}/headers/utils/fs_path.h
    ${CMAKE_CURRENT_LIST_DIR}/headers/utils/db_connector.h
    ${CMAKE_CURRENT_LIST_DIR}/headers/utils/db_connector_strict.h
    ${CMAKE_CURRENT_LIST_DIR}/headers/utils/db_schema.h
    ${CMAKE_CURRENT_LIST_DIR}/headers/utils/db_iterator.h
    ${CMAKE_CURRENT_LIST_DIR}/headers/utils/exc_utils.h
    ${CMAKE_CURRENT_LIST_DIR}/headers/utils/exc_utils_base64.h
    ${CMAKE_CURRENT_LIST_DIR}/headers/utils/exc_magic.h
    ${CMAKE_CURRENT_LIST_DIR}/headers/utils/exc_logs.h
    ${CMAKE_CURRENT_LIST_DIR}/headers/utils/exc_logs_filter.h
    ${CMAKE_CURRENT_LIST_DIR}/headers/utils/exc_commands.h
    ${CMAKE_CURRENT_LIST_DIR}/headers/utils/exc_logs_extra.h
    ${CMAKE_CURRENT_LIST_DIR}/headers/utils/exc_msgpack_describe.h
    ${CMAKE_CURRENT_LIST_DIR}/headers/utils/variant_model.h
    ${CMAKE_CURRENT_LIST_DIR}/headers/utils/safeptr.h
    ${CMAKE_CURRENT_LIST_DIR}/headers/utils/vpn_types.h
    ${CMAKE_CURRENT_LIST_DIR}/headers/utils/thread_pool_boost.h
    ${CMAKE_CURRENT_LIST_DIR}/headers/chat/chat_manager.h
    ${CMAKE_CURRENT_LIST_DIR}/headers/chat/chat.h
    ${CMAKE_CURRENT_LIST_DIR}/headers/chat/message.h
    ${CMAKE_CURRENT_LIST_DIR}/sources/dfs/dfs_controller.cpp
    ${CMAKE_CURRENT_LIST_DIR}/sources/dfs/dirs_manager.cpp
    ${CMAKE_CURRENT_LIST_DIR}/sources/dfs/load_manager.cpp
    ${CMAKE_CURRENT_LIST_DIR}/sources/dfs/collection_template.cpp
    ${CMAKE_CURRENT_LIST_DIR}/sources/dfs/version_controller.cpp
    ${CMAKE_CURRENT_LIST_DIR}/sources/dfs/historical_collection.cpp
    # ${CMAKE_CURRENT_LIST_DIR}/sources/dfs/permission_manager.cpp
    ${CMAKE_CURRENT_LIST_DIR}/sources/dfs/dfs_utils.cpp
    ${CMAKE_CURRENT_LIST_DIR}/sources/dfs/name_validator.cpp
    ${CMAKE_CURRENT_LIST_DIR}/sources/dfs/dfs_vector.cpp
    ${CMAKE_CURRENT_LIST_DIR}/sources/chain/actor_id.cpp
    ${CMAKE_CURRENT_LIST_DIR}/sources/chain/actor_index.cpp
    ${CMAKE_CURRENT_LIST_DIR}/sources/chain/transaction.cpp
    ${CMAKE_CURRENT_LIST_DIR}/sources/chain/transaction_cache.cpp
    ${CMAKE_CURRENT_LIST_DIR}/sources/chain/private_profile.cpp
    ${CMAKE_CURRENT_LIST_DIR}/sources/chain/dag.cpp
    ${CMAKE_CURRENT_LIST_DIR}/sources/chain/dag_cache.cpp
    ${CMAKE_CURRENT_LIST_DIR}/sources/encryption/encryption_tools.cpp
    ${CMAKE_CURRENT_LIST_DIR}/sources/encryption/key_private.cpp
    ${CMAKE_CURRENT_LIST_DIR}/sources/encryption/key_public.cpp
    ${CMAKE_CURRENT_LIST_DIR}/sources/managers/account_controller.cpp
    ${CMAKE_CURRENT_LIST_DIR}/sources/managers/data_mining_manager.cpp
    ${CMAKE_CURRENT_LIST_DIR}/sources/managers/file_data_manager.cpp
    ${CMAKE_CURRENT_LIST_DIR}/sources/managers/extrachain_node.cpp
    ${CMAKE_CURRENT_LIST_DIR}/sources/managers/luminance_manager.cpp
    ${CMAKE_CURRENT_LIST_DIR}/sources/managers/logs_manager.cpp
    ${CMAKE_CURRENT_LIST_DIR}/sources/managers/thread_pool.cpp
    ${CMAKE_CURRENT_LIST_DIR}/sources/managers/data_mining_manager.cpp
    ${CMAKE_CURRENT_LIST_DIR}/sources/managers/token_manager.cpp
    ${CMAKE_CURRENT_LIST_DIR}/sources/managers/thoth_manager.cpp
    ${CMAKE_CURRENT_LIST_DIR}/sources/managers/janus_manager.cpp
    ${CMAKE_CURRENT_LIST_DIR}/sources/network/discovery_service.cpp
    ${CMAKE_CURRENT_LIST_DIR}/sources/network/network_manager.cpp
    ${CMAKE_CURRENT_LIST_DIR}/sources/network/network_status.cpp
    ${CMAKE_CURRENT_LIST_DIR}/sources/network/isocket_service.cpp
    ${CMAKE_CURRENT_LIST_DIR}/sources/network/websocket_service.cpp
    ${CMAKE_CURRENT_LIST_DIR}/sources/network/upnpconnection.cpp
    ${CMAKE_CURRENT_LIST_DIR}/sources/network/upnpconnector.cpp
    ${CMAKE_CURRENT_LIST_DIR}/sources/utils/autologinhash.cpp
    ${CMAKE_CURRENT_LIST_DIR}/sources/utils/bignumber.cpp
    ${CMAKE_CURRENT_LIST_DIR}/sources/utils/bignumber_float.cpp
    ${CMAKE_CURRENT_LIST_DIR}/sources/utils/fs_path.cpp
    ${CMAKE_CURRENT_LIST_DIR}/sources/utils/db_connector.cpp
    ${CMAKE_CURRENT_LIST_DIR}/sources/utils/db_schema.cpp
    ${CMAKE_CURRENT_LIST_DIR}/sources/utils/db_iterator.cpp
    ${CMAKE_CURRENT_LIST_DIR}/sources/utils/exc_utils.cpp
    ${CMAKE_CURRENT_LIST_DIR}/sources/utils/variant_model.cpp
    ${CMAKE_CURRENT_LIST_DIR}/sources/utils/variant_model.cpp
    ${CMAKE_CURRENT_LIST_DIR}/sources/utils/thread_pool_boost.cpp
    ${CMAKE_CURRENT_LIST_DIR}/sources/chat/chat_manager.cpp
    ${CMAKE_CURRENT_LIST_DIR}/sources/chat/chat.cpp
    ${CMAKE_CURRENT_LIST_DIR}/sources/chat/message.cpp
    )

# add_library(extrachainc
#     ${CMAKE_CURRENT_LIST_DIR}/Cbindings/extrachainc.h ${CMAKE_CURRENT_LIST_DIR}/Cbindings/extrachainc.cpp
#     )

if (EXTRACHAIN_STATIC_BUILD)
    message("ExtraChain: static build")
    add_library(extrachain ${EXTRACHAIN_CORE_SOURCES})
else()
    add_compile_definitions(extrachain PRIVATE EXTRACHAIN_LIBRARY)
    message("ExtraChain: library build")
    add_library(extrachain SHARED ${EXTRACHAIN_CORE_SOURCES})
endif()

# link time optimization
if (EXTRACHAIN_ENABLE_LTO)
    set_property(TARGET extrachain PROPERTY POSITION_INDEPENDENT_CODE 1)
    include(CheckIPOSupported)
    check_ipo_supported(RESULT lto_supported OUTPUT error)
    if (lto_supported)
        message(STATUS "LTO enabled")
        set_property(TARGET extrachain PROPERTY INTERPROCEDURAL_OPTIMIZATION TRUE)
    else()
        message(STATUS "LTO not supported: <${error}>")
    endif(lto_supported)
endif(EXTRACHAIN_ENABLE_LTO)

# git
execute_process(COMMAND git --git-dir ${CMAKE_CURRENT_LIST_DIR}/.git --work-tree $$PWD describe --always --tags OUTPUT_VARIABLE GIT_COMMIT_CORE)
execute_process(COMMAND git --git-dir ${CMAKE_CURRENT_LIST_DIR}/.git --work-tree $$PWD symbolic-ref --short HEAD OUTPUT_VARIABLE GIT_BRANCH_CORE)
string(STRIP "${GIT_COMMIT_CORE}" GIT_COMMIT_CORE)
string(STRIP "${GIT_BRANCH_CORE}" GIT_BRANCH_CORE)
string(TIMESTAMP COMPILE_DATE "%m.%d")
add_compile_definitions(_CRT_SECURE_NO_WARNINGS EXTRACHAIN_CMAKE COMPILE_DATE="${COMPILE_DATE}" GIT_COMMIT_CORE="${GIT_COMMIT_CORE}" GIT_BRANCH_CORE="${GIT_BRANCH_CORE}" MAGIC_ENUM_RANGE_MIN=0 MAGIC_ENUM_RANGE_MAX=256)

target_precompile_headers(extrachain PRIVATE ${CMAKE_CURRENT_LIST_DIR}/headers/precompiled.h)
target_include_directories(extrachain PUBLIC ${EXTRACHAIN_CORE_INCLUDES})
# target_include_directories(extrachainc PUBLIC ${EXTRACHAIN_CORE_INCLUDES})

set(bip3x_BUILD_SHARED_LIBS OFF CACHE BOOL "")
set(bip3x_BUILD_JNI_BINDINGS OFF CACHE BOOL "")
set(bip3x_BUILD_C_BINDINGS OFF CACHE BOOL "")
set(bip3x_BUILD_TESTS OFF CACHE BOOL "")
set(bip3x_USE_OPENSSL_RANDOM OFF CACHE BOOL "")

add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../extrachain-3rdparty/bip3x ${CMAKE_CURRENT_BINARY_DIR}/bip3x)

target_include_directories(extrachain PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/../extrachain-3rdparty/bip3x/include)

target_link_libraries(extrachain
    Qt6::Core Qt6::Network Qt6::WebSockets
    unofficial-sodium::sodium unofficial::sqlite3::sqlite3 msgpack-cxx fmt::fmt-header-only BLAKE3::blake3 magic_enum::magic_enum
    Boost::boost Boost::system Boost::thread Boost::mp11 Boost::describe Boost::json bip3x)
# extrachainc
# target_link_libraries(extrachainc Qt6::Core Qt6::Network Qt6::WebSockets
#     unofficial-sodium::sodium unofficial::sqlite3::sqlite3 msgpack-cxx Boost::boost Boost::system Boost::thread)

set_property(TARGET extrachain PROPERTY POSITION_INDEPENDENT_CODE 1)

set(CMAKE_INCLUDE_CURRENT_DIR ON)
