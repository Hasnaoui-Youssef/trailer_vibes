add_library(trace_decoder
        src/decode_tree_builder.cc
)

target_include_directories(trace_decoder PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(trace_decoder PUBLIC
    config_parser
    trace_sink
    OpenCSD::opencsd
)

target_compile_features(trace_decoder PRIVATE cxx_std_20)

add_subdirectory(test)
