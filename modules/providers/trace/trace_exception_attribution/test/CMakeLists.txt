add_executable(exception_attribution_test
        exception_attribution_test.cc
)

target_link_libraries(exception_attribution_test PRIVATE
    trace_exception_attribution
)

target_compile_features(exception_attribution_test PRIVATE cxx_std_23)
