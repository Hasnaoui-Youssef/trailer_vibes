add_executable(program_disassembler_test
        program_disassembler_test.cc
)

target_link_libraries(program_disassembler_test PRIVATE
    disassembler
    trace_model
)

target_compile_definitions(program_disassembler_test PRIVATE
    TRAILER_DISASSEMBLER_FIXTURE_PATH="${CMAKE_CURRENT_SOURCE_DIR}/fixture.elf"
    TRAILER_DWARF_FIXTURE_PATH="${CMAKE_CURRENT_SOURCE_DIR}/dwarf_fixture.elf"
    TRAILER_FIRMWARE_PATH="${CMAKE_SOURCE_DIR}/test_resources/firmware.elf"
)

target_compile_features(program_disassembler_test PRIVATE cxx_std_23)
