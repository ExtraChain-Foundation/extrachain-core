cmake_minimum_required(VERSION 3.21)

file(STRINGS "../extrachain_version" EXTRACHAIN_VERSION)
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
