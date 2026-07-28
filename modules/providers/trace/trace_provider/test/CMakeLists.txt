add_executable(trace_provider_test
        trace_provider_test.cc
)

target_link_libraries(trace_provider_test PRIVATE
    trace_provider
    disassembler
    nlohmann_json::nlohmann_json
)

target_compile_definitions(trace_provider_test PRIVATE
    TRAILER_FIRMWARE_PATH="${CMAKE_SOURCE_DIR}/test_resources/firmware.elf"
    TRAILER_TRACE_DUMP_PATH="${CMAKE_SOURCE_DIR}/test_resources/etm_dump.bin"
    TRAILER_ETM_REGS_JSON_PATH="${CMAKE_SOURCE_DIR}/test_resources/etm_regs.json"
)

target_compile_features(trace_provider_test PRIVATE cxx_std_23)
