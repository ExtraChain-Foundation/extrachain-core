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
find_package(Boost REQUIRED COMPONENTS system thread)
find_package(fmt CONFIG REQUIRED)
find_package(magic_enum CONFIG REQUIRED)

add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../extrachain-3rdparty/msgpack ${CMAKE_CURRENT_BINARY_DIR}/msgpack)

set(EXTRACHAIN_CORE_INCLUDES
    ${CMAKE_CURRENT_LIST_DIR}
    ${CMAKE_CURRENT_LIST_DIR}/headers
    )

set(EXTRACHAIN_CORE_SOURCES

    ${CMAKE_CURRENT_LIST_DIR}/headers/datastorage/dfs/dfs_controller.h
    ${CMAKE_CURRENT_LIST_DIR}/headers/datastorage/dfs/fragment_storage.h
    ${CMAKE_CURRENT_LIST_DIR}/headers/datastorage/dfs/historical_chain.h
    ${CMAKE_CURRENT_LIST_DIR}/headers/datastorage/actor.h
    ${CMAKE_CURRENT_LIST_DIR}/headers/datastorage/block.h
    ${CMAKE_CURRENT_LIST_DIR}/headers/datastorage/blockchain.h
    ${CMAKE_CURRENT_LIST_DIR}/headers/datastorage/genesis_block.h
    ${CMAKE_CURRENT_LIST_DIR}/headers/datastorage/index/actorindex.h
    ${CMAKE_CURRENT_LIST_DIR}/headers/datastorage/index/blockindex.h
    ${CMAKE_CURRENT_LIST_DIR}/headers/datastorage/index/memindex.h
    ${CMAKE_CURRENT_LIST_DIR}/headers/datastorage/transaction.h
    ${CMAKE_CURRENT_LIST_DIR}/headers/datastorage/dfs/permission_manager.h
    ${CMAKE_CURRENT_LIST_DIR}/headers/datastorage/threds/inserter_files.h
    ${CMAKE_CURRENT_LIST_DIR}/headers/datastorage/private_profile.h
    ${CMAKE_CURRENT_LIST_DIR}/headers/enc/enc_tools.h
    ${CMAKE_CURRENT_LIST_DIR}/headers/enc/key_private.h
    ${CMAKE_CURRENT_LIST_DIR}/headers/enc/key_public.h
    ${CMAKE_CURRENT_LIST_DIR}/headers/managers/account_controller.h
    ${CMAKE_CURRENT_LIST_DIR}/headers/managers/logs_manager.h
    ${CMAKE_CURRENT_LIST_DIR}/headers/managers/extrachain_node.h
    ${CMAKE_CURRENT_LIST_DIR}/headers/managers/thread_pool.h
    ${CMAKE_CURRENT_LIST_DIR}/headers/managers/tx_manager.h
    ${CMAKE_CURRENT_LIST_DIR}/headers/metatypes.h
    ${CMAKE_CURRENT_LIST_DIR}/headers/extrachain_global.h
    ${CMAKE_CURRENT_LIST_DIR}/headers/network/discovery_service.h
    ${CMAKE_CURRENT_LIST_DIR}/headers/network/network_manager.h
    ${CMAKE_CURRENT_LIST_DIR}/headers/network/network_status.h
    ${CMAKE_CURRENT_LIST_DIR}/headers/network/message_body.h
    ${CMAKE_CURRENT_LIST_DIR}/headers/network/isocket_service.h
    ${CMAKE_CURRENT_LIST_DIR}/headers/network/websocket_service.h
    ${CMAKE_CURRENT_LIST_DIR}/headers/network/upnpconnection.h
    ${CMAKE_CURRENT_LIST_DIR}/headers/utils/autologinhash.h
    ${CMAKE_CURRENT_LIST_DIR}/headers/utils/bignumber.h
    ${CMAKE_CURRENT_LIST_DIR}/headers/utils/coinprocess.h
    ${CMAKE_CURRENT_LIST_DIR}/headers/utils/db_connector.h
    ${CMAKE_CURRENT_LIST_DIR}/headers/utils/exc_utils.h
    ${CMAKE_CURRENT_LIST_DIR}/headers/utils/dfs_utils.h
    ${CMAKE_CURRENT_LIST_DIR}/headers/utils/variant_model.h
    ${CMAKE_CURRENT_LIST_DIR}/sources/datastorage/dfs/dfs_controller.cpp
    ${CMAKE_CURRENT_LIST_DIR}/sources/datastorage/dfs/fragment_storage.cpp
    ${CMAKE_CURRENT_LIST_DIR}/sources/datastorage/dfs/historical_chain.cpp
    ${CMAKE_CURRENT_LIST_DIR}/sources/datastorage/block.cpp
    ${CMAKE_CURRENT_LIST_DIR}/sources/datastorage/blockchain.cpp
    ${CMAKE_CURRENT_LIST_DIR}/sources/datastorage/genesis_block.cpp
    ${CMAKE_CURRENT_LIST_DIR}/sources/datastorage/index/actorindex.cpp
    ${CMAKE_CURRENT_LIST_DIR}/sources/datastorage/index/blockindex.cpp
    ${CMAKE_CURRENT_LIST_DIR}/sources/datastorage/index/memindex.cpp
    ${CMAKE_CURRENT_LIST_DIR}/sources/datastorage/transaction.cpp
    ${CMAKE_CURRENT_LIST_DIR}/sources/datastorage/dfs/permission_manager.cpp
    ${CMAKE_CURRENT_LIST_DIR}/sources/datastorage/threds/inserter_files.cpp
    ${CMAKE_CURRENT_LIST_DIR}/sources/datastorage/private_profile.cpp
    ${CMAKE_CURRENT_LIST_DIR}/sources/enc/enc_tools.cpp
    ${CMAKE_CURRENT_LIST_DIR}/sources/enc/key_private.cpp
    ${CMAKE_CURRENT_LIST_DIR}/sources/enc/key_public.cpp
    ${CMAKE_CURRENT_LIST_DIR}/sources/managers/account_controller.cpp
    ${CMAKE_CURRENT_LIST_DIR}/sources/managers/logs_manager.cpp
    ${CMAKE_CURRENT_LIST_DIR}/sources/managers/extrachain_node.cpp
    ${CMAKE_CURRENT_LIST_DIR}/sources/managers/thread_pool.cpp
    ${CMAKE_CURRENT_LIST_DIR}/sources/managers/tx_manager.cpp
    ${CMAKE_CURRENT_LIST_DIR}/sources/network/discovery_service.cpp
    ${CMAKE_CURRENT_LIST_DIR}/sources/network/network_manager.cpp
    ${CMAKE_CURRENT_LIST_DIR}/sources/network/network_status.cpp
    ${CMAKE_CURRENT_LIST_DIR}/sources/network/isocket_service.cpp
    ${CMAKE_CURRENT_LIST_DIR}/sources/network/websocket_service.cpp
    ${CMAKE_CURRENT_LIST_DIR}/sources/network/upnpconnection.cpp
    ${CMAKE_CURRENT_LIST_DIR}/sources/utils/autologinhash.cpp
    ${CMAKE_CURRENT_LIST_DIR}/sources/utils/bignumber.cpp
    ${CMAKE_CURRENT_LIST_DIR}/sources/utils/coinprocess.cpp
    ${CMAKE_CURRENT_LIST_DIR}/sources/utils/db_connector.cpp
    ${CMAKE_CURRENT_LIST_DIR}/sources/utils/exc_utils.cpp
    ${CMAKE_CURRENT_LIST_DIR}/sources/utils/dfs_utils.cpp
    ${CMAKE_CURRENT_LIST_DIR}/sources/utils/variant_model.cpp
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
add_compile_definitions(EXTRACHAIN_CMAKE COMPILE_DATE="${COMPILE_DATE}" EXTRACHAIN_VERSION="${EXTRACHAIN_VERSION}" GIT_COMMIT_CORE="${GIT_COMMIT_CORE}" GIT_BRANCH_CORE="${GIT_BRANCH_CORE}")

target_precompile_headers(extrachain PRIVATE ${CMAKE_CURRENT_LIST_DIR}/headers/precompiled.h)
target_include_directories(extrachain PUBLIC ${EXTRACHAIN_CORE_INCLUDES})
# target_include_directories(extrachainc PUBLIC ${EXTRACHAIN_CORE_INCLUDES})

target_link_libraries(extrachain Qt6::Core Qt6::Network Qt6::Concurrent Qt6::WebSockets
    unofficial-sodium::sodium unofficial::sqlite3::sqlite3 msgpackc-cxx fmt::fmt-header-only
    magic_enum::magic_enum Boost::boost Boost::system Boost::thread) # extrachainc
# target_link_libraries(extrachainc Qt6::Core Qt6::Network Qt6::Concurrent Qt6::WebSockets
#     unofficial-sodium::sodium unofficial::sqlite3::sqlite3 msgpackc-cxx Boost::boost Boost::system Boost::thread)

set_property(TARGET extrachain PROPERTY POSITION_INDEPENDENT_CODE 1)

set(CMAKE_INCLUDE_CURRENT_DIR ON)
