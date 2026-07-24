# Header-only: LldbProvider is two SB API value members plus one trivial
# inline accessor, so there's nothing to put in a .cc.
add_library(lldb_provider INTERFACE)

target_include_directories(lldb_provider INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

# Public: this module's own public header names lldb:: SB API types
# directly, so consumers need these include paths too - same rationale as
# modules/dap's dap_core target.
target_include_directories(lldb_provider INTERFACE
    ${LLVM_INCLUDE_DIRS}
    ${LLDB_INCLUDE_DIR}
)

target_link_libraries(lldb_provider INTERFACE
    LLDB::liblldb
)

# Same RTTI boundary discipline as modules/disassembler and modules/dap:
# LLVM/LLDB are built -fno-rtti, and this module links liblldb directly, so
# it must never be linked into a translation unit that also touches OpenCSD
# (which requires RTTI).
target_compile_options(lldb_provider INTERFACE
    $<$<COMPILE_LANGUAGE:CXX>:-fno-rtti>
)

target_compile_features(lldb_provider INTERFACE cxx_std_23)
