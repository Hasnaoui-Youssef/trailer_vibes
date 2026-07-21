add_library(instr_reconstruct
        src/reconstruct.cc
)

target_include_directories(instr_reconstruct PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(instr_reconstruct PUBLIC
    trace_sink
    disassembler
    trace_model
    OpenCSD::opencsd
)

target_compile_features(instr_reconstruct PRIVATE cxx_std_20)

add_subdirectory(test)
