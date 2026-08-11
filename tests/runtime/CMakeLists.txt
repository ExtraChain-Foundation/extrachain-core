cmake_minimum_required(VERSION 3.21)

project(extrachain-runtime-tests LANGUAGES CXX)

find_package(Boost CONFIG REQUIRED COMPONENTS system thread signals2)

add_executable(extrachain-runtime-tests
    runtime_test.cpp
    ../../sources/network/network_runtime.cpp
    ../../sources/runtime/deadline_task.cpp
    ../../sources/runtime/periodic_task.cpp
    ../../sources/runtime/runtime.cpp
)
target_include_directories(extrachain-runtime-tests PRIVATE ../../headers)
target_link_libraries(extrachain-runtime-tests PRIVATE Boost::boost Boost::system Boost::thread)
target_compile_features(extrachain-runtime-tests PRIVATE cxx_std_23)
if(WIN32)
    target_compile_definitions(extrachain-runtime-tests PRIVATE _WIN32_WINNT=0x0A00)
endif()

enable_testing()
add_test(NAME runtime COMMAND extrachain-runtime-tests)
