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

# Load generator: spins up a real node and floods it with reward transactions to
# fill the DAG with many sections (hot -> packs). Usage: extrachain-gen-sections [N]
add_executable(extrachain-gen-sections
    gen_sections.cpp
    )

target_link_libraries(extrachain-gen-sections
    PRIVATE Qt${QT_VERSION_MAJOR}::Core extrachain)
set_property(TARGET extrachain-gen-sections PROPERTY POSITION_INDEPENDENT_CODE 1)

# Replays the real pack-sync transfer (read_raw -> install_raw) from a generated
# data dir into a fresh store and verifies every packed section round-trips.
# Usage: extrachain-sync-check <server-data-dir> [client-dir]
add_executable(extrachain-sync-check
    sync_check.cpp
    )

target_link_libraries(extrachain-sync-check
    PRIVATE Qt${QT_VERSION_MAJOR}::Core extrachain)
set_property(TARGET extrachain-sync-check PROPERTY POSITION_INDEPENDENT_CODE 1)

# Minimal real node runner for live multi-process sync tests.
#   extrachain-node-run serve <home>
#   extrachain-node-run join  <home> <peer-ip> <target-section>
add_executable(extrachain-node-run
    node_run.cpp
    )

target_link_libraries(extrachain-node-run
    PRIVATE Qt${QT_VERSION_MAJOR}::Core extrachain)
set_property(TARGET extrachain-node-run PROPERTY POSITION_INDEPENDENT_CODE 1)

add_executable(extrachain-control-bench control_bench.cpp)
target_link_libraries(extrachain-control-bench PRIVATE Qt${QT_VERSION_MAJOR}::Core extrachain)
set_property(TARGET extrachain-control-bench PROPERTY POSITION_INDEPENDENT_CODE 1)

add_executable(extrachain-ci-unit ci_unit.cpp)
target_link_libraries(extrachain-ci-unit PRIVATE Qt${QT_VERSION_MAJOR}::Core extrachain)
set_property(TARGET extrachain-ci-unit PROPERTY POSITION_INDEPENDENT_CODE 1)

add_executable(extrachain-dag-audit dag_audit.cpp)
target_link_libraries(extrachain-dag-audit PRIVATE Qt${QT_VERSION_MAJOR}::Core extrachain)
set_property(TARGET extrachain-dag-audit PROPERTY POSITION_INDEPENDENT_CODE 1)

add_executable(extrachain-chain-compare chain_compare.cpp)
target_link_libraries(extrachain-chain-compare PRIVATE Qt${QT_VERSION_MAJOR}::Core extrachain)
set_property(TARGET extrachain-chain-compare PROPERTY POSITION_INDEPENDENT_CODE 1)

add_executable(extrachain-amount-rt amount_roundtrip.cpp)
target_link_libraries(extrachain-amount-rt PRIVATE Qt${QT_VERSION_MAJOR}::Core extrachain)
set_property(TARGET extrachain-amount-rt PROPERTY POSITION_INDEPENDENT_CODE 1)
