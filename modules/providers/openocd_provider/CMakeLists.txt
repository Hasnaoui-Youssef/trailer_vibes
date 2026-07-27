# OpenOcdProvider links the openocd static library in-process (see
# external/openocd/src/CMakeLists.txt - openocd.c/main.c are excluded from
# that build, this module is the entry point instead). openocd headers are
# never exposed past src/openocd_c_api.hpp; consumers only ever see the
# public API in include/openocd_provider.

add_library(openocd_provider
        src/openocd_exit.c
        src/openocd_runtime.cc
        src/command_queue.cc
        src/openocd_provider.cc
)

target_include_directories(openocd_provider PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(openocd_provider PRIVATE
    openocd
)

# openocd_exit.c defines the symbol external/openocd/cmake/openocd_exit.h
# force-includes into every OpenOCD translation unit; pull that declaration
# in directly so the compiler checks the definition matches.
target_include_directories(openocd_provider PRIVATE
    ${CMAKE_SOURCE_DIR}/external/openocd/cmake
)

add_dependencies(openocd_provider openocd_startup_tcl)

target_compile_features(openocd_provider PUBLIC cxx_std_23)

add_subdirectory(test)
