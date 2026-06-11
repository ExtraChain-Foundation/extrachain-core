cmake_minimum_required(VERSION 3.21)

# Version string is sourced from extrachain_version.h; the old plain-text
# `extrachain_version` file no longer exists, so don't hard-depend on it.
if(EXISTS "${CMAKE_CURRENT_LIST_DIR}/../extrachain_version")
    file(STRINGS "${CMAKE_CURRENT_LIST_DIR}/../extrachain_version" EXTRACHAIN_VERSION)
else()
    set(EXTRACHAIN_VERSION "0.25")
endif()
project(extrachain-tests LANGUAGES C CXX VERSION ${EXTRACHAIN_VERSION})

find_package(QT NAMES Qt6 Qt5 COMPONENTS Test REQUIRED)
find_package(Qt${QT_VERSION_MAJOR} COMPONENTS Test REQUIRED)

set(EXTRACHAIN_STATIC_BUILD true)
include(../CMakeLists.txt)

add_executable(extrachain-tests
    test.cpp
    )

target_link_libraries(extrachain-tests
    PRIVATE Qt${QT_VERSION_MAJOR}::Core Qt${QT_VERSION_MAJOR}::Test extrachain)
set_property(TARGET extrachain-tests PROPERTY POSITION_INDEPENDENT_CODE 1)

# Self-contained synchronization tests (pack-sync data path, wire-format
# legacy/canonical round-trip, hex->decimal migration round-trip). Kept in a
# separate target so it builds and runs independently of test.cpp.
add_executable(extrachain-sync-tests
    sync_test.cpp
    )

target_link_libraries(extrachain-sync-tests
    PRIVATE Qt${QT_VERSION_MAJOR}::Core Qt${QT_VERSION_MAJOR}::Test extrachain)
set_property(TARGET extrachain-sync-tests PROPERTY POSITION_INDEPENDENT_CODE 1)
