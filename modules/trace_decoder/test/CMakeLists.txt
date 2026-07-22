add_executable(trace_decoder_test
        trace_decoder_test.cc
)

target_link_libraries(trace_decoder_test PRIVATE
    trace_decoder
    config_parser
    trace_sink
)

target_compile_definitions(trace_decoder_test PRIVATE
    TRAILER_SAMPLE_CONFIG_PATH="${CMAKE_SOURCE_DIR}/test_resources/sample_config.json"
)

target_compile_features(trace_decoder_test PRIVATE cxx_std_23)
