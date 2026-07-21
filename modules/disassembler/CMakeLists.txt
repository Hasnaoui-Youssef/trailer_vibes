add_library(disassembler
        src/program_disassembler.cc
)

target_include_directories(disassembler PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

# LLVM headers are a private implementation detail: nothing in this
# module's public interface (program_disassembler.hpp) names an LLVM type,
# so consumers never need this include path.
target_include_directories(disassembler PRIVATE
    ${llvm_SOURCE_DIR}/llvm/include
    ${llvm_BINARY_DIR}/include
)

target_link_libraries(disassembler
    PUBLIC
        trace_model
    PRIVATE
        LLVMObject
        LLVMBinaryFormat
        LLVMMC
        LLVMMCParser
        LLVMMCDisassembler
        LLVMCodeGenTypes
        LLVMSupport
        LLVMDemangle
        LLVMTargetParser
        LLVMARMDesc
        LLVMARMInfo
        LLVMARMDisassembler
        LLVMARMUtils
)

# This is the RTTI boundary: LLVM is built with its default -fno-rtti, but
# OpenCSD requires RTTI (dynamic_cast + inheritance). This module is the
# only one that includes LLVM headers or links LLVM libraries, and it must
# never be linked into a translation unit that also touches OpenCSD.
target_compile_options(disassembler PRIVATE
    $<$<COMPILE_LANGUAGE:CXX>:-fno-rtti>
)

target_compile_features(disassembler PRIVATE cxx_std_20)

add_subdirectory(test)
