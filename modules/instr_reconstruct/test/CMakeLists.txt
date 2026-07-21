add_executable(reconstruct_test
        reconstruct_test.cc
)

target_link_libraries(reconstruct_test PRIVATE
    instr_reconstruct
)

target_compile_definitions(reconstruct_test PRIVATE
    TRAILER_DISASSEMBLER_FIXTURE_PATH="${CMAKE_SOURCE_DIR}/modules/disassembler/test/fixture.elf"
)

target_compile_features(reconstruct_test PRIVATE cxx_std_20)
