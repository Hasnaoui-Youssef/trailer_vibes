add_executable(decode_tree_builder_smoke
        decode_tree_builder_smoke.cc
)

target_link_libraries(decode_tree_builder_smoke PRIVATE
    trace_decoder
    config_parser
    trace_sink
)

target_compile_definitions(decode_tree_builder_smoke PRIVATE
    TRAILER_SAMPLE_CONFIG_PATH="${CMAKE_SOURCE_DIR}/test_resources/sample_config.json"
)

target_compile_features(decode_tree_builder_smoke PRIVATE cxx_std_20)
