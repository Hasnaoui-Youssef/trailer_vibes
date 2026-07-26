cmake_minimum_required(VERSION 3.20)
project(trailer LANGUAGES C CXX)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE "Debug" CACHE STRING "Build Type" FORCE)
endif()

set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

include(FetchContent)

# See INSTALL.md for prerequisites and CMakePresets.json for per-platform
# LLVM_DIR setup.
set(TRAILER_LLVM_VERSION 22 CACHE STRING "LLVM major version to build against")
find_package(LLVM CONFIG REQUIRED)
if(NOT LLVM_VERSION_MAJOR VERSION_EQUAL TRAILER_LLVM_VERSION)
    message(FATAL_ERROR "Found LLVM ${LLVM_VERSION_MAJOR} at ${LLVM_DIR}, expected ${TRAILER_LLVM_VERSION}.")
endif()
message(STATUS "Using LLVM ${LLVM_PACKAGE_VERSION} from ${LLVM_DIR}")

# LLDB ships no CMake package config; located manually alongside LLVM.
find_path(LLDB_INCLUDE_DIR NAMES lldb/API/SBDebugger.h HINTS ${LLVM_INCLUDE_DIRS})
find_library(LLDB_LIBRARY NAMES lldb liblldb HINTS ${LLVM_LIBRARY_DIRS})
if(NOT LLDB_INCLUDE_DIR OR NOT LLDB_LIBRARY)
    message(FATAL_ERROR "LLDB SB API headers/library not found alongside LLVM "
                         "(looked under ${LLVM_INCLUDE_DIRS} / ${LLVM_LIBRARY_DIRS}). "
                         "Install the LLDB dev package (e.g. Ubuntu: liblldb-<N>-dev).")
endif()
message(STATUS "Using LLDB library ${LLDB_LIBRARY}, headers ${LLDB_INCLUDE_DIR}")

add_library(LLDB::liblldb UNKNOWN IMPORTED)
set_target_properties(LLDB::liblldb PROPERTIES
    IMPORTED_LOCATION "${LLDB_LIBRARY}"
    INTERFACE_INCLUDE_DIRECTORIES "${LLDB_INCLUDE_DIR}"
)

set(OPENCSD_BUILD_MIN_LIB_STATIC ON CACHE BOOL "" FORCE)

set(FETCHCONTENT_QUIET OFF)
set(FETCHCONTENT_FULLY_DISCONNECTED OFF)
set(FETCHCONTENT_USES_TERMINAL_BUILD TRUE)

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

FetchContent_MakeAvailable(opencsd)
FetchContent_MakeAvailable(nlohmann_json)

add_subdirectory(modules/common)
add_subdirectory(modules/providers)
add_subdirectory(modules/dap)
add_subdirectory(modules/core)

add_executable(trailer
        src/main.cc
)

# RTTI boundary: see modules/disassembler/CMakeLists.txt.
target_link_libraries(trailer
    PRIVATE
    OpenCSD::opencsd
    snapshot_parser
    trace_sink
    trace_decoder
    trace_model
    disassembler
    trace_transform
)

target_compile_definitions(trailer PRIVATE
    TRAILER_SAMPLE_ELF_PATH="${CMAKE_SOURCE_DIR}/test_resources/firmware.elf"
    TRAILER_SAMPLE_TRACE_DUMP_PATH="${CMAKE_SOURCE_DIR}/test_resources/etm_dump.bin"
)

target_compile_features(trailer PRIVATE cxx_std_23)

add_executable(trailer-dap
        src/dap_main.cc
)

target_link_libraries(trailer-dap
    PRIVATE
    dap_core
    dap_handlers
)

target_compile_features(trailer-dap PRIVATE cxx_std_23)
