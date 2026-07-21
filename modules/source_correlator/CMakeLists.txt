add_library(source_correlator
        src/source_correlator.cc
)

target_include_directories(source_correlator PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

# LLVM headers are a private implementation detail: nothing in this
# module's public interface (source_correlator.hpp) names an LLVM type, so
# consumers never need this include path.
target_include_directories(source_correlator PRIVATE
    ${llvm_SOURCE_DIR}/llvm/include
    ${llvm_BINARY_DIR}/include
)

target_link_libraries(source_correlator
    PUBLIC
        trace_model
    PRIVATE
        LLVMDebugInfoDWARF
        LLVMDebugInfoDWARFLowLevel
        LLVMObject
        LLVMBitReader
        LLVMCore
        LLVMIRReader
        LLVMMC
        LLVMMCParser
        LLVMBinaryFormat
        LLVMTextAPI
        LLVMTargetParser
        LLVMSupport
        LLVMDemangle
)

# Same RTTI boundary as modules/disassembler: LLVM's DWARFContext is
# reached here, and this must never be linked into a translation unit that
# also touches OpenCSD (which requires RTTI).
target_compile_options(source_correlator PRIVATE
    $<$<COMPILE_LANGUAGE:CXX>:-fno-rtti>
)

target_compile_features(source_correlator PRIVATE cxx_std_20)

add_subdirectory(test)
