QT += concurrent
VERSION = 0.7.28
INCLUDEPATH += $$PWD/headers
INCLUDEPATH += $$PWD

SOURCES += \
    $$PWD/dfs/controls/sources/subscribe_controller.cpp \
    $$PWD/dfs/managers/sources/dfsnetmanager.cpp \
    $$PWD/dfs/managers/sources/sender.cpp \
    $$PWD/dfs/managers/sources/card_manager.cpp \
    $$PWD/dfs/packages/sources/hash_operations.cpp \
    $$PWD/dfs/packages/sources/status.cpp \
    $$PWD/dfs/packages/sources/dfs_request.cpp \
    $$PWD/dfs/packages/sources/dfs_changes.cpp \
    $$PWD/dfs/packages/sources/message_struct.cpp \
    $$PWD/dfs/packages/sources/req_frags_message.cpp \
    $$PWD/dfs/packages/sources/title_message.cpp \
    $$PWD/dfs/types/sources/cardfile.cpp \
    $$PWD/sources/managers/chat.cpp \
    $$PWD/sources/managers/chatmanager.cpp \
    $$PWD/sources/enc/algorithms/ecc/eccmath.cpp \
    $$PWD/sources/enc/algorithms/ecc/ellipticpoint.cpp \
    $$PWD/sources/enc/algorithms/blowfish_crypt.cpp \
    $$PWD/sources/enc/key_private.cpp \
    $$PWD/sources/enc/key_public.cpp \
    $$PWD/sources/datastorage/index/actorindex.cpp \
    $$PWD/sources/datastorage/index/blockindex.cpp \
    $$PWD/sources/datastorage/index/memindex.cpp \
    $$PWD/sources/datastorage/block.cpp \
    $$PWD/sources/datastorage/blockchain.cpp \
    $$PWD/sources/datastorage/contract.cpp \
    $$PWD/sources/datastorage/genesis_block.cpp \
    $$PWD/sources/datastorage/transaction.cpp \
    $$PWD/sources/managers/file_updater_manager.cpp \
    $$PWD/sources/managers/notification_manager.cpp \
    $$PWD/sources/network/packages/service/connections_message.cpp \
    $$PWD/sources/profile/profile.cpp \
    $$PWD/sources/managers/account_controller.cpp \
    $$PWD/sources/managers/sm_manager.cpp \
    $$PWD/sources/managers/coin_service.cpp \
    $$PWD/sources/managers/contract_manager.cpp \
    $$PWD/sources/managers/tx_manager.cpp \
    $$PWD/sources/managers/thread_pool.cpp \
    $$PWD/sources/managers/logs_manager.cpp \
    $$PWD/sources/network/packages/base_message_response.cpp \
    $$PWD/sources/network/packages/service/get_all_actor_message.cpp \
    $$PWD/sources/network/socket_pair.cpp \
    $$PWD/sources/profile/private_profile.cpp \
    $$PWD/sources/profile/public_profile.cpp \
    $$PWD/sources/resolve/dfs_resolver_service.cpp \
    $$PWD/sources/resolve/resolve_manager.cpp \
    $$PWD/sources/resolve/resolver_service.cpp \
    $$PWD/sources/utils/bignumber.cpp \
    $$PWD/sources/utils/coinprocess.cpp \
    $$PWD/sources/utils/db_connector.cpp \
    $$PWD/sources/utils/utils.cpp \
    $$PWD/sources/utils/Keccak256.cpp \
    $$PWD/dfs/controls/sources/dfs.cpp \
    $$PWD/dfs/types/sources/dfstruct.cpp \
    $$PWD/sources/managers/node_manager.cpp \
    $$PWD/sources/network/discovery_service.cpp \
    $$PWD/sources/network/network_manager.cpp \
    $$PWD/sources/network/packages/base_message.cpp \
    $$PWD/sources/network/packages/service/get_actor_message.cpp \
    $$PWD/sources/network/packages/service/get_block_message.cpp \
    $$PWD/sources/network/packages/service/get_tx_message.cpp \
    $$PWD/sources/network/packages/service/get_tx_pair_message.cpp \
    $$PWD/sources/network/server_service.cpp \
    $$PWD/sources/network/socket_service.cpp \
    $$PWD/sources/network/upnpconnection.cpp

