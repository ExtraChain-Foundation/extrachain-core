QT += concurrent websockets
VERSION = "$$cat($$PWD/extrachain_version)"
win32: CONFIG += precompile_header
win32: PRECOMPILED_HEADER = $$PWD/headers/precompiled.h
INCLUDEPATH += $$PWD/headers
INCLUDEPATH += $$PWD

SOURCES += \
    $$PWD/sources/datastorage/dfs/dfs_controller.cpp \
    $$PWD/sources/datastorage/block.cpp \
    $$PWD/sources/datastorage/blockchain.cpp \
    $$PWD/sources/datastorage/genesis_block.cpp \
    $$PWD/sources/datastorage/index/actorindex.cpp \
    $$PWD/sources/datastorage/index/blockindex.cpp \
    $$PWD/sources/datastorage/index/memindex.cpp \
    $$PWD/sources/datastorage/transaction.cpp \
    $$PWD/sources/datastorage/dfs/permission_manager.cpp \
    $$PWD/sources/datastorage/private_profile.cpp \
    $$PWD/sources/enc/enc_tools.cpp \
    $$PWD/sources/enc/key_private.cpp \
    $$PWD/sources/enc/key_public.cpp \
    $$PWD/sources/managers/account_controller.cpp \
    $$PWD/sources/managers/logs_manager.cpp \
    $$PWD/sources/managers/extrachain_node.cpp \
    $$PWD/sources/managers/thread_pool.cpp \
    $$PWD/sources/managers/tx_manager.cpp \
    $$PWD/sources/network/discovery_service.cpp \
    $$PWD/sources/network/network_manager.cpp \
    $$PWD/sources/network/network_status.cpp \
    $$PWD/sources/network/isocket_service.cpp \
    $$PWD/sources/network/websocket_service.cpp \
    $$PWD/sources/network/upnpconnection.cpp \
    $$PWD/sources/utils/autologinhash.cpp \
    $$PWD/sources/utils/bignumber.cpp \
    $$PWD/sources/utils/coinprocess.cpp \
    $$PWD/sources/utils/db_connector.cpp \
    $$PWD/sources/utils/exc_utils.cpp \
    $$PWD/sources/utils/dfs_utils.cpp \
    $$PWD/sources/utils/variant_model.cpp

HEADERS += \
    $$PWD/headers/datastorage/dfs/dfs_controller.h \
    $$PWD/headers/datastorage/actor.h \
    $$PWD/headers/datastorage/block.h \
    $$PWD/headers/datastorage/blockchain.h \
    $$PWD/headers/datastorage/genesis_block.h \
    $$PWD/headers/datastorage/index/actorindex.h \
    $$PWD/headers/datastorage/index/blockindex.h \
    $$PWD/headers/datastorage/index/memindex.h \
    $$PWD/headers/datastorage/transaction.h \
    $$PWD/headers/datastorage/dfs/permission_manager.h \
    $$PWD/headers/datastorage/private_profile.h \
    $$PWD/headers/enc/enc_tools.h \
    $$PWD/headers/enc/key_private.h \
    $$PWD/headers/enc/key_public.h \
    $$PWD/headers/managers/account_controller.h \
    $$PWD/headers/managers/logs_manager.h \
    $$PWD/headers/managers/extrachain_node.h \
    $$PWD/headers/managers/thread_pool.h \
    $$PWD/headers/managers/tx_manager.h \
    $$PWD/headers/metatypes.h \
    $$PWD/headers/extrachain_global.h \
    $$PWD/headers/network/discovery_service.h \
    $$PWD/headers/network/network_manager.h \
    $$PWD/headers/network/network_status.h \
    $$PWD/headers/network/message_body.h \
    $$PWD/headers/network/isocket_service.h \
    $$PWD/headers/network/websocket_service.h \
    $$PWD/headers/network/upnpconnection.h \
    $$PWD/headers/utils/autologinhash.h \
    $$PWD/headers/utils/bignumber.h \
    $$PWD/headers/utils/coinprocess.h \
    $$PWD/headers/utils/db_connector.h \
    $$PWD/headers/utils/exc_utils.h \
    $$PWD/headers/utils/dfs_utils.h \
    $$PWD/headers/utils/variant_model.h

gcc || clang: QMAKE_CXXFLAGS += -Werror=return-type -Werror=implicit-fallthrough -Wno-unused-function  -Wno-deprecated # -Wno-unused-value -Wno-unused-parameter -Wno-unused-variable

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

lessThan(QT_MAJOR_VERSION, 6): error("requires Qt 6.2+")
equals(QT_MAJOR_VERSION, 6): lessThan(QT_MINOR_VERSION, 2): error("requires Qt 6.2+")
equals(QT_MAJOR_VERSION, 6): equals(QT_MINOR_VERSION, 2): lessThan(QT_PATCH_VERSION, 2): error("requires Qt 6.2.2+")
