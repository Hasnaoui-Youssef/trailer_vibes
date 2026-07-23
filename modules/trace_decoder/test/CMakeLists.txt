add_executable(trace_decoder_test
        trace_decoder_test.cc
)

target_link_libraries(trace_decoder_test PRIVATE
    trace_decoder
    trace_sink
)

target_compile_definitions(trace_decoder_test PRIVATE
    TRAILER_SAMPLE_ELF_PATH="${CMAKE_SOURCE_DIR}/test_resources/firmware.elf"
    TRAILER_SAMPLE_TRACE_DUMP_PATH="${CMAKE_SOURCE_DIR}/test_resources/etm_dump.bin"
)

target_compile_features(trace_decoder_test PRIVATE cxx_std_23)