HEADERS += \
    $$PWD/dfs/controls/headers/subscribe_controller.h \
    $$PWD/dfs/managers/headers/dfsnetmanager.h \
    $$PWD/dfs/managers/headers/sender.h \
    $$PWD/dfs/packages/headers/all.h \
    $$PWD/dfs/packages/headers/dfs_message_interface.h \
    $$PWD/dfs/packages/headers/hash_operations.h \
    $$PWD/dfs/packages/headers/message_struct.h \
    $$PWD/dfs/packages/headers/req_frags_message.h \
    $$PWD/dfs/packages/headers/status.h \
    $$PWD/dfs/packages/headers/title_message.h \
    $$PWD/headers/enc/algorithms/aes.h \
    $$PWD/headers/enc/algorithms/blowfish_crypt.h \
    $$PWD/headers/enc/algorithms/ecc/curves.h \
    $$PWD/headers/enc/algorithms/ecc/eccmath.h \
    $$PWD/headers/enc/algorithms/ecc/ellipticpoint.h \
    $$PWD/headers/enc/key_private.h \
    $$PWD/headers/enc/key_public.h \
    $$PWD/headers/enc/sign_interface.h \
    $$PWD/headers/datastorage/searchfilters.h \
    $$PWD/dfs/packages/headers/dfs_changes.h \
    $$PWD/dfs/types/headers/cardfile.h \
    $$PWD/headers/managers/chat.h \
    $$PWD/headers/managers/chatmanager.h \
    $$PWD/headers/managers/file_updater_manager.h \
    $$PWD/headers/managers/notification_manager.h \
    $$PWD/headers/metatypes.h \
    $$PWD/dfs/packages/headers/dfs_request.h \
    $$PWD/headers/datastorage/index/actorindex.h \
    $$PWD/headers/datastorage/index/blockindex.h \
    $$PWD/headers/datastorage/index/memindex.h \
    $$PWD/headers/datastorage/actor.h \
    $$PWD/headers/datastorage/block.h \
    $$PWD/headers/datastorage/blockchain.h \
    $$PWD/headers/datastorage/contract.h \
    $$PWD/headers/datastorage/genesis_block.h \
    $$PWD/headers/datastorage/transaction.h \
    $$PWD/headers/network/packages/service/connections_message.h \
    $$PWD/headers/network/packages/service/message_types.h \
    $$PWD/headers/profile/profile.h \
    $$PWD/headers/profile/quickprofile.h \
    $$PWD/headers/managers/account_controller.h \
    $$PWD/headers/managers/sm_manager.h \
    $$PWD/headers/managers/coin_service.h \
    $$PWD/headers/managers/contract_manager.h \
    $$PWD/headers/managers/tx_manager.h \
    $$PWD/headers/managers/thread_pool.h \
    $$PWD/headers/managers/logs_manager.h \
    $$PWD/headers/network/packages/base_message_response.h \
    $$PWD/headers/network/packages/service/get_all_actor_message.h \
    $$PWD/headers/network/socket_pair.h \
    $$PWD/headers/profile/private_profile.h \
    $$PWD/headers/profile/public_profile.h \
    $$PWD/headers/resolve/dfs_resolver_service.h \
    $$PWD/headers/resolve/resolve_manager.h \
    $$PWD/headers/resolve/resolver_service.h \
    $$PWD/headers/utils/Keccak256.h \
    $$PWD/headers/utils/bignumber.h \
    $$PWD/headers/utils/coinprocess.h \
    $$PWD/headers/utils/db_connector.h \
    $$PWD/dfs/controls/headers/dfs.h \
    $$PWD/dfs/managers/headers/card_manager.h \
    $$PWD/dfs/types/headers/dfstruct.h \
    $$PWD/headers/managers/node_manager.h \
    $$PWD/headers/network/discovery_service.h \
    $$PWD/headers/network/network_manager.h \
    $$PWD/headers/network/packages/base_message.h \
    $$PWD/headers/network/packages/message_interface.h \
    $$PWD/headers/network/packages/service/all_messages.h \
    $$PWD/headers/network/packages/service/get_actor_message.h \
    $$PWD/headers/network/packages/service/get_block_message.h \
    $$PWD/headers/network/packages/service/get_count_messages.h \
    $$PWD/headers/network/packages/service/get_tx_message.h \
    $$PWD/headers/network/packages/service/get_tx_pair_message.h \
    $$PWD/headers/network/packages/service/ping_pong_message.h \
    $$PWD/headers/network/packages/service/verify_messages.h \
    $$PWD/headers/network/server_service.h \
    $$PWD/headers/network/socket_service.h \
    $$PWD/headers/network/upnpconnection.h \
    $$PWD/headers/utils/utils.h \
    $$PWD/test.h

linux: QMAKE_CXXFLAGS += -Wall -Werror=return-type -Werror=implicit-fallthrough -Wno-unused-function # -Wno-unused-value -Wno-unused-parameter -Wno-unused-variable

!android {
!android!ios: DESTDIR = Build
android: DESTDIR = android-build
OBJECTS_DIR = .obj
MOC_DIR = .moc
RCC_DIR = .qrc
UI_DIR = .ui
}

QMAKE_SPEC_T = $$[QMAKE_SPEC]
contains(QMAKE_SPEC_T,.*win32.*) {
    COMPILE_DATE=$$system(date /t)
} else {
    COMPILE_DATE=$$system(date +%m.%d)
}

GIT_COMMIT_CORE = $$system(git --git-dir .git --work-tree $$PWD describe --always --tags)
GIT_BRANCH_CORE = $$system(git --git-dir .git --work-tree $$PWD symbolic-ref --short HEAD)
QMAKE_SUBSTITUTES += preconfig.h.in

include(../extrachain-3rdparty/extrachain-3rdparty.pri)

lessThan(QT_MAJOR_VERSION, 5): error("requires Qt 5.14+")
lessThan(QT_MINOR_VERSION, 14): error("requires Qt 5.14+")
# lessThan(QT_PATCH_VERSION, 0): error("requires Qt 5.14+")
