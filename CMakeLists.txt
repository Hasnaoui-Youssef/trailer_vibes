cmake_minimum_required(VERSION 3.20)
project(trailer LANGUAGES CXX)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE "Debug" CACHE STRING "Build Type" FORCE)
endif()

set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

include(FetchContent)

set(LLVM_TARGETS_TO_BUILD "X86;ARM;AArch64" CACHE STRING "" FORCE)
set(LLVM_ENABLE_PROJECTS "" CACHE STRING "" FORCE)
set(LLVM_BUILD_TOOLS OFF CACHE BOOL "" FORCE)
set(LLVM_BUILD_UTILS OFF CACHE BOOL "" FORCE)
set(LLVM_INCLUDE_TESTS OFF CACHE BOOL "" FORCE)
set(LLVM_INCLUDE_EXAMPLES OFF CACHE BOOL "" FORCE)
set(LLVM_INCLUDE_BENCHMARKS OFF CACHE BOOL "" FORCE)
set(LLVM_ENABLE_ZLIB OFF CACHE BOOL "" FORCE)
set(LLVM_ENABLE_ZSTD OFF CACHE BOOL "" FORCE)
set(LLVM_ENABLE_TERMINFO OFF CACHE BOOL "" FORCE)
set(LLVM_APPEND_VC_REV OFF CACHE BOOL "" FORCE)

set(OPENCSD_BUILD_MIN_LIB_STATIC ON CACHE BOOL "" FORCE)

set(FETCHCONTENT_QUIET OFF)
set(FETCHCONTENT_FULLY_DISCONNECTED OFF)
set(FETCHCONTENT_USES_TERMINAL_BUILD TRUE)

FetchContent_Declare(
    llvm
    URL https://github.com/llvm/llvm-project/releases/download/llvmorg-22.1.8/llvm-project-22.1.8.src.tar.xz
    SOURCE_SUBDIR llvm
    EXCLUDE_FROM_ALL
)

FetchContent_Declare(
    opencsd
    GIT_REPOSITORY https://github.com/Linaro/OpenCSD
    GIT_TAG v1.8.3
)

set(JSON_BuildTests OFF CACHE INTERNAL "")
set(JSON_Install OFF CACHE BOOL "" FORCE)

FetchContent_Declare(
    nlohmann_json
    GIT_REPOSITORY https://github.com/nlohmann/json
    GIT_TAG v3.11.3
)

FetchContent_MakeAvailable(llvm)
FetchContent_MakeAvailable(opencsd)
FetchContent_MakeAvailable(nlohmann_json)

add_subdirectory(modules/snapshot_parser)
add_subdirectory(modules/trace_sink)
add_subdirectory(modules/config_parser)
add_subdirectory(modules/trace_decoder)

add_executable(trailer
        src/main.cc
)

target_compile_options(trailer
    PRIVATE
    $<$<COMPILE_LANGUAGE:CXX>:-fno-rtti>
)

target_include_directories(trailer
    PRIVATE
        ${llvm_SOURCE_DIR}/llvm/include
        ${llvm_BINARY_DIR}/include
)
target_link_libraries(trailer
    PRIVATE
    OpenCSD::opencsd
    snapshot_parser
    trace_sink
    config_parser
    trace_decoder
    LLVMDebugInfoDWARF
    LLVMObject
    LLVMBinaryFormat
    LLVMMC
    LLVMMCParser
    LLVMSupport
    LLVMDemangle
    LLVMTargetParser
    LLVMMCDisassembler
    LLVMSymbolize
    LLVMARMDesc
    LLVMARMInfo
)
target_compile_features(trailer PRIVATE cxx_std_20)

