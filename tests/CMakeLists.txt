cmake_minimum_required(VERSION 3.21)

# Tests use the same deterministic dependency manifest as the library root.
set(VCPKG_MANIFEST_DIR "${CMAKE_CURRENT_LIST_DIR}/.." CACHE PATH "ExtraChain vcpkg manifest directory")

file(STRINGS "../extrachain_version.h" EXTRACHAIN_VERSION_LINE REGEX "extrachain_version =")
string(REGEX MATCH "[0-9]+\\.[0-9]+\\.[0-9]+" EXTRACHAIN_VERSION "${EXTRACHAIN_VERSION_LINE}")
project(extrachain-tests LANGUAGES C CXX VERSION ${EXTRACHAIN_VERSION})

set(EXTRACHAIN_STATIC_BUILD true)
option(EXTRACHAIN_BUILD_QT_COMPAT_TESTS "Build tests for the optional Qt compatibility library" OFF)
set(EXTRACHAIN_BUILD_QT_COMPAT ${EXTRACHAIN_BUILD_QT_COMPAT_TESTS} CACHE BOOL "" FORCE)
include(../CMakeLists.txt)

function(add_core_test_executable target)
    add_executable(${target} ${ARGN})
    target_link_libraries(${target} PRIVATE extrachain-core)
    set_property(TARGET ${target} PROPERTY POSITION_INDEPENDENT_CODE ON)
endfunction()

option(EXTRACHAIN_BUILD_LEGACY_TESTS "Build the obsolete monolithic test target" OFF)
if(EXTRACHAIN_BUILD_LEGACY_TESTS)
    add_core_test_executable(extrachain-tests test.cpp)
endif()

# Self-contained synchronization tests (pack-sync data path, wire-format
# legacy/canonical round-trip, hex->decimal migration round-trip). Kept in a
# separate target so it builds and runs independently of test.cpp.
add_core_test_executable(extrachain-sync-tests sync_test.cpp)

# Load generator: spins up a real node and floods it with reward transactions to
# fill the DAG with many sections (hot -> packs). Usage: extrachain-gen-sections [N]
add_core_test_executable(extrachain-gen-sections gen_sections.cpp)

# Replays the real pack-sync transfer (read_raw -> install_raw) from a generated
# data dir into a fresh store and verifies every packed section round-trips.
# Usage: extrachain-sync-check <server-data-dir> [client-dir]
add_core_test_executable(extrachain-sync-check sync_check.cpp)

# Minimal real node runner for live multi-process sync tests.
#   extrachain-node-run serve <home>
#   extrachain-node-run join  <home> <peer-ip> <target-section>
add_core_test_executable(extrachain-node-run node_run.cpp)
add_core_test_executable(extrachain-control-bench control_bench.cpp)
add_core_test_executable(extrachain-ci-unit ci_unit.cpp)
add_core_test_executable(extrachain-dag-audit dag_audit.cpp)
add_core_test_executable(extrachain-chain-compare chain_compare.cpp)
add_core_test_executable(extrachain-amount-rt amount_roundtrip.cpp)
add_core_test_executable(extrachain-native-balance native_balance.cpp)
add_core_test_executable(extrachain-token-registry token_registry.cpp)
add_core_test_executable(extrachain-dag-recovery dag_recovery.cpp)
add_core_test_executable(extrachain-runtime-tests runtime/runtime_test.cpp)

if(EXTRACHAIN_BUILD_QT_COMPAT_TESTS)
    add_executable(extrachain-qt-compat-tests qt_compat_test.cpp)
    target_link_libraries(extrachain-qt-compat-tests PRIVATE extrachain-qt-compat extrachain)
    set_property(TARGET extrachain-qt-compat-tests PROPERTY POSITION_INDEPENDENT_CODE ON)
endif()

enable_testing()
if(EXTRACHAIN_BUILD_LEGACY_TESTS)
    add_test(NAME legacy-monolith COMMAND extrachain-tests)
endif()
add_test(NAME core-unit COMMAND extrachain-ci-unit)
add_test(NAME amount-roundtrip COMMAND extrachain-amount-rt)
add_test(NAME sync-roundtrip COMMAND extrachain-sync-tests)
add_test(NAME native-balance COMMAND extrachain-native-balance)
add_test(NAME token-registry COMMAND extrachain-token-registry)
add_test(NAME dag-recovery COMMAND extrachain-dag-recovery)
add_test(NAME runtime COMMAND extrachain-runtime-tests)
if(EXTRACHAIN_BUILD_QT_COMPAT_TESTS)
    add_test(NAME qt-compat COMMAND extrachain-qt-compat-tests)
