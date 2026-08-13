cmake_minimum_required(VERSION 3.19)

if(CMAKE_CURRENT_SOURCE_DIR STREQUAL CMAKE_SOURCE_DIR)
  project(
    extrachain
    LANGUAGES C CXX
    DESCRIPTION "ExtraChain Core")
endif()

set(CMAKE_INCLUDE_CURRENT_DIR ON)
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

option(EXTRACHAIN_STATIC_BUILD "Build ExtraChain Core as a static library" OFF)

if(EXTRACHAIN_STATIC_BUILD)
  message("Build static ExtraChain")
endif()

# c++ settings
message(
  "Compiler: ${CMAKE_CXX_COMPILER_ID} | front: ${CMAKE_CXX_COMPILER_FRONTEND_VARIANT} | "
  "build: ${CMAKE_BUILD_TYPE}")

if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU" OR CMAKE_CXX_COMPILER_ID STREQUAL
                                           "Clang")
  set(CMAKE_CXX_FLAGS
      "${CMAKE_CXX_FLAGS} -Wpedantic -Werror=return-type -Werror=implicit-fallthrough"
  )
  if(UNIX)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fPIC")
  endif(UNIX)
endif()
if(MSVC)
  add_compile_definitions(NOMINMAX)
  # add_compile_options(/W4/analyze)
endif()

set(EXTRACHAIN_CORE_INCLUDES
    ${CMAKE_CURRENT_LIST_DIR} ${CMAKE_CURRENT_LIST_DIR}/headers
    ${CMAKE_CURRENT_BINARY_DIR}/generated)
option(EXTRACHAIN_BUILD_QT_COMPAT "Build the Qt compatibility layer" ON)
option(EXTRACHAIN_BUILD_LEGACY_QT_NODE
       "Deprecated alias for EXTRACHAIN_BUILD_QT_COMPAT" ON)
if(NOT EXTRACHAIN_BUILD_LEGACY_QT_NODE)
  set(EXTRACHAIN_BUILD_QT_COMPAT OFF)
endif()

if(EXTRACHAIN_BUILD_QT_COMPAT)
  find_package(
    Qt6
    COMPONENTS Core Network
    REQUIRED)
endif()

find_package(unofficial-sodium CONFIG REQUIRED)
find_package(unofficial-sqlite3 CONFIG REQUIRED)
find_package(Boost CONFIG REQUIRED COMPONENTS system thread json)
find_package(fmt CONFIG REQUIRED)
find_package(magic_enum CONFIG REQUIRED)
find_package(unofficial-hash-library CONFIG REQUIRED)
find_package(msgpack-cxx CONFIG REQUIRED)
find_package(blake3 CONFIG REQUIRED)
find_package(zstd CONFIG REQUIRED)
find_package(ZLIB REQUIRED)

set(EXTRACHAIN_EMBEDDED_CONTRACTS_HEADER
    ${CMAKE_CURRENT_BINARY_DIR}/generated/contracts/embedded_contracts.h)
file(MAKE_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/generated/contracts)
file(WRITE ${EXTRACHAIN_EMBEDDED_CONTRACTS_HEADER}
     "#pragma once\nnamespace " "ExtraChain::Contracts::Embedded {\n")
