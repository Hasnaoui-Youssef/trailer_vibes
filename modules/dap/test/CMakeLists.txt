add_executable(orchestrator_cancel_test
        orchestrator_cancel_test.cc
)

target_link_libraries(orchestrator_cancel_test PRIVATE
    dap_core
)

target_compile_features(orchestrator_cancel_test PRIVATE cxx_std_23)
