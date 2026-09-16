cmake_minimum_required(VERSION 3.21)
project(trailer VERSION 0.1.0 LANGUAGES C CXX)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE "Debug" CACHE STRING "Build Type" FORCE)
endif()

set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(TRAILER_BUILD_TESTS "Build engine test executables" ON)

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
set(PUGIXML_INSTALL OFF CACHE BOOL "" FORCE)

FetchContent_Declare(
    nlohmann_json
    GIT_REPOSITORY https://github.com/nlohmann/json
    GIT_TAG v3.11.3
)
FetchContent_Declare(
    pugixml
    GIT_REPOSITORY https://github.com/zeux/pugixml.git
    GIT_TAG v1.16
)

FetchContent_MakeAvailable(opencsd)
FetchContent_MakeAvailable(nlohmann_json)
FetchContent_MakeAvailable(pugixml)

# The committed CMSIS_SVD bindings pin LIBXSD_VERSION to 4.2.0 exactly; a
# system libxsd of any other version fails to compile rather than misbehaving.
find_path(XSD_SYSTEM_INCLUDE_DIR NAMES xsd/cxx/config.hxx)
if(XSD_SYSTEM_INCLUDE_DIR)
    set(XSD_INCLUDE_DIR "${XSD_SYSTEM_INCLUDE_DIR}")
else()
    FetchContent_Declare(
        libxsd
        URL https://www.codesynthesis.com/download/xsd/4.2/libxsd-4.2.0.tar.gz
        URL_HASH SHA256=55caf0038603883eb39ac4caeaacda23a09cf81cffc8eb55a854b6b06ef2c52e
    )
    FetchContent_MakeAvailable(libxsd)
    set(XSD_INCLUDE_DIR "${libxsd_SOURCE_DIR}")
endif()
message(STATUS "Using libxsd headers from ${XSD_INCLUDE_DIR}")

add_subdirectory(external/openocd)
add_subdirectory(modules/common)
add_subdirectory(modules/providers)
add_subdirectory(modules/dap)
add_subdirectory(modules/core)

add_executable(trailer-dap
        src/dap_main.cc
)

target_link_libraries(trailer-dap
    PRIVATE
    dap_core
    dap_handlers
)

add_executable(trailer-trace-replay
        src/trailer_trace_replay.cc
)

target_link_libraries(trailer-trace-replay
    PRIVATE
    disassembler
    trace_decoder
    trace_transform
    trace_exception_attribution
    device_xml
    nlohmann_json::nlohmann_json
)

target_compile_definitions(trailer-trace-replay PRIVATE
    TRAILER_SAMPLE_ELF_PATH="${CMAKE_CURRENT_SOURCE_DIR}/test_resources/firmware.elf"
    TRAILER_SAMPLE_TRACE_DUMP_PATH="${CMAKE_CURRENT_SOURCE_DIR}/test_resources/etm_dump.bin"
)

target_compile_features(trailer-trace-replay PRIVATE cxx_std_23)

set(TRAILER_RESOURCE_INDEX "${CMAKE_CURRENT_SOURCE_DIR}/resources/device_index.tsv")
set(TRAILER_RESOURCE_CORES "${CMAKE_CURRENT_SOURCE_DIR}/resources/XML/Cores")
set(TRAILER_RESOURCE_RZONE "${CMAKE_CURRENT_SOURCE_DIR}/resources/XML/Rzone")
set(TRAILER_OPENOCD_SCRIPTS "${CMAKE_CURRENT_SOURCE_DIR}/external/openocd/tcl")

# device_provider resolves its resources at runtime relative to the
# executable (see resource_paths.cc), so a shipped binary isn't tied to
# CMAKE_SOURCE_DIR. Only what's actually read at runtime is copied - not
# Schemas/ (codegen-time only) or XML/Devices/ (not committed; folded into
# device_index.tsv by tools/expand_devices.py).
add_custom_command(TARGET trailer-dap POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory
        $<TARGET_FILE_DIR:trailer-dap>/resources/XML
    COMMAND ${CMAKE_COMMAND} -E copy
        ${TRAILER_RESOURCE_INDEX}
        $<TARGET_FILE_DIR:trailer-dap>/resources/device_index.tsv
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${TRAILER_RESOURCE_CORES}
        $<TARGET_FILE_DIR:trailer-dap>/resources/XML/Cores
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${TRAILER_RESOURCE_RZONE}
        $<TARGET_FILE_DIR:trailer-dap>/resources/XML/Rzone
    COMMENT "Copying device resources next to trailer-dap"
)

