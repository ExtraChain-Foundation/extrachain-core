cmake_minimum_required(VERSION 3.16)

# project(extrachain LANGUAGES CXX VERSION 0.8.5 DESCRIPTION "ExtraChain Core")

set(CMAKE_INCLUDE_CURRENT_DIR ON)

set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(QT NAMES Qt6 Qt5 COMPONENTS Core Network Concurrent REQUIRED)
find_package(Qt${QT_VERSION_MAJOR} COMPONENTS Core Network Concurrent REQUIRED)
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
    ${CMAKE_CURRENT_LIST_DIR}/dfs/managers/headers/dfsnetmanager.h
    ${CMAKE_CURRENT_LIST_DIR}/dfs/managers/headers/sender.h
    ${CMAKE_CURRENT_LIST_DIR}/dfs/managers/sources/card_manager.cpp
    ${CMAKE_CURRENT_LIST_DIR}/dfs/managers/sources/dfsnetmanager.cpp
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
    ${CMAKE_CURRENT_LIST_DIR}/headers/datastorage/searchfilters.h
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
    ${CMAKE_CURRENT_LIST_DIR}/headers/managers/notification_manager.h
    ${CMAKE_CURRENT_LIST_DIR}/headers/managers/sm_manager.h
    ${CMAKE_CURRENT_LIST_DIR}/headers/managers/thread_pool.h
    ${CMAKE_CURRENT_LIST_DIR}/headers/managers/tx_manager.h
    ${CMAKE_CURRENT_LIST_DIR}/headers/metatypes.h
    ${CMAKE_CURRENT_LIST_DIR}/headers/network/discovery_service.h
    ${CMAKE_CURRENT_LIST_DIR}/headers/network/network_manager.h
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
    ${CMAKE_CURRENT_LIST_DIR}/headers/network/server_service.h
    ${CMAKE_CURRENT_LIST_DIR}/headers/network/socket_pair.h
    ${CMAKE_CURRENT_LIST_DIR}/headers/network/socket_service.h
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
    ${CMAKE_CURRENT_LIST_DIR}/sources/managers/notification_manager.cpp
    ${CMAKE_CURRENT_LIST_DIR}/sources/managers/sm_manager.cpp
    ${CMAKE_CURRENT_LIST_DIR}/sources/managers/thread_pool.cpp
    ${CMAKE_CURRENT_LIST_DIR}/sources/managers/tx_manager.cpp
    ${CMAKE_CURRENT_LIST_DIR}/sources/network/discovery_service.cpp
    ${CMAKE_CURRENT_LIST_DIR}/sources/network/network_manager.cpp
    ${CMAKE_CURRENT_LIST_DIR}/sources/network/packages/base_message.cpp
    ${CMAKE_CURRENT_LIST_DIR}/sources/network/packages/base_message_response.cpp
    ${CMAKE_CURRENT_LIST_DIR}/sources/network/packages/service/connections_message.cpp
    ${CMAKE_CURRENT_LIST_DIR}/sources/network/packages/service/get_actor_message.cpp
    ${CMAKE_CURRENT_LIST_DIR}/sources/network/packages/service/get_all_actor_message.cpp
    ${CMAKE_CURRENT_LIST_DIR}/sources/network/packages/service/get_block_message.cpp
    ${CMAKE_CURRENT_LIST_DIR}/sources/network/packages/service/get_tx_message.cpp
    ${CMAKE_CURRENT_LIST_DIR}/sources/network/packages/service/get_tx_pair_message.cpp
    ${CMAKE_CURRENT_LIST_DIR}/sources/network/server_service.cpp
    ${CMAKE_CURRENT_LIST_DIR}/sources/network/socket_pair.cpp
    ${CMAKE_CURRENT_LIST_DIR}/sources/network/socket_service.cpp
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
    )

if (EXTRACHAIN_STATIC_BUILD)
    add_library(extrachain
        ${EXTRACHAIN_CORE_SOURCES}
        )
else()
    add_library(extrachain SHARED
        ${EXTRACHAIN_CORE_SOURCES}
        )
endif()

target_include_directories(extrachain PUBLIC ${EXTRACHAIN_CORE_INCLUDES} ../etalonium-console/headers)
target_link_libraries(extrachain Qt${QT_VERSION_MAJOR}::Core Qt${QT_VERSION_MAJOR}::Network Qt${QT_VERSION_MAJOR}::Concurrent
    unofficial-sodium::sodium unofficial::sqlite3::sqlite3)
if (UNIX AND NOT APPLE)
    target_link_libraries(extrachain gmp)
endif (UNIX AND NOT APPLE)
set_property(TARGET extrachain PROPERTY POSITION_INDEPENDENT_CODE 1)
message("--- ExtraChain Core ---")
