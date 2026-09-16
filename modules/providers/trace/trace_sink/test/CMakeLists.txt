add_executable(trace_record_sink_test
        trace_record_sink_test.cc
)

target_link_libraries(trace_record_sink_test PRIVATE
    trace_sink
    OpenCSD::opencsd
)

target_compile_features(trace_record_sink_test PRIVATE cxx_std_23)
