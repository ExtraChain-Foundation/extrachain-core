cmake_minimum_required(VERSION 3.21)

project(extrachain-contract-tests LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Qt6 COMPONENTS Core REQUIRED)
include(${CMAKE_CURRENT_LIST_DIR}/../../cmake/ExtraChainWamr.cmake)

add_executable(extrachain-contract-tests
    contract_runtime_test.cpp
    ../../sources/contracts/contract_codec.cpp
    ../../sources/contracts/contract_manager.cpp
    ../../sources/contracts/wasm_runtime.cpp)
target_include_directories(extrachain-contract-tests PRIVATE
    ../../headers
    ../../../extrachain-3rdparty/msgpack/include)
target_link_libraries(extrachain-contract-tests PRIVATE Qt6::Core extrachain-wamr)
if(UNIX AND NOT APPLE)
    target_link_options(extrachain-contract-tests PRIVATE "-Wl,-z,noexecstack")
endif()

enable_testing()
add_test(NAME contract-runtime
    COMMAND extrachain-contract-tests
        ${CMAKE_CURRENT_LIST_DIR}/../../contracts/standard/fungible_token.wasm
        ${CMAKE_CURRENT_LIST_DIR}/../../contracts/standard/message_claim.wasm)
