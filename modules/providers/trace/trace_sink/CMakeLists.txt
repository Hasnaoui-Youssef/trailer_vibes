add_library(trace_sink
        src/trace_record_sink.cc
)

target_include_directories(trace_sink PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(trace_sink PRIVATE
    OpenCSD::opencsd
)

target_compile_features(trace_sink PRIVATE cxx_std_20)

if(TRAILER_BUILD_TESTS)
    add_subdirectory(test)
endif()