find_program(EXTRACHAIN_XXD_EXECUTABLE xxd)
foreach(contract_name IN
        ITEMS fungible_token_rust fungible_token_assemblyscript
              non_fungible_token_rust non_fungible_token_assemblyscript)
  if(EXTRACHAIN_XXD_EXECUTABLE)
    set(contract_fragment
        ${CMAKE_CURRENT_BINARY_DIR}/generated/contracts/${contract_name}.inc)
    execute_process(
      COMMAND ${EXTRACHAIN_XXD_EXECUTABLE} -i -n ${contract_name}
              ${CMAKE_CURRENT_LIST_DIR}/contracts/standard/${contract_name}.wasm
      OUTPUT_FILE ${contract_fragment} COMMAND_ERROR_IS_FATAL ANY)
    file(APPEND ${EXTRACHAIN_EMBEDDED_CONTRACTS_HEADER}
         "#include " "\"contracts/" "${contract_name}.inc\"\n")
  else()
    file(READ
         ${CMAKE_CURRENT_LIST_DIR}/contracts/standard/${contract_name}.wasm
         contract_hex HEX)
    string(REGEX REPLACE "(..)" "0x\\1," contract_bytes "${contract_hex}")
    file(APPEND ${EXTRACHAIN_EMBEDDED_CONTRACTS_HEADER}
         "inline constexpr unsigned char " "${contract_name}[] = "
         "{${contract_bytes}};\n")
  endif()
endforeach()
file(APPEND ${EXTRACHAIN_EMBEDDED_CONTRACTS_HEADER} "}\n")

