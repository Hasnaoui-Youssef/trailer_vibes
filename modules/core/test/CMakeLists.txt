add_executable(event_bus_test
        event_bus_test.cc
)

target_link_libraries(event_bus_test PRIVATE
    core
)

target_compile_features(event_bus_test PRIVATE cxx_std_23)
