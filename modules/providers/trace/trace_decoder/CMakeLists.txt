add_library(trace_decoder
        src/trace_decoder.cc
)

target_include_directories(trace_decoder PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(trace_decoder PUBLIC
    trace_sink
    trace_model
    OpenCSD::opencsd
)

target_compile_features(trace_decoder PRIVATE cxx_std_23)

add_subdirectory(test)