target_compile_features(trailer-dap PRIVATE cxx_std_23)

# OpenCSD installs headers and static libs unconditionally, so everything
# shipped is scoped to one component and only that component is packaged.
if(WIN32)
    get_filename_component(TRAILER_TOOLCHAIN_BIN "${CMAKE_CXX_COMPILER}" DIRECTORY)
    install(TARGETS trailer-dap
        RUNTIME_DEPENDENCY_SET trailer_runtime_deps
        DESTINATION .
        COMPONENT runtime
    )
    install(RUNTIME_DEPENDENCY_SET trailer_runtime_deps
        PRE_EXCLUDE_REGEXES "api-ms-" "ext-ms-"
        POST_EXCLUDE_REGEXES ".*[Ss]ystem32.*" ".*[Ww]inSxS.*"
        DIRECTORIES "${TRAILER_TOOLCHAIN_BIN}"
        DESTINATION .
        COMPONENT runtime
    )

    # liblldb initialises Python eagerly, and the standard library is data
    # rather than an import, so the dependency walk above cannot find it.
    file(GLOB TRAILER_PYTHON_STDLIB "${TRAILER_TOOLCHAIN_BIN}/../lib/python3.*")
    if(TRAILER_PYTHON_STDLIB)
        list(GET TRAILER_PYTHON_STDLIB 0 TRAILER_PYTHON_STDLIB)
        install(DIRECTORY "${TRAILER_PYTHON_STDLIB}"
            DESTINATION lib
            COMPONENT runtime
            PATTERN "test" EXCLUDE
            PATTERN "idlelib" EXCLUDE
            PATTERN "ensurepip" EXCLUDE
            PATTERN "pydoc_data" EXCLUDE
            PATTERN "__pycache__" EXCLUDE
        )
    else()
        message(WARNING "Python standard library not found next to ${TRAILER_TOOLCHAIN_BIN}; "
                        "the packaged trailer-dap will abort at startup if liblldb needs it.")
    endif()
else()
    install(TARGETS trailer-dap DESTINATION . COMPONENT runtime)
endif()

install(FILES ${TRAILER_RESOURCE_INDEX} DESTINATION resources COMPONENT runtime)
install(DIRECTORY ${TRAILER_RESOURCE_CORES} ${TRAILER_RESOURCE_RZONE}
    DESTINATION resources/XML COMPONENT runtime)
install(DIRECTORY ${TRAILER_OPENOCD_SCRIPTS}/ DESTINATION openocd/scripts COMPONENT runtime)

set(CPACK_PACKAGE_NAME "Trailer")
set(CPACK_PACKAGE_VENDOR "Trailer")
set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Embedded systems debug engine")
set(CPACK_PACKAGE_INSTALL_DIRECTORY "Trailer")
set(CPACK_VERBATIM_VARIABLES ON)
set(CPACK_COMPONENTS_ALL runtime)
set(CPACK_COMPONENTS_GROUPING ALL_COMPONENTS_IN_ONE)
# Without per-generator component mode the packages are built monolithically
# and pick up the dependencies' own install rules regardless of CPACK_COMPONENTS_ALL.
set(CPACK_ARCHIVE_COMPONENT_INSTALL ON)
set(CPACK_NSIS_COMPONENT_INSTALL ON)

if(WIN32)
    set(CPACK_GENERATOR "NSIS;ZIP")
    set(CPACK_PACKAGE_FILE_NAME "Trailer-${PROJECT_VERSION}-win64")
    set(CPACK_NSIS_PACKAGE_NAME "Trailer")
    set(CPACK_NSIS_DISPLAY_NAME "Trailer Debug Engine")
    set(CPACK_NSIS_MODIFY_PATH OFF)
    set(CPACK_NSIS_ENABLE_UNINSTALL_BEFORE_INSTALL ON)
    set(CPACK_NSIS_INSTALLED_ICON_NAME "trailer-dap.exe")
    set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE")
else()
    set(CPACK_GENERATOR "TGZ")
endif()
include(CPack)