endif()
set(EXTRACHAIN_TEST_NAMES core-unit amount-roundtrip sync-roundtrip native-balance token-registry dag-recovery runtime)
if(EXTRACHAIN_BUILD_LEGACY_TESTS)
    list(APPEND EXTRACHAIN_TEST_NAMES legacy-monolith)
endif()
if(EXTRACHAIN_BUILD_QT_COMPAT_TESTS)
    list(APPEND EXTRACHAIN_TEST_NAMES qt-compat)
endif()
set_tests_properties(${EXTRACHAIN_TEST_NAMES} PROPERTIES
    ENVIRONMENT_MODIFICATION
        "PATH=path_list_prepend:$<TARGET_FILE_DIR:unofficial::sqlite3::sqlite3>"
    TIMEOUT 120
)
if(EXTRACHAIN_BUILD_QT_COMPAT_TESTS)
    set_tests_properties(qt-compat PROPERTIES
        ENVIRONMENT_MODIFICATION
            "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>;PATH=path_list_prepend:$<TARGET_FILE_DIR:unofficial::sqlite3::sqlite3>"
    )
endif()

set(EXTRACHAIN_HEADER_CHECKS
    chain/actor.h
    chain/dag.h
    chain/dag_quarantine.h
    chain/pack.h
    chain/pack_registry.h
    chain/transaction_status.h
    chat/chat_manager.h
    contracts/contract_manager.h
    contracts/wasm_runtime.h
    core/extrachain_node.h
    dfs/dfs_service.h
    network/message_body.h
    network/responder.h
    network/network_runtime.h
    network/peer_context.h
    runtime/event.h
    utils/exc_logs.h
    utils/exc_utils_base64.h
    utils/hash.h
    utils/serialization.h
    utils/version.h
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

if(EXTRACHAIN_BUILD_QT_COMPAT_TESTS)
    set(EXTRACHAIN_QT_HEADER_CHECKS
        dfs/dfs_controller.h
        managers/extrachain_node.h
        network/network_manager.h
    )
    foreach(header IN LISTS EXTRACHAIN_QT_HEADER_CHECKS)
        string(REPLACE "/" "_" check_name "${header}")
        string(REPLACE "." "_" check_name "${check_name}")
        set(check_source "${CMAKE_CURRENT_BINARY_DIR}/header-check-${check_name}.cpp")
        file(GENERATE OUTPUT "${check_source}" CONTENT "#include \"${header}\"\n")
        add_library("header-check-${check_name}" OBJECT EXCLUDE_FROM_ALL "${check_source}")
        target_link_libraries("header-check-${check_name}" PRIVATE extrachain-qt-compat)
        list(APPEND EXTRACHAIN_HEADER_CHECK_TARGETS "header-check-${check_name}")
    endforeach()
endif()
add_custom_target(extrachain-header-checks DEPENDS ${EXTRACHAIN_HEADER_CHECK_TARGETS})

file(GLOB_RECURSE EXTRACHAIN_NO_QT_TEST_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_LIST_DIR}/*.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/*.h"
)
list(REMOVE_ITEM EXTRACHAIN_NO_QT_TEST_SOURCES "${CMAKE_CURRENT_LIST_DIR}/qt_compat_test.cpp")
string(REPLACE ";" "\n" EXTRACHAIN_NO_QT_TEST_MANIFEST "${EXTRACHAIN_NO_QT_TEST_SOURCES}")
set(EXTRACHAIN_NO_QT_TEST_MANIFEST_FILE "${CMAKE_CURRENT_BINARY_DIR}/extrachain-no-qt-tests.txt")
file(GENERATE OUTPUT "${EXTRACHAIN_NO_QT_TEST_MANIFEST_FILE}" CONTENT "${EXTRACHAIN_NO_QT_TEST_MANIFEST}\n")
add_custom_target(extrachain-tests-qt-boundary-check ALL
    COMMAND ${CMAKE_COMMAND}
        -DCORE_SOURCE_ROOT=${CMAKE_CURRENT_LIST_DIR}/..
        -DCORE_SOURCE_MANIFEST=${EXTRACHAIN_NO_QT_TEST_MANIFEST_FILE}
        -P ${CMAKE_CURRENT_LIST_DIR}/../cmake/CheckCoreQtBoundary.cmake
    VERBATIM
)
