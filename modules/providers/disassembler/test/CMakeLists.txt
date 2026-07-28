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

# Proves disassembler (LLVM) and LLDB::liblldb (which embeds its own LLVM)
# can be linked into and used from the same binary, as trailer-dap will -
# see the target's own comment for what this catches.
add_executable(llvm_lldb_coexistence_test
        llvm_lldb_coexistence_test.cc
)

target_link_libraries(llvm_lldb_coexistence_test PRIVATE
    disassembler
    LLDB::liblldb
)

target_compile_definitions(llvm_lldb_coexistence_test PRIVATE
    TRAILER_FIRMWARE_PATH="${CMAKE_SOURCE_DIR}/test_resources/firmware.elf"
)

target_compile_features(llvm_lldb_coexistence_test PRIVATE cxx_std_23)
