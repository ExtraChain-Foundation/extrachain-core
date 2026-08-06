set(WAMR_ROOT_DIR "${CMAKE_CURRENT_LIST_DIR}/../../extrachain-3rdparty/WAMR")

if(NOT EXISTS "${WAMR_ROOT_DIR}/core/iwasm/include/wasm_export.h")
    message(FATAL_ERROR "WAMR 2.4.5 is missing from extrachain-3rdparty")
endif()

if(ANDROID)
    set(WAMR_BUILD_PLATFORM android)
elseif(WIN32)
    set(WAMR_BUILD_PLATFORM windows)
elseif(APPLE)
    set(WAMR_BUILD_PLATFORM darwin)
else()
    set(WAMR_BUILD_PLATFORM linux)
endif()

set(WAMR_BUILD_INTERP 1)
set(WAMR_BUILD_FAST_INTERP 0)
set(WAMR_BUILD_INSTRUCTION_METERING 1)
set(WAMR_CONFIGURABLE_BOUNDS_CHECKS 1)
set(WAMR_BUILD_AOT 0)
set(WAMR_BUILD_JIT 0)
set(WAMR_BUILD_FAST_JIT 0)
set(WAMR_BUILD_LIBC_BUILTIN 0)
set(WAMR_BUILD_LIBC_WASI 0)
set(WAMR_BUILD_LIBC_UVWASI 0)
set(WAMR_BUILD_MULTI_MODULE 0)
set(WAMR_BUILD_LIB_PTHREAD 0)
set(WAMR_BUILD_LIB_WASI_THREADS 0)
set(WAMR_BUILD_SHARED_MEMORY 0)
set(WAMR_BUILD_BULK_MEMORY 0)
set(WAMR_BUILD_REF_TYPES 0)
set(WAMR_BUILD_SIMD 0)
set(WAMR_BUILD_GC 0)

include("${WAMR_ROOT_DIR}/build-scripts/runtime_lib.cmake")

add_library(extrachain-wamr STATIC ${WAMR_RUNTIME_LIB_SOURCE})
target_include_directories(extrachain-wamr PUBLIC "${WAMR_ROOT_DIR}/core/iwasm/include")
set_target_properties(extrachain-wamr PROPERTIES
    C_STANDARD 99
    C_STANDARD_REQUIRED ON
    POSITION_INDEPENDENT_CODE ON)

find_package(Threads REQUIRED)
target_link_libraries(extrachain-wamr PUBLIC Threads::Threads)

if(UNIX AND NOT APPLE)
    target_link_libraries(extrachain-wamr PUBLIC m dl)
endif()
