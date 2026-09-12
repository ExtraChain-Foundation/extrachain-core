cmake_minimum_required(VERSION 3.21)

# Tests use the same deterministic dependency manifest as the library root.
set(VCPKG_MANIFEST_DIR "${CMAKE_CURRENT_LIST_DIR}/.." CACHE PATH "ExtraChain vcpkg manifest directory")

file(STRINGS "../extrachain_version.h" EXTRACHAIN_VERSION_LINE REGEX "extrachain_version =")
string(REGEX MATCH "[0-9]+\\.[0-9]+\\.[0-9]+" EXTRACHAIN_VERSION "${EXTRACHAIN_VERSION_LINE}")
project(extrachain-tests LANGUAGES C CXX VERSION ${EXTRACHAIN_VERSION})

enable_testing()
find_package(Python3 REQUIRED COMPONENTS Interpreter)
add_test(NAME shadow-vector-audit COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/shadow_vector_audit.py)

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
add_core_test_executable(extrachain-audit-admission audit_admission.cpp)
add_test(NAME audit-admission COMMAND extrachain-audit-admission)
set_tests_properties(audit-admission PROPERTIES TIMEOUT 30)
add_core_test_executable(extrachain-vector-index-migration vector_index_migration.cpp)
add_core_test_executable(extrachain-vector-descriptor vector_descriptor.cpp)
add_test(NAME vector-descriptor COMMAND extrachain-vector-descriptor)
set_tests_properties(vector-descriptor PROPERTIES TIMEOUT 20)
add_core_test_executable(extrachain-messagepack-budget messagepack_budget.cpp)
add_test(NAME messagepack-budget COMMAND extrachain-messagepack-budget)
add_core_test_executable(extrachain-vector-snapshot vector_snapshot.cpp)
add_core_test_executable(extrachain-vector-sync-limits vector_sync_limits.cpp)
add_test(NAME vector-sync-limits COMMAND extrachain-vector-sync-limits)
set_tests_properties(vector-sync-limits PROPERTIES TIMEOUT 30)
add_test(NAME vector-snapshot COMMAND extrachain-vector-snapshot)
set_tests_properties(vector-snapshot PROPERTIES TIMEOUT 30)
add_test(NAME vector-index-migration COMMAND extrachain-vector-index-migration)
set_tests_properties(vector-index-migration PROPERTIES TIMEOUT 15)
add_core_test_executable(extrachain-vector-index vector_index.cpp)
add_test(NAME vector-index COMMAND extrachain-vector-index)
set_tests_properties(vector-index PROPERTIES TIMEOUT 30)
add_core_test_executable(extrachain-websocket-queue websocket_queue.cpp)
add_test(NAME websocket-queue COMMAND extrachain-websocket-queue)
set_tests_properties(websocket-queue PROPERTIES TIMEOUT 30)
add_core_test_executable(extrachain-peer-identity peer_identity.cpp)
add_test(NAME peer-identity COMMAND extrachain-peer-identity)
set_tests_properties(peer-identity PROPERTIES TIMEOUT 10)
add_core_test_executable(extrachain-handshake-limits handshake_limits.cpp)
add_test(NAME handshake-limits COMMAND extrachain-handshake-limits)
set_tests_properties(handshake-limits PROPERTIES TIMEOUT 30)
add_core_test_executable(extrachain-seed-profile-tests seed_profile.cpp)
add_test(NAME seed-profile COMMAND extrachain-seed-profile-tests)
set_tests_properties(seed-profile PROPERTIES TIMEOUT 30)
add_core_test_executable(extrachain-file-cipher-tests file_cipher.cpp)
add_test(NAME file-cipher COMMAND extrachain-file-cipher-tests)
set_tests_properties(file-cipher PROPERTIES TIMEOUT 30)
add_core_test_executable(extrachain-thoth-token-tests thoth_tokens.cpp)
add_test(NAME thoth-tokens COMMAND extrachain-thoth-token-tests)
set_tests_properties(thoth-tokens PROPERTIES TIMEOUT 30)
if(APPLE OR CMAKE_SYSTEM_NAME STREQUAL "Linux")
    add_core_test_executable(extrachain-migration-response-bounds migration_response_bounds.cpp)
    add_test(NAME migration-response-bounds COMMAND extrachain-migration-response-bounds)
    set_tests_properties(migration-response-bounds PROPERTIES TIMEOUT 30)
