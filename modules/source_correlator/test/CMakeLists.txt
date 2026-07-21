add_executable(source_correlator_test
        source_correlator_test.cc
)

target_link_libraries(source_correlator_test PRIVATE
    source_correlator
)

target_compile_definitions(source_correlator_test PRIVATE
    TRAILER_SOURCE_CORRELATOR_FIXTURE_PATH="${CMAKE_CURRENT_SOURCE_DIR}/fixture.elf"
)

target_compile_features(source_correlator_test PRIVATE cxx_std_20)