set(EXTRACHAIN_CORE_SOURCES
    ${CMAKE_CURRENT_LIST_DIR}/extrachain_version.h
    ${CMAKE_CURRENT_LIST_DIR}/headers/core/types.h
    ${CMAKE_CURRENT_LIST_DIR}/headers/core/byte_array.h
    ${CMAKE_CURRENT_LIST_DIR}/headers/core/container.h
    ${CMAKE_CURRENT_LIST_DIR}/headers/dfs/dfs_service.h
    ${CMAKE_CURRENT_LIST_DIR}/headers/dfs/dirs_manager.h
    ${CMAKE_CURRENT_LIST_DIR}/headers/dfs/load_manager.h
    ${CMAKE_CURRENT_LIST_DIR}/headers/dfs/version_controller.h
    ${CMAKE_CURRENT_LIST_DIR}/headers/dfs/historical_collection.h
    ${CMAKE_CURRENT_LIST_DIR}/headers/dfs/dfs_utils.h
    ${CMAKE_CURRENT_LIST_DIR}/headers/dfs/name_validator.h
    ${CMAKE_CURRENT_LIST_DIR}/headers/dfs/collection_template.h
    ${CMAKE_CURRENT_LIST_DIR}/headers/dfs/dfs_vector.h
    ${CMAKE_CURRENT_LIST_DIR}/headers/chain/actor.h
    ${CMAKE_CURRENT_LIST_DIR}/headers/chain/actor_id.h
    ${CMAKE_CURRENT_LIST_DIR}/headers/chain/actor_index.h
    ${CMAKE_CURRENT_LIST_DIR}/headers/chain/actor_filter.h
    ${CMAKE_CURRENT_LIST_DIR}/headers/chain/transaction.h
    ${CMAKE_CURRENT_LIST_DIR}/headers/chain/transaction_status.h
    ${CMAKE_CURRENT_LIST_DIR}/headers/chain/transaction_cache.h
    ${CMAKE_CURRENT_LIST_DIR}/headers/chain/private_profile.h
    ${CMAKE_CURRENT_LIST_DIR}/headers/chain/dag.h
    ${CMAKE_CURRENT_LIST_DIR}/headers/chain/dag_cache.h
    ${CMAKE_CURRENT_LIST_DIR}/headers/chain/pack.h
    ${CMAKE_CURRENT_LIST_DIR}/headers/chain/pack_registry.h
    ${CMAKE_CURRENT_LIST_DIR}/headers/chain/hot_section_store.h
    ${CMAKE_CURRENT_LIST_DIR}/headers/chain/dag_migration.h
    ${CMAKE_CURRENT_LIST_DIR}/headers/chain/chain_index.h
    ${CMAKE_CURRENT_LIST_DIR}/headers/chain/control_index.h
    ${CMAKE_CURRENT_LIST_DIR}/headers/encryption/encryption_tools.h
    ${CMAKE_CURRENT_LIST_DIR}/headers/encryption/key_private.h
    ${CMAKE_CURRENT_LIST_DIR}/headers/encryption/key_public.h
    ${CMAKE_CURRENT_LIST_DIR}/headers/managers/account_controller.h
    ${CMAKE_CURRENT_LIST_DIR}/headers/core/extrachain_node.h
    ${CMAKE_CURRENT_LIST_DIR}/headers/managers/luminance_manager.h
    ${CMAKE_CURRENT_LIST_DIR}/headers/managers/data_mining_manager.h
    ${CMAKE_CURRENT_LIST_DIR}/headers/managers/file_data_manager.h
    ${CMAKE_CURRENT_LIST_DIR}/headers/managers/token_manager.h
    ${CMAKE_CURRENT_LIST_DIR}/headers/managers/thoth_manager.h
    ${CMAKE_CURRENT_LIST_DIR}/headers/managers/janus_manager.h
    ${CMAKE_CURRENT_LIST_DIR}/headers/extrachain_global.h
    ${CMAKE_CURRENT_LIST_DIR}/headers/network/network_service.h
    ${CMAKE_CURRENT_LIST_DIR}/headers/network/network_runtime.h
    ${CMAKE_CURRENT_LIST_DIR}/headers/network/network_status.h
    ${CMAKE_CURRENT_LIST_DIR}/headers/network/responder.h
    ${CMAKE_CURRENT_LIST_DIR}/headers/network/traffic_meter.h
    ${CMAKE_CURRENT_LIST_DIR}/headers/network/message_body.h
    ${CMAKE_CURRENT_LIST_DIR}/headers/network/isocket_service.h
    ${CMAKE_CURRENT_LIST_DIR}/headers/network/peer_context.h
    ${CMAKE_CURRENT_LIST_DIR}/headers/network/peer_meta.h
    ${CMAKE_CURRENT_LIST_DIR}/headers/network/wire_format.h
    ${CMAKE_CURRENT_LIST_DIR}/headers/network/websocket_service.h
    ${CMAKE_CURRENT_LIST_DIR}/headers/runtime/event.h
    ${CMAKE_CURRENT_LIST_DIR}/headers/runtime/deadline_task.h
    ${CMAKE_CURRENT_LIST_DIR}/headers/runtime/periodic_task.h
    ${CMAKE_CURRENT_LIST_DIR}/headers/runtime/runtime.h
    ${CMAKE_CURRENT_LIST_DIR}/headers/utils/autologinhash.h
    ${CMAKE_CURRENT_LIST_DIR}/headers/utils/bignumber.h
    ${CMAKE_CURRENT_LIST_DIR}/headers/utils/bignumber_float.h
    ${CMAKE_CURRENT_LIST_DIR}/headers/utils/fs_path.h
    ${CMAKE_CURRENT_LIST_DIR}/headers/utils/file_io.h
    ${CMAKE_CURRENT_LIST_DIR}/headers/utils/hash.h
    ${CMAKE_CURRENT_LIST_DIR}/headers/utils/db_connector.h
    ${CMAKE_CURRENT_LIST_DIR}/headers/utils/db_connector_strict.h
    ${CMAKE_CURRENT_LIST_DIR}/headers/utils/db_schema.h
    ${CMAKE_CURRENT_LIST_DIR}/headers/utils/db_iterator.h
    ${CMAKE_CURRENT_LIST_DIR}/headers/utils/exc_utils.h
    ${CMAKE_CURRENT_LIST_DIR}/headers/utils/exc_utils_base64.h
    ${CMAKE_CURRENT_LIST_DIR}/headers/utils/serialization.h
    ${CMAKE_CURRENT_LIST_DIR}/headers/utils/thread_pool_boost.h
    ${CMAKE_CURRENT_LIST_DIR}/headers/utils/version.h
    ${CMAKE_CURRENT_LIST_DIR}/headers/utils/exc_magic.h
    ${CMAKE_CURRENT_LIST_DIR}/headers/utils/exc_logs.h
    ${CMAKE_CURRENT_LIST_DIR}/headers/utils/exc_logs_filter.h
    ${CMAKE_CURRENT_LIST_DIR}/headers/utils/exc_logs_extra.h
    ${CMAKE_CURRENT_LIST_DIR}/headers/utils/exc_msgpack_describe.h
    ${CMAKE_CURRENT_LIST_DIR}/headers/utils/safeptr.h
    ${CMAKE_CURRENT_LIST_DIR}/headers/utils/compression.h
    ${CMAKE_CURRENT_LIST_DIR}/headers/utils/legacy_compression.h
    ${CMAKE_CURRENT_LIST_DIR}/headers/chat/chat_manager.h
    ${CMAKE_CURRENT_LIST_DIR}/headers/chat/chat.h
    ${CMAKE_CURRENT_LIST_DIR}/headers/chat/chat_folders.h
    ${CMAKE_CURRENT_LIST_DIR}/headers/chat/chat_profile.h
    ${CMAKE_CURRENT_LIST_DIR}/headers/chat/message.h
    ${CMAKE_CURRENT_LIST_DIR}/headers/contracts/contract_types.h
    ${CMAKE_CURRENT_LIST_DIR}/headers/contracts/contract_codec.h
    ${CMAKE_CURRENT_LIST_DIR}/headers/contracts/contract_manager.h
    ${CMAKE_CURRENT_LIST_DIR}/headers/contracts/contract_module.h
    ${CMAKE_CURRENT_LIST_DIR}/headers/contracts/dfs_contract_storage.h
    ${CMAKE_CURRENT_LIST_DIR}/headers/contracts/contract_transaction.h
    ${CMAKE_CURRENT_LIST_DIR}/headers/contracts/standard_token.h
    ${CMAKE_CURRENT_LIST_DIR}/headers/contracts/wasm_runtime.h
    ${CMAKE_CURRENT_LIST_DIR}/headers/contracts/toolchain_registry.h
    ${EXTRACHAIN_EMBEDDED_CONTRACTS_HEADER}
    ${CMAKE_CURRENT_LIST_DIR}/sources/dfs/dfs_service.cpp
    ${CMAKE_CURRENT_LIST_DIR}/sources/dfs/dirs_manager.cpp
    ${CMAKE_CURRENT_LIST_DIR}/sources/dfs/load_manager.cpp
    ${CMAKE_CURRENT_LIST_DIR}/sources/dfs/collection_template.cpp
    ${CMAKE_CURRENT_LIST_DIR}/sources/dfs/version_controller.cpp
    ${CMAKE_CURRENT_LIST_DIR}/sources/dfs/historical_collection.cpp
    ${CMAKE_CURRENT_LIST_DIR}/sources/dfs/dfs_utils.cpp
    ${CMAKE_CURRENT_LIST_DIR}/sources/dfs/name_validator.cpp
    ${CMAKE_CURRENT_LIST_DIR}/sources/dfs/dfs_vector.cpp
    ${CMAKE_CURRENT_LIST_DIR}/sources/chain/actor_id.cpp
    ${CMAKE_CURRENT_LIST_DIR}/sources/chain/actor_index.cpp
    ${CMAKE_CURRENT_LIST_DIR}/sources/chain/transaction.cpp
    ${CMAKE_CURRENT_LIST_DIR}/sources/chain/transaction_cache.cpp
    ${CMAKE_CURRENT_LIST_DIR}/sources/chain/private_profile.cpp
    ${CMAKE_CURRENT_LIST_DIR}/sources/chain/dag.cpp
    ${CMAKE_CURRENT_LIST_DIR}/sources/chain/dag_admission.cpp
    ${CMAKE_CURRENT_LIST_DIR}/sources/chain/dag_cache.cpp
    ${CMAKE_CURRENT_LIST_DIR}/sources/chain/pack.cpp
    ${CMAKE_CURRENT_LIST_DIR}/sources/chain/pack_registry.cpp
    ${CMAKE_CURRENT_LIST_DIR}/sources/chain/hot_section_store.cpp
    ${CMAKE_CURRENT_LIST_DIR}/sources/chain/dag_migration.cpp
    ${CMAKE_CURRENT_LIST_DIR}/sources/chain/chain_index.cpp
    ${CMAKE_CURRENT_LIST_DIR}/sources/chain/control_index.cpp
    ${CMAKE_CURRENT_LIST_DIR}/sources/encryption/encryption_tools.cpp
    ${CMAKE_CURRENT_LIST_DIR}/sources/encryption/key_private.cpp
    ${CMAKE_CURRENT_LIST_DIR}/sources/encryption/key_public.cpp
    ${CMAKE_CURRENT_LIST_DIR}/sources/managers/account_controller.cpp
    ${CMAKE_CURRENT_LIST_DIR}/sources/managers/data_mining_manager.cpp
    ${CMAKE_CURRENT_LIST_DIR}/sources/managers/file_data_manager.cpp
    ${CMAKE_CURRENT_LIST_DIR}/sources/core/extrachain_node.cpp
    ${CMAKE_CURRENT_LIST_DIR}/sources/managers/luminance_manager.cpp
    ${CMAKE_CURRENT_LIST_DIR}/sources/managers/token_manager.cpp
    ${CMAKE_CURRENT_LIST_DIR}/sources/managers/thoth_manager.cpp
    ${CMAKE_CURRENT_LIST_DIR}/sources/managers/janus_manager.cpp
    ${CMAKE_CURRENT_LIST_DIR}/sources/network/network_service.cpp
    ${CMAKE_CURRENT_LIST_DIR}/sources/network/network_runtime.cpp
    ${CMAKE_CURRENT_LIST_DIR}/sources/network/network_status.cpp
    ${CMAKE_CURRENT_LIST_DIR}/sources/network/responder.cpp
    ${CMAKE_CURRENT_LIST_DIR}/sources/network/traffic_meter.cpp
    ${CMAKE_CURRENT_LIST_DIR}/sources/network/isocket_service.cpp
    ${CMAKE_CURRENT_LIST_DIR}/sources/network/wire_format.cpp
    ${CMAKE_CURRENT_LIST_DIR}/sources/network/websocket_service.cpp
    ${CMAKE_CURRENT_LIST_DIR}/sources/utils/autologinhash.cpp
    ${CMAKE_CURRENT_LIST_DIR}/sources/utils/bignumber.cpp
    ${CMAKE_CURRENT_LIST_DIR}/sources/utils/bignumber_float.cpp
    ${CMAKE_CURRENT_LIST_DIR}/sources/utils/file_io.cpp
    ${CMAKE_CURRENT_LIST_DIR}/sources/utils/fs_path.cpp
    ${CMAKE_CURRENT_LIST_DIR}/sources/utils/hash.cpp
    ${CMAKE_CURRENT_LIST_DIR}/sources/utils/db_connector.cpp
    ${CMAKE_CURRENT_LIST_DIR}/sources/utils/db_schema.cpp
    ${CMAKE_CURRENT_LIST_DIR}/sources/utils/db_iterator.cpp
    ${CMAKE_CURRENT_LIST_DIR}/sources/utils/exc_utils.cpp
    ${CMAKE_CURRENT_LIST_DIR}/sources/utils/compression.cpp
    ${CMAKE_CURRENT_LIST_DIR}/sources/utils/legacy_compression.cpp
    ${CMAKE_CURRENT_LIST_DIR}/sources/utils/thread_pool_boost.cpp
    ${CMAKE_CURRENT_LIST_DIR}/sources/utils/version.cpp
    ${CMAKE_CURRENT_LIST_DIR}/sources/runtime/deadline_task.cpp
    ${CMAKE_CURRENT_LIST_DIR}/sources/runtime/periodic_task.cpp
    ${CMAKE_CURRENT_LIST_DIR}/sources/runtime/runtime.cpp
    ${CMAKE_CURRENT_LIST_DIR}/sources/chat/chat_manager.cpp
    ${CMAKE_CURRENT_LIST_DIR}/sources/chat/chat.cpp
    ${CMAKE_CURRENT_LIST_DIR}/sources/chat/chat_folders.cpp
    ${CMAKE_CURRENT_LIST_DIR}/sources/chat/chat_profile.cpp
    ${CMAKE_CURRENT_LIST_DIR}/sources/chat/message.cpp
    ${CMAKE_CURRENT_LIST_DIR}/sources/contracts/wasm_runtime.cpp
    ${CMAKE_CURRENT_LIST_DIR}/sources/contracts/wasm_policy.cpp
    ${CMAKE_CURRENT_LIST_DIR}/sources/contracts/contract_codec.cpp
    ${CMAKE_CURRENT_LIST_DIR}/sources/contracts/contract_manager.cpp
    ${CMAKE_CURRENT_LIST_DIR}/sources/contracts/contract_module.cpp
    ${CMAKE_CURRENT_LIST_DIR}/sources/contracts/standard_token.cpp
    ${CMAKE_CURRENT_LIST_DIR}/sources/contracts/dfs_contract_storage.cpp
    ${CMAKE_CURRENT_LIST_DIR}/sources/contracts/toolchain_registry.cpp)