endif()
add_core_test_executable(extrachain-dag-audit dag_audit.cpp)
add_core_test_executable(extrachain-chain-compare chain_compare.cpp)
add_core_test_executable(extrachain-amount-rt amount_roundtrip.cpp)
add_core_test_executable(extrachain-native-balance native_balance.cpp)
add_core_test_executable(extrachain-token-registry token_registry.cpp)
add_core_test_executable(extrachain-dag-recovery dag_recovery.cpp)
add_core_test_executable(extrachain-runtime-tests runtime/runtime_test.cpp)
add_core_test_executable(extrachain-consensus-tests consensus_test.cpp)
add_core_test_executable(extrachain-consensus-protocol-tests consensus_protocol_test.cpp)
add_core_test_executable(extrachain-consensus-network-tests consensus_network_test.cpp)
add_core_test_executable(extrachain-shadow-recovery-tests shadow_recovery_test.cpp)
add_core_test_executable(extrachain-shadow-bootstrap-tests shadow_bootstrap_test.cpp)
add_core_test_executable(extrachain-shadow-bundle shadow_bundle.cpp)
add_core_test_executable(extrachain-consensus-benchmark consensus_benchmark.cpp)
add_core_test_executable(extrachain-envelope-tests envelope_test.cpp)
add_core_test_executable(extrachain-signature-tests signature_test.cpp)
add_core_test_executable(extrachain-crypto-tests encryption_test.cpp)
add_core_test_executable(extrachain-msgpack-budget-tests msgpack_budget.cpp)
if(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_definitions(extrachain-msgpack-budget-tests PRIVATE EXTRACHAIN_TEST_TRACK_MALLOC=1)
    target_link_options(extrachain-msgpack-budget-tests PRIVATE "LINKER:--wrap=malloc")
endif()
add_test(NAME msgpack-budget COMMAND extrachain-msgpack-budget-tests)
set_tests_properties(msgpack-budget PROPERTIES TIMEOUT 30)
add_core_test_executable(extrachain-dfs-vector-tests dfs_vector_test.cpp)
add_core_test_executable(extrachain-dfs-sync-tests dfs_sync_test.cpp)
add_core_test_executable(extrachain-dfs-vector-bench dfs_vector_bench.cpp)

if(EXTRACHAIN_BUILD_QT_COMPAT_TESTS)
    add_executable(extrachain-qt-compat-tests qt_compat_test.cpp)
    target_link_libraries(extrachain-qt-compat-tests PRIVATE extrachain-qt-compat extrachain)
    set_property(TARGET extrachain-qt-compat-tests PROPERTY POSITION_INDEPENDENT_CODE ON)
endif()

add_core_test_executable(extrachain-relay-tests relay_test.cpp)
add_core_test_executable(extrachain-contract-storage-tests contract_storage.cpp)
add_core_test_executable(extrachain-contract-depth-replay-tests contract_depth_replay.cpp)
add_core_test_executable(extrachain-dfs-recovery-tests dfs_recovery.cpp)
add_core_test_executable(extrachain-dfs-source-tests dfs_source_recovery.cpp)
add_core_test_executable(extrachain-dfs-vector-replication dfs_vector_replication.cpp)
add_test(NAME dfs-complete-file-recovery COMMAND extrachain-dfs-source-tests metadata)
add_test(NAME dfs-source-backoff COMMAND extrachain-dfs-source-tests backoff)
add_test(NAME dfs-vector-converge COMMAND extrachain-dfs-vector-replication converge)
add_test(NAME dfs-vector-delta COMMAND extrachain-dfs-vector-replication delta)
add_test(NAME dfs-vector-failover COMMAND extrachain-dfs-vector-replication failover)
set_tests_properties(dfs-vector-failover PROPERTIES TIMEOUT 30)
set_tests_properties(dfs-vector-delta PROPERTIES TIMEOUT 60)
add_test(NAME dfs-vector-proof-rejection COMMAND extrachain-dfs-vector-replication tamper)
set_tests_properties(dfs-vector-proof-rejection PROPERTIES TIMEOUT 30)
add_test(NAME dfs-vector-staged COMMAND extrachain-dfs-vector-replication staged)
add_test(NAME dfs-vector-bounded COMMAND extrachain-dfs-vector-replication bounded)
set_tests_properties(dfs-complete-file-recovery dfs-source-backoff PROPERTIES TIMEOUT 30)
add_test(NAME dfs-recovery COMMAND extrachain-dfs-recovery-tests)
add_test(NAME contract-storage COMMAND extrachain-contract-storage-tests)
add_test(NAME contract-depth-replay COMMAND extrachain-contract-depth-replay-tests
         ${CMAKE_CURRENT_LIST_DIR}/contracts/depth-fixture.wasm)
set_tests_properties(contract-depth-replay PROPERTIES
    TIMEOUT 120
    ENVIRONMENT "ASAN_OPTIONS=detect_leaks=1:halt_on_error=1:detect_stack_use_after_return=0"
)
add_test(NAME relay COMMAND extrachain-relay-tests)
if(EXTRACHAIN_BUILD_CBINDINGS)
    add_test(NAME cbindings-lifecycle COMMAND ${Python3_EXECUTABLE}
             ${CMAKE_CURRENT_LIST_DIR}/cbindings_lifecycle.py $<TARGET_FILE:extrachainc>)
    add_test(NAME cbindings-dfs-json COMMAND ${Python3_EXECUTABLE}
             ${CMAKE_CURRENT_LIST_DIR}/cbindings_dfs_json.py $<TARGET_FILE:extrachainc>)
    set_tests_properties(cbindings-dfs-json PROPERTIES TIMEOUT 30)
    add_test(NAME cbindings-profile-backup COMMAND ${Python3_EXECUTABLE}
             ${CMAKE_CURRENT_LIST_DIR}/cbindings_profile_backup.py $<TARGET_FILE:extrachainc>)
    set_tests_properties(cbindings-profile-backup PROPERTIES TIMEOUT 30)
endif()
if(EXTRACHAIN_BUILD_LEGACY_TESTS)
    add_test(NAME legacy-monolith COMMAND extrachain-tests)
endif()
add_test(NAME core-unit COMMAND extrachain-ci-unit)
add_test(NAME amount-roundtrip COMMAND extrachain-amount-rt)
add_test(NAME sync-roundtrip COMMAND extrachain-sync-tests)
add_test(NAME native-balance COMMAND extrachain-native-balance)
add_test(NAME token-registry COMMAND extrachain-token-registry)
add_test(NAME dag-recovery COMMAND extrachain-dag-recovery)
add_test(NAME envelope COMMAND extrachain-envelope-tests)
add_test(NAME signature COMMAND extrachain-signature-tests)
set_tests_properties(signature PROPERTIES TIMEOUT 30)
add_test(NAME crypto COMMAND extrachain-crypto-tests)
add_test(NAME dfs-vector COMMAND extrachain-dfs-vector-tests)
add_test(NAME dfs-sync COMMAND extrachain-dfs-sync-tests)
add_test(NAME runtime COMMAND extrachain-runtime-tests)
add_test(NAME consensus COMMAND extrachain-consensus-tests)
add_test(NAME consensus-protocol COMMAND extrachain-consensus-protocol-tests)
add_test(NAME consensus-network COMMAND extrachain-consensus-network-tests)
add_test(NAME shadow-recovery COMMAND extrachain-shadow-recovery-tests)
add_test(NAME shadow-bootstrap COMMAND extrachain-shadow-bootstrap-tests)
if(EXTRACHAIN_BUILD_QT_COMPAT_TESTS)
    add_test(NAME qt-compat COMMAND extrachain-qt-compat-tests)
endif()
set(EXTRACHAIN_TEST_NAMES contract-depth-replay relay core-unit amount-roundtrip sync-roundtrip native-balance token-registry dag-recovery runtime consensus consensus-protocol consensus-network shadow-recovery)
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
    chain/dag_recovery.h
    chain/pack.h
    chain/pack_registry.h
    chain/transaction_status.h
    chat/chat_manager.h
    contracts/contract_manager.h
    contracts/wasm_runtime.h
    consensus/consensus_engine.h
    consensus/relay_transport.h
    consensus/consensus_protocol.h
    consensus/intent_store.h
    consensus/light_client.h
    consensus/consensus_service.h
    consensus/consensus_types.h
    consensus/peer_authenticator.h
    consensus/safety_store.h
    consensus/shadow_consensus.h
    consensus/validator_set.h
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
list(APPEND EXTRACHAIN_HEADER_CHECKS
    chain/dag_cache.h
    contracts/dfs_contract_storage.h
    dfs/dfs_utils.h
    dfs/load_manager.h
    encryption/box_session.h
    network/isocket_service.h
    network/network_service.h
    network/peer_meta.h
    runtime/runtime.h
    utils/bignumber.h
    utils/bignumber_float.h
    utils/exc_magic.h
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
