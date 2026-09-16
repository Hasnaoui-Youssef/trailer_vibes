add_library(trace_exception_attribution
        src/exception_attribution.cc
)

target_include_directories(trace_exception_attribution PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(trace_exception_attribution PUBLIC
    trace_model
    device_xml
)

target_compile_features(trace_exception_attribution PRIVATE cxx_std_23)

if(TRAILER_BUILD_TESTS)
    add_subdirectory(test)
endif()