include(${CMAKE_CURRENT_LIST_DIR}/cmake/ExtraChainWamr.cmake)

# C FFI bindings(optional)
option(EXTRACHAIN_BUILD_CBINDINGS "Build C FFI bindings (libextrachainc)" OFF)
option(EXTRACHAIN_BUILD_CBINDINGS_EXAMPLE "Build C FFI example program" OFF)

if(EXTRACHAIN_STATIC_BUILD)
  message("ExtraChain Core: static build")
  add_library(extrachain-core STATIC ${EXTRACHAIN_CORE_SOURCES})
else()
  message("ExtraChain Core: library build")
  add_library(extrachain-core SHARED ${EXTRACHAIN_CORE_SOURCES})
  target_compile_definitions(
    extrachain-core
    PUBLIC EXTRACHAIN_LIBRARY_ACTIVATE
    PRIVATE EXTRACHAIN_LIBRARY)
endif()
add_library(extrachain-runtime ALIAS extrachain-core)

# link time optimization
if(EXTRACHAIN_ENABLE_LTO)
  set_property(TARGET extrachain-core PROPERTY POSITION_INDEPENDENT_CODE ON)
  include(CheckIPOSupported)
  check_ipo_supported(RESULT lto_supported OUTPUT error)
  if(lto_supported)
    message(STATUS "LTO enabled")
    set_property(TARGET extrachain-core PROPERTY INTERPROCEDURAL_OPTIMIZATION
                                                 TRUE)
  else()
    message(STATUS "LTO not supported: <${error}>")
  endif(lto_supported)
