add_library(common
        src/output_redirector.cc
        src/fifo_files.cc
        src/client_launcher.cc
)

target_include_directories(common PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${LLVM_INCLUDE_DIRS}
)

# OS-level utilities only (pipes, FIFOs, process spawn) - no LLDB, no DAP
# protocol knowledge. The lowest-level module: everything else may depend on
# this, it depends on nothing in this tree. Public headers name llvm::
# types directly (StringRef, Error), hence LLVM include dirs/link are PUBLIC.
target_link_libraries(common PUBLIC
    LLVMSupport
)

target_compile_features(common PUBLIC cxx_std_23)
