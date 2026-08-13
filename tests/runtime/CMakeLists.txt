cmake_minimum_required(VERSION 3.21)

project(extrachain-runtime-tests LANGUAGES C CXX)

set(EXTRACHAIN_BUILD_QT_COMPAT OFF CACHE BOOL "" FORCE)
set(EXTRACHAIN_BUILD_LEGACY_QT_NODE OFF CACHE BOOL "" FORCE)
set(EXTRACHAIN_BUILD_CBINDINGS OFF CACHE BOOL "" FORCE)
set(EXTRACHAIN_STATIC_BUILD ON CACHE BOOL "" FORCE)
include(../../CMakeLists.txt)

add_executable(extrachain-runtime-tests runtime_test.cpp)
target_link_libraries(extrachain-runtime-tests PRIVATE extrachain-core)

enable_testing()
add_test(NAME runtime COMMAND extrachain-runtime-tests)