endif(EXTRACHAIN_ENABLE_LTO)

# git
execute_process(
  COMMAND git -C "${CMAKE_CURRENT_LIST_DIR}" describe --always --tags
  OUTPUT_VARIABLE GIT_COMMIT_CORE
  OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
execute_process(
  COMMAND git -C "${CMAKE_CURRENT_LIST_DIR}" symbolic-ref --short HEAD
  OUTPUT_VARIABLE GIT_BRANCH_CORE
  OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
string(TIMESTAMP COMPILE_DATE "%m.%d")
add_compile_definitions(
  _CRT_SECURE_NO_WARNINGS
  EXTRACHAIN_CMAKE
  COMPILE_DATE="${COMPILE_DATE}"
  GIT_COMMIT_CORE="${GIT_COMMIT_CORE}"
  GIT_BRANCH_CORE="${GIT_BRANCH_CORE}"
  MAGIC_ENUM_RANGE_MIN=0
  MAGIC_ENUM_RANGE_MAX=256)

option(EXTRACHAIN_ENABLE_PCH "Use precompiled headers for ExtraChain Core" ON)

if(EXTRACHAIN_ENABLE_PCH AND NOT ANDROID)
  target_precompile_headers(extrachain-core PRIVATE
                            ${CMAKE_CURRENT_LIST_DIR}/headers/precompiled.h)
endif()
if(MSVC)
  target_compile_options(extrachain-core PRIVATE /bigobj)
  target_compile_definitions(
    extrachain-core PUBLIC NOMINMAX WIN32_LEAN_AND_MEAN _WIN32_WINNT=0x0A00)
  if(EXTRACHAIN_ENABLE_PCH AND NOT ANDROID)
    set_source_files_properties(
      ${CMAKE_CURRENT_LIST_DIR}/sources/contracts/dfs_contract_storage.cpp
      ${CMAKE_CURRENT_LIST_DIR}/sources/contracts/toolchain_registry.cpp
      ${CMAKE_CURRENT_LIST_DIR}/sources/managers/thoth_manager.cpp
      PROPERTIES SKIP_PRECOMPILE_HEADERS ON)
  endif()
endif()
target_include_directories(extrachain-core PUBLIC ${EXTRACHAIN_CORE_INCLUDES})

set(bip3x_BUILD_SHARED_LIBS
    OFF
    CACHE BOOL "")
set(bip3x_BUILD_JNI_BINDINGS
    OFF
    CACHE BOOL "")
set(bip3x_BUILD_C_BINDINGS
    OFF
    CACHE BOOL "")
set(bip3x_BUILD_TESTS
    OFF
    CACHE BOOL "")
set(bip3x_USE_OPENSSL_RANDOM
    OFF
    CACHE BOOL "")

add_subdirectory(${CMAKE_CURRENT_LIST_DIR}/../extrachain-3rdparty/bip3x
                 ${CMAKE_CURRENT_BINARY_DIR}/bip3x)

target_include_directories(
  extrachain-core
  PUBLIC ${CMAKE_CURRENT_LIST_DIR}/../extrachain-3rdparty/bip3x/include)

target_link_libraries(
  extrachain-core
  unofficial-sodium::sodium
  unofficial::sqlite3::sqlite3
  msgpack-cxx
  fmt::fmt-header-only
  BLAKE3::blake3
  magic_enum::magic_enum
  Boost::boost
  Boost::system
  Boost::thread
  Boost::mp11
  Boost::describe
  Boost::json
  bip3x
  extrachain-wamr
  $<IF:$<TARGET_EXISTS:zstd::libzstd_shared>,zstd::libzstd_shared,zstd::libzstd_static>
  ZLIB::ZLIB)

if(EXTRACHAIN_BUILD_QT_COMPAT)
  set(EXTRACHAIN_QT_SOURCES
      ${CMAKE_CURRENT_LIST_DIR}/headers/managers/extrachain_node.h
      ${CMAKE_CURRENT_LIST_DIR}/headers/network/network_manager.h
      ${CMAKE_CURRENT_LIST_DIR}/headers/dfs/dfs_controller.h
      ${CMAKE_CURRENT_LIST_DIR}/headers/adapters/qt/actor_id_adapter.h
      ${CMAKE_CURRENT_LIST_DIR}/headers/adapters/qt/bignumber_adapter.h
      ${CMAKE_CURRENT_LIST_DIR}/headers/adapters/qt/byte_array_adapter.h
      ${CMAKE_CURRENT_LIST_DIR}/headers/adapters/qt/logging_adapter.h
      ${CMAKE_CURRENT_LIST_DIR}/headers/adapters/qt/network_status_adapter.h
      ${CMAKE_CURRENT_LIST_DIR}/headers/adapters/qt/network_manager_adapter.h
      ${CMAKE_CURRENT_LIST_DIR}/headers/adapters/qt/qt_compat_global.h
      ${CMAKE_CURRENT_LIST_DIR}/headers/adapters/qt/thread_pool.h
      ${CMAKE_CURRENT_LIST_DIR}/headers/adapters/qt/utils_adapter.h
      ${CMAKE_CURRENT_LIST_DIR}/headers/adapters/qt/toolchain_installer.h
      ${CMAKE_CURRENT_LIST_DIR}/headers/metatypes.h
      ${CMAKE_CURRENT_LIST_DIR}/headers/managers/thread_pool.h
      ${CMAKE_CURRENT_LIST_DIR}/headers/utils/variant_model.h
      ${CMAKE_CURRENT_LIST_DIR}/sources/managers/extrachain_node.cpp
      ${CMAKE_CURRENT_LIST_DIR}/sources/network/network_manager.cpp
      ${CMAKE_CURRENT_LIST_DIR}/sources/dfs/dfs_controller.cpp
      ${CMAKE_CURRENT_LIST_DIR}/sources/adapters/qt/actor_id_adapter.cpp
      ${CMAKE_CURRENT_LIST_DIR}/sources/adapters/qt/byte_array_adapter.cpp
      ${CMAKE_CURRENT_LIST_DIR}/sources/adapters/qt/network_status_adapter.cpp
      ${CMAKE_CURRENT_LIST_DIR}/sources/adapters/qt/network_manager_adapter.cpp
      ${CMAKE_CURRENT_LIST_DIR}/sources/adapters/qt/utils_adapter.cpp
      ${CMAKE_CURRENT_LIST_DIR}/sources/contracts/toolchain_installer.cpp
      ${CMAKE_CURRENT_LIST_DIR}/sources/utils/variant_model.cpp)
  if(EXTRACHAIN_STATIC_BUILD)
    add_library(extrachain STATIC ${EXTRACHAIN_QT_SOURCES})
  else()
    add_library(extrachain SHARED ${EXTRACHAIN_QT_SOURCES})
    target_compile_definitions(
      extrachain
      PUBLIC EXTRACHAIN_QT_LIBRARY_ACTIVATE
      PRIVATE EXTRACHAIN_QT_LIBRARY)
  endif()
  set_target_properties(
    extrachain
    PROPERTIES AUTOMOC ON
               AUTOUIC ON
               AUTORCC ON)
  target_include_directories(extrachain PUBLIC ${EXTRACHAIN_CORE_INCLUDES})
  # Keep the plain signature for compatibility with existing client projects
  # that extend this target with the same CMake signature.
  target_link_libraries(extrachain extrachain-core Qt6::Core Qt6::Network
                        fmt::fmt-header-only)
  target_compile_features(extrachain PUBLIC cxx_std_23)
  set_property(TARGET extrachain PROPERTY POSITION_INDEPENDENT_CODE ON)

  add_library(extrachain-qt-compat INTERFACE)
  target_link_libraries(extrachain-qt-compat INTERFACE extrachain)
else()
  add_library(extrachain INTERFACE)
  target_link_libraries(extrachain INTERFACE extrachain-core)
endif()

if(UNIX AND NOT APPLE)
  # WAMR contains an assembly object without a GNU-stack note. Apply the
  # explicit non-executable-stack policy to every final executable that links
  # the static Core library.
  target_link_options(extrachain-core PUBLIC "-Wl,-z,noexecstack")
endif()
# C FFI bindings
if(EXTRACHAIN_BUILD_CBINDINGS)
  add_subdirectory(Cbindings)
endif()

set_property(TARGET extrachain-core PROPERTY POSITION_INDEPENDENT_CODE ON)
