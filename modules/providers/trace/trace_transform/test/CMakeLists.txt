add_executable(transform_test
        transform_test.cc
)

target_link_libraries(transform_test PRIVATE
    trace_transform
)

target_compile_features(transform_test PRIVATE cxx_std_23)
