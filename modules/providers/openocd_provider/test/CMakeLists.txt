add_executable(openocd_provider_hw_test
        openocd_provider_hw_test.cc
)

target_link_libraries(openocd_provider_hw_test PRIVATE
    openocd_provider
)

target_compile_definitions(openocd_provider_hw_test PRIVATE
    OPENOCD_TCL_DIR="${CMAKE_SOURCE_DIR}/external/openocd/tcl"
    OPENOCD_BOARD_CFG="${CMAKE_SOURCE_DIR}/test_resources/stm32h7s3x_dummy/st_nucleo_h7s3l8.cfg"
)

target_compile_features(openocd_provider_hw_test PRIVATE cxx_std_23)
