cmake_minimum_required(VERSION 3.21)

# Tests use the same deterministic dependency manifest as the library root.
set(VCPKG_MANIFEST_DIR "${CMAKE_CURRENT_LIST_DIR}/.." CACHE PATH "ExtraChain vcpkg manifest directory")

file(STRINGS "../extrachain_version.h" EXTRACHAIN_VERSION_LINE REGEX "extrachain_version =")
string(REGEX MATCH "[0-9]+\\.[0-9]+\\.[0-9]+" EXTRACHAIN_VERSION "${EXTRACHAIN_VERSION_LINE}")
project(extrachain-tests LANGUAGES C CXX VERSION ${EXTRACHAIN_VERSION})

find_package(QT NAMES Qt6 Qt5 COMPONENTS Test REQUIRED)
find_package(Qt${QT_VERSION_MAJOR} COMPONENTS Test REQUIRED)

set(EXTRACHAIN_STATIC_BUILD true)
include(../CMakeLists.txt)

option(EXTRACHAIN_BUILD_LEGACY_TESTS "Build the obsolete monolithic test target" OFF)
if(EXTRACHAIN_BUILD_LEGACY_TESTS)
    add_executable(extrachain-tests test.cpp)
    target_link_libraries(extrachain-tests
        PRIVATE Qt${QT_VERSION_MAJOR}::Core Qt${QT_VERSION_MAJOR}::Test extrachain)
    set_property(TARGET extrachain-tests PROPERTY POSITION_INDEPENDENT_CODE 1)
endif()

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

add_executable(extrachain-native-balance native_balance.cpp)
target_link_libraries(extrachain-native-balance PRIVATE Qt${QT_VERSION_MAJOR}::Core extrachain)
set_property(TARGET extrachain-native-balance PROPERTY POSITION_INDEPENDENT_CODE 1)

add_executable(extrachain-runtime-tests runtime/runtime_test.cpp)
target_link_libraries(extrachain-runtime-tests PRIVATE extrachain-runtime)
set_property(TARGET extrachain-runtime-tests PROPERTY POSITION_INDEPENDENT_CODE 1)

if(EXTRACHAIN_BUILD_QT_COMPAT)
    add_executable(extrachain-qt-compat-tests qt_compat_test.cpp)
    target_link_libraries(extrachain-qt-compat-tests PRIVATE extrachain-qt-compat extrachain)
    set_property(TARGET extrachain-qt-compat-tests PROPERTY POSITION_INDEPENDENT_CODE 1)
endif()

enable_testing()
add_test(NAME core-unit COMMAND extrachain-ci-unit)
add_test(NAME amount-roundtrip COMMAND extrachain-amount-rt)
add_test(NAME sync-roundtrip COMMAND extrachain-sync-tests)
add_test(NAME native-balance COMMAND extrachain-native-balance)
add_test(NAME runtime COMMAND extrachain-runtime-tests)
if(EXTRACHAIN_BUILD_QT_COMPAT)
    add_test(NAME qt-compat COMMAND extrachain-qt-compat-tests)
endif()
set(EXTRACHAIN_TEST_NAMES core-unit amount-roundtrip sync-roundtrip native-balance runtime)
if(EXTRACHAIN_BUILD_QT_COMPAT)
    list(APPEND EXTRACHAIN_TEST_NAMES qt-compat)
endif()
set_tests_properties(${EXTRACHAIN_TEST_NAMES} PROPERTIES
    ENVIRONMENT_MODIFICATION
        "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt${QT_VERSION_MAJOR}::Core>;PATH=path_list_prepend:$<TARGET_FILE_DIR:unofficial::sqlite3::sqlite3>"
    TIMEOUT 120
)

set(EXTRACHAIN_HEADER_CHECKS
    chain/actor.h
    chain/dag.h
    chain/pack.h
    chain/pack_registry.h
    contracts/contract_manager.h
    contracts/wasm_runtime.h
    managers/extrachain_node.h
    network/network_manager.h
    network/message_body.h
    utils/hash.h
    utils/serialization.h
)
set(EXTRACHAIN_HEADER_CHECK_TARGETS)
foreach(header IN LISTS EXTRACHAIN_HEADER_CHECKS)
    string(REPLACE "/" "_" check_name "${header}")
    string(REPLACE "." "_" check_name "${check_name}")
    set(check_source "${CMAKE_CURRENT_BINARY_DIR}/header-check-${check_name}.cpp")
    file(GENERATE OUTPUT "${check_source}" CONTENT "#include \"${header}\"\n")
    add_library("header-check-${check_name}" OBJECT EXCLUDE_FROM_ALL "${check_source}")
    target_link_libraries("header-check-${check_name}" PRIVATE extrachain)
    list(APPEND EXTRACHAIN_HEADER_CHECK_TARGETS "header-check-${check_name}")
endforeach()
add_custom_target(extrachain-header-checks DEPENDS ${EXTRACHAIN_HEADER_CHECK_TARGETS})
