cmake_minimum_required(VERSION 3.19)

file(STRINGS "${CMAKE_CURRENT_LIST_DIR}/extrachain_version" EXTRACHAIN_VERSION)

if (${CMAKE_CURRENT_LIST_DIR} MATCHES ${CMAKE_SOURCE_DIR})
    project(extrachain LANGUAGES CXX VERSION ${EXTRACHAIN_VERSION} DESCRIPTION "ExtraChain Core")
    add_compile_definitions(EXTRACHAIN_LIBRARY_ACTIVATE)
endif()

set(CMAKE_INCLUDE_CURRENT_DIR ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if (EXTRACHAIN_STATIC_BUILD)
    message("Build static ExtraChain")
endif()

# c++ settings
message("Compiler: ${CMAKE_CXX_COMPILER_ID} | front: ${CMAKE_CXX_COMPILER_FRONTEND_VARIANT} | build: ${CMAKE_BUILD_TYPE}")

if (CMAKE_CXX_COMPILER_ID STREQUAL "GNU" OR CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
    message("Use flags")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wpedantic -Werror=return-type -Werror=implicit-fallthrough")
    if (UNIX)
        set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fPIC")
    endif (UNIX)
endif()

find_package(Qt6 COMPONENTS Core Network Concurrent WebSockets REQUIRED)
find_package(unofficial-sodium CONFIG REQUIRED)
find_package(unofficial-sqlite3 CONFIG REQUIRED)

set(EXTRACHAIN_CORE_INCLUDES
    ${CMAKE_CURRENT_LIST_DIR}
    ${CMAKE_CURRENT_LIST_DIR}/headers
    )

set(EXTRACHAIN_CORE_SOURCES
    ${CMAKE_CURRENT_LIST_DIR}/dfs/controls/headers/dfs.h
    ${CMAKE_CURRENT_LIST_DIR}/dfs/controls/headers/subscribe_controller.h
    ${CMAKE_CURRENT_LIST_DIR}/dfs/controls/sources/dfs.cpp
    ${CMAKE_CURRENT_LIST_DIR}/dfs/controls/sources/subscribe_controller.cpp
    ${CMAKE_CURRENT_LIST_DIR}/dfs/managers/headers/card_manager.h
    ${CMAKE_CURRENT_LIST_DIR}/dfs/managers/headers/dfs_networkmanager.h
    ${CMAKE_CURRENT_LIST_DIR}/dfs/managers/headers/sender.h
    ${CMAKE_CURRENT_LIST_DIR}/dfs/managers/sources/card_manager.cpp
    ${CMAKE_CURRENT_LIST_DIR}/dfs/managers/sources/dfs_networkmanager.cpp
    ${CMAKE_CURRENT_LIST_DIR}/dfs/managers/sources/sender.cpp
    ${CMAKE_CURRENT_LIST_DIR}/dfs/packages/headers/all.h
    ${CMAKE_CURRENT_LIST_DIR}/dfs/packages/headers/dfs_changes.h
    ${CMAKE_CURRENT_LIST_DIR}/dfs/packages/headers/dfs_message_interface.h
    ${CMAKE_CURRENT_LIST_DIR}/dfs/packages/headers/dfs_request.h
    ${CMAKE_CURRENT_LIST_DIR}/dfs/packages/headers/hash_operations.h
    ${CMAKE_CURRENT_LIST_DIR}/dfs/packages/headers/message_struct.h
    ${CMAKE_CURRENT_LIST_DIR}/dfs/packages/headers/req_frags_message.h
    ${CMAKE_CURRENT_LIST_DIR}/dfs/packages/headers/status.h
    ${CMAKE_CURRENT_LIST_DIR}/dfs/packages/headers/title_message.h
    ${CMAKE_CURRENT_LIST_DIR}/dfs/packages/sources/dfs_changes.cpp
    ${CMAKE_CURRENT_LIST_DIR}/dfs/packages/sources/dfs_request.cpp
    ${CMAKE_CURRENT_LIST_DIR}/dfs/packages/sources/hash_operations.cpp
    ${CMAKE_CURRENT_LIST_DIR}/dfs/packages/sources/message_struct.cpp
    ${CMAKE_CURRENT_LIST_DIR}/dfs/packages/sources/req_frags_message.cpp
    ${CMAKE_CURRENT_LIST_DIR}/dfs/packages/sources/status.cpp
    ${CMAKE_CURRENT_LIST_DIR}/dfs/packages/sources/title_message.cpp
    ${CMAKE_CURRENT_LIST_DIR}/dfs/types/headers/cardfile.h
    ${CMAKE_CURRENT_LIST_DIR}/dfs/types/headers/dfstruct.h
    ${CMAKE_CURRENT_LIST_DIR}/dfs/types/sources/cardfile.cpp
    ${CMAKE_CURRENT_LIST_DIR}/dfs/types/sources/dfstruct.cpp
    ${CMAKE_CURRENT_LIST_DIR}/headers/datastorage/actor.h
    ${CMAKE_CURRENT_LIST_DIR}/headers/datastorage/block.h
    ${CMAKE_CURRENT_LIST_DIR}/headers/datastorage/blockchain.h
    # ${CMAKE_CURRENT_LIST_DIR}/headers/datastorage/contract.h
    ${CMAKE_CURRENT_LIST_DIR}/headers/datastorage/genesis_block.h
    ${CMAKE_CURRENT_LIST_DIR}/headers/datastorage/index/actorindex.h
    ${CMAKE_CURRENT_LIST_DIR}/headers/datastorage/index/blockindex.h
    ${CMAKE_CURRENT_LIST_DIR}/headers/datastorage/index/memindex.h
    ${CMAKE_CURRENT_LIST_DIR}/headers/datastorage/transaction.h
    ${CMAKE_CURRENT_LIST_DIR}/headers/enc/enc_tools.h
    ${CMAKE_CURRENT_LIST_DIR}/headers/enc/key_private.h
    ${CMAKE_CURRENT_LIST_DIR}/headers/enc/key_public.h
    ${CMAKE_CURRENT_LIST_DIR}/headers/enc/sign_interface.h
    ${CMAKE_CURRENT_LIST_DIR}/headers/managers/account_controller.h
    ${CMAKE_CURRENT_LIST_DIR}/headers/managers/chat.h
    ${CMAKE_CURRENT_LIST_DIR}/headers/managers/chatmanager.h
    # ${CMAKE_CURRENT_LIST_DIR}/headers/managers/coin_service.h
    ${CMAKE_CURRENT_LIST_DIR}/headers/managers/contract_manager.h
    ${CMAKE_CURRENT_LIST_DIR}/headers/managers/file_updater_manager.h
    ${CMAKE_CURRENT_LIST_DIR}/headers/managers/logs_manager.h
    ${CMAKE_CURRENT_LIST_DIR}/headers/managers/extrachain_node.h
    ${CMAKE_CURRENT_LIST_DIR}/headers/managers/sm_manager.h
    ${CMAKE_CURRENT_LIST_DIR}/headers/managers/thread_pool.h
    ${CMAKE_CURRENT_LIST_DIR}/headers/managers/tx_manager.h
    ${CMAKE_CURRENT_LIST_DIR}/headers/metatypes.h
    ${CMAKE_CURRENT_LIST_DIR}/headers/extrachain_global.h
    ${CMAKE_CURRENT_LIST_DIR}/headers/network/discovery_service.h
    ${CMAKE_CURRENT_LIST_DIR}/headers/network/network_manager.h
    ${CMAKE_CURRENT_LIST_DIR}/headers/network/network_status.h
    ${CMAKE_CURRENT_LIST_DIR}/headers/network/packages/base_message.h
    ${CMAKE_CURRENT_LIST_DIR}/headers/network/packages/base_message_response.h
    ${CMAKE_CURRENT_LIST_DIR}/headers/network/packages/message_interface.h
    ${CMAKE_CURRENT_LIST_DIR}/headers/network/packages/service/all_messages.h
    ${CMAKE_CURRENT_LIST_DIR}/headers/network/packages/service/connections_message.h
    ${CMAKE_CURRENT_LIST_DIR}/headers/network/packages/service/get_actor_message.h
    ${CMAKE_CURRENT_LIST_DIR}/headers/network/packages/service/get_all_actor_message.h
    ${CMAKE_CURRENT_LIST_DIR}/headers/network/packages/service/get_block_message.h
    ${CMAKE_CURRENT_LIST_DIR}/headers/network/packages/service/get_count_messages.h
    ${CMAKE_CURRENT_LIST_DIR}/headers/network/packages/service/get_tx_message.h
    ${CMAKE_CURRENT_LIST_DIR}/headers/network/packages/service/get_tx_pair_message.h
    ${CMAKE_CURRENT_LIST_DIR}/headers/network/packages/service/message_types.h
    ${CMAKE_CURRENT_LIST_DIR}/headers/network/packages/service/ping_pong_message.h
    ${CMAKE_CURRENT_LIST_DIR}/headers/network/packages/service/verify_messages.h
    ${CMAKE_CURRENT_LIST_DIR}/headers/network/tcpserver_service.h
    ${CMAKE_CURRENT_LIST_DIR}/headers/network/socket_pair.h
    ${CMAKE_CURRENT_LIST_DIR}/headers/network/isocket_service.h
    ${CMAKE_CURRENT_LIST_DIR}/headers/network/tcpsocket_service.h
    ${CMAKE_CURRENT_LIST_DIR}/headers/network/websocket_service.h
    ${CMAKE_CURRENT_LIST_DIR}/headers/network/upnpconnection.h
    ${CMAKE_CURRENT_LIST_DIR}/headers/profile/private_profile.h
    ${CMAKE_CURRENT_LIST_DIR}/headers/profile/public_profile.h
    ${CMAKE_CURRENT_LIST_DIR}/headers/resolve/dfs_resolver_service.h
    ${CMAKE_CURRENT_LIST_DIR}/headers/resolve/resolve_manager.h
    ${CMAKE_CURRENT_LIST_DIR}/headers/resolve/resolver_service.h
    ${CMAKE_CURRENT_LIST_DIR}/headers/utils/Keccak256.h
    ${CMAKE_CURRENT_LIST_DIR}/headers/utils/bignumber.h
    ${CMAKE_CURRENT_LIST_DIR}/headers/utils/coinprocess.h
    ${CMAKE_CURRENT_LIST_DIR}/headers/utils/db_connector.h
    ${CMAKE_CURRENT_LIST_DIR}/headers/utils/exc_utils.h
    ${CMAKE_CURRENT_LIST_DIR}/headers/utils/variant_model.h
    ${CMAKE_CURRENT_LIST_DIR}/sources/datastorage/block.cpp
    ${CMAKE_CURRENT_LIST_DIR}/sources/datastorage/blockchain.cpp
    # ${CMAKE_CURRENT_LIST_DIR}/sources/datastorage/contract.cpp
    ${CMAKE_CURRENT_LIST_DIR}/sources/datastorage/genesis_block.cpp
    ${CMAKE_CURRENT_LIST_DIR}/sources/datastorage/index/actorindex.cpp
    ${CMAKE_CURRENT_LIST_DIR}/sources/datastorage/index/blockindex.cpp
    ${CMAKE_CURRENT_LIST_DIR}/sources/datastorage/index/memindex.cpp
    ${CMAKE_CURRENT_LIST_DIR}/sources/datastorage/transaction.cpp
    ${CMAKE_CURRENT_LIST_DIR}/sources/enc/enc_tools.cpp
    ${CMAKE_CURRENT_LIST_DIR}/sources/enc/key_private.cpp
    ${CMAKE_CURRENT_LIST_DIR}/sources/enc/key_public.cpp
    ${CMAKE_CURRENT_LIST_DIR}/sources/managers/account_controller.cpp
    ${CMAKE_CURRENT_LIST_DIR}/sources/managers/chat.cpp
    ${CMAKE_CURRENT_LIST_DIR}/sources/managers/chatmanager.cpp
    # ${CMAKE_CURRENT_LIST_DIR}/sources/managers/coin_service.cpp
    ${CMAKE_CURRENT_LIST_DIR}/sources/managers/contract_manager.cpp
    ${CMAKE_CURRENT_LIST_DIR}/sources/managers/file_updater_manager.cpp
    ${CMAKE_CURRENT_LIST_DIR}/sources/managers/logs_manager.cpp
    ${CMAKE_CURRENT_LIST_DIR}/sources/managers/extrachain_node.cpp
    ${CMAKE_CURRENT_LIST_DIR}/sources/managers/sm_manager.cpp
    ${CMAKE_CURRENT_LIST_DIR}/sources/managers/thread_pool.cpp
    ${CMAKE_CURRENT_LIST_DIR}/sources/managers/tx_manager.cpp
    ${CMAKE_CURRENT_LIST_DIR}/sources/network/discovery_service.cpp
    ${CMAKE_CURRENT_LIST_DIR}/sources/network/network_manager.cpp
    ${CMAKE_CURRENT_LIST_DIR}/sources/network/network_status.cpp
    ${CMAKE_CURRENT_LIST_DIR}/sources/network/packages/base_message.cpp
    ${CMAKE_CURRENT_LIST_DIR}/sources/network/packages/base_message_response.cpp
    ${CMAKE_CURRENT_LIST_DIR}/sources/network/packages/service/connections_message.cpp
    ${CMAKE_CURRENT_LIST_DIR}/sources/network/packages/service/get_actor_message.cpp
    ${CMAKE_CURRENT_LIST_DIR}/sources/network/packages/service/get_all_actor_message.cpp
    ${CMAKE_CURRENT_LIST_DIR}/sources/network/packages/service/get_block_message.cpp
    ${CMAKE_CURRENT_LIST_DIR}/sources/network/packages/service/get_tx_message.cpp
    ${CMAKE_CURRENT_LIST_DIR}/sources/network/packages/service/get_tx_pair_message.cpp
    ${CMAKE_CURRENT_LIST_DIR}/sources/network/tcpserver_service.cpp
    ${CMAKE_CURRENT_LIST_DIR}/sources/network/socket_pair.cpp
    ${CMAKE_CURRENT_LIST_DIR}/sources/network/isocket_service.cpp
    ${CMAKE_CURRENT_LIST_DIR}/sources/network/tcpsocket_service.cpp
    ${CMAKE_CURRENT_LIST_DIR}/sources/network/websocket_service.cpp
    ${CMAKE_CURRENT_LIST_DIR}/sources/network/upnpconnection.cpp
    ${CMAKE_CURRENT_LIST_DIR}/sources/profile/private_profile.cpp
    ${CMAKE_CURRENT_LIST_DIR}/sources/profile/public_profile.cpp
    ${CMAKE_CURRENT_LIST_DIR}/sources/resolve/dfs_resolver_service.cpp
    ${CMAKE_CURRENT_LIST_DIR}/sources/resolve/resolve_manager.cpp
    ${CMAKE_CURRENT_LIST_DIR}/sources/resolve/resolver_service.cpp
    ${CMAKE_CURRENT_LIST_DIR}/sources/utils/Keccak256.cpp
    ${CMAKE_CURRENT_LIST_DIR}/sources/utils/bignumber.cpp
    ${CMAKE_CURRENT_LIST_DIR}/sources/utils/coinprocess.cpp
    ${CMAKE_CURRENT_LIST_DIR}/sources/utils/db_connector.cpp
    ${CMAKE_CURRENT_LIST_DIR}/sources/utils/exc_utils.cpp
    ${CMAKE_CURRENT_LIST_DIR}/sources/utils/variant_model.cpp
    ${CMAKE_CURRENT_LIST_DIR}/rextrachain/rextrachain.h
    )


add_library(extrachainc
    ${CMAKE_CURRENT_LIST_DIR}/Cbindings/extrachainc.h ${CMAKE_CURRENT_LIST_DIR}/Cbindings/extrachainc.cpp
    )

if (EXTRACHAIN_STATIC_BUILD)
    add_library(extrachain ${EXTRACHAIN_CORE_SOURCES})
else()
    add_compile_definitions(extrachain PRIVATE EXTRACHAIN_LIBRARY)
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

# rust
if (EXTRACHAIN_ENABLE_RUST)
    add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../corrosion ${CMAKE_CURRENT_BINARY_DIR}/corrosion)
    corrosion_import_crate(MANIFEST_PATH ${CMAKE_CURRENT_LIST_DIR}/rextrachain/Cargo.toml)
    corrosion_link_libraries(rextrachain extrachainc)
    execute_process(COMMAND rustc --version OUTPUT_VARIABLE RUST_VERSION)
    string(STRIP "${RUST_VERSION}" RUST_VERSION)
    string(SUBSTRING "${RUST_VERSION}" 6 6 RUST_VERSION)
    add_compile_definitions(EXTRACHAIN_ENABLE_RUST EXTRACHAIN_RUST_VERSION="${RUST_VERSION}")
endif(EXTRACHAIN_ENABLE_RUST)

# git
execute_process(COMMAND git --git-dir ${CMAKE_CURRENT_LIST_DIR}/.git --work-tree $$PWD describe --always --tags OUTPUT_VARIABLE GIT_COMMIT_CORE)
execute_process(COMMAND git --git-dir ${CMAKE_CURRENT_LIST_DIR}/.git --work-tree $$PWD symbolic-ref --short HEAD OUTPUT_VARIABLE GIT_BRANCH_CORE)
string(STRIP "${GIT_COMMIT_CORE}" GIT_COMMIT_CORE)
string(STRIP "${GIT_BRANCH_CORE}" GIT_BRANCH_CORE)
string(TIMESTAMP COMPILE_DATE "%m.%d")
add_compile_definitions(EXTRACHAIN_CMAKE COMPILE_DATE="${COMPILE_DATE}" EXTRACHAIN_VERSION="${EXTRACHAIN_VERSION}" GIT_COMMIT_CORE="${GIT_COMMIT_CORE}" GIT_BRANCH_CORE="${GIT_BRANCH_CORE}")

target_precompile_headers(extrachain PRIVATE ${CMAKE_CURRENT_LIST_DIR}/headers/precompiled.h)
target_include_directories(extrachain PUBLIC ${EXTRACHAIN_CORE_INCLUDES})
target_include_directories(extrachainc PUBLIC ${EXTRACHAIN_CORE_INCLUDES})

target_link_libraries(extrachain Qt6::Core Qt6::Network Qt6::Concurrent Qt6::WebSockets
    unofficial-sodium::sodium unofficial::sqlite3::sqlite3 extrachainc)
target_link_libraries(extrachainc Qt6::Core Qt6::Network Qt6::Concurrent Qt6::WebSockets
    unofficial-sodium::sodium unofficial::sqlite3::sqlite3)
if (EXTRACHAIN_ENABLE_RUST)
    target_link_libraries(extrachain rextrachain extrachainc)
endif (EXTRACHAIN_ENABLE_RUST)

if (UNIX AND NOT APPLE)
    target_link_libraries(extrachain gmp)
endif (UNIX AND NOT APPLE)
if (APPLE AND NOT IOS) # temp
    target_include_directories(extrachain PUBLIC /usr/local/Cellar/gmp/6.2.1/include)
    target_link_libraries(extrachain /usr/local/Cellar/gmp/6.2.1/lib/libgmp.a /usr/local/Cellar/gmp/6.2.1/lib/libgmpxx.a)
endif(APPLE AND NOT IOS)
if (WIN32) # temp
    target_link_libraries(extrachain ${_VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/lib/mpir.lib)
endif(WIN32)

set_property(TARGET extrachain PROPERTY POSITION_INDEPENDENT_CODE 1)
