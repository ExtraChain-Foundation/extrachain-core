cmake_minimum_required(VERSION 3.21)

project(extrachain-contract-tests LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Boost CONFIG REQUIRED COMPONENTS describe json)
find_package(msgpack-cxx CONFIG REQUIRED)
find_package(blake3 CONFIG REQUIRED)
find_package(unofficial-sodium CONFIG REQUIRED)
include(${CMAKE_CURRENT_LIST_DIR}/../../cmake/ExtraChainWamr.cmake)
include(${CMAKE_CURRENT_LIST_DIR}/../../cmake/ExtraChainEmbeddedContracts.cmake)
extrachain_generate_embedded_contracts(
    ${CMAKE_CURRENT_BINARY_DIR}/generated
    ${CMAKE_CURRENT_LIST_DIR}/../../contracts/standard)

add_executable(extrachain-contract-tests
    contract_runtime_test.cpp
    ../../sources/contracts/contract_codec.cpp
    ../../sources/contracts/contract_manager.cpp
    ../../sources/contracts/contract_module.cpp
    ../../sources/contracts/standard_token.cpp
    ../../sources/contracts/wasm_policy.cpp
    ../../sources/contracts/wasm_runtime.cpp)
target_include_directories(extrachain-contract-tests PRIVATE
    ../../headers
    ${CMAKE_CURRENT_BINARY_DIR}/generated
    ../../../extrachain-3rdparty/msgpack/include)
target_link_libraries(extrachain-contract-tests PRIVATE
    Boost::describe Boost::json msgpack-cxx BLAKE3::blake3 unofficial-sodium::sodium extrachain-wamr)
if(UNIX AND NOT APPLE)
    target_link_options(extrachain-contract-tests PRIVATE "-Wl,-z,noexecstack")
endif()

enable_testing()
add_test(NAME contract-runtime
    COMMAND extrachain-contract-tests
        ${CMAKE_CURRENT_LIST_DIR}/../../contracts/standard/fungible_token_rust.wasm
        ${CMAKE_CURRENT_LIST_DIR}/../../contracts/standard/fungible_token_assemblyscript.wasm
        ${CMAKE_CURRENT_LIST_DIR}/../../contracts/standard/non_fungible_token_rust.wasm
        ${CMAKE_CURRENT_LIST_DIR}/../../contracts/standard/non_fungible_token_assemblyscript.wasm
        ${CMAKE_CURRENT_LIST_DIR}/../../contracts/standard/message_claim.wasm
        ${CMAKE_CURRENT_LIST_DIR}/../../contracts/standard/assemblyscript_basic.wasm
        ${CMAKE_CURRENT_LIST_DIR}/../../contracts/standard/assemblyscript_dfs_binding.wasm)
