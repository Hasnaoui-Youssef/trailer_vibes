add_executable(device_provider_test
        device_provider_test.cc
)

target_link_libraries(device_provider_test PRIVATE
    device_provider
)

target_compile_definitions(device_provider_test PRIVATE
    TRAILER_DEVICE_INDEX_PATH="${CMAKE_SOURCE_DIR}/resources/device_index.tsv"
    TRAILER_RZONE_DIR="${CMAKE_SOURCE_DIR}/resources/XML/Rzone"
    TRAILER_CORES_DIR="${CMAKE_SOURCE_DIR}/resources/XML/Cores"
)

target_compile_features(device_provider_test PRIVATE cxx_std_23)

add_executable(device_svd_test
        device_svd_test.cc
)

target_link_libraries(device_svd_test PRIVATE
    device_xml
)

target_compile_definitions(device_svd_test PRIVATE
    TRAILER_DEVICE_SVD_PATH="${CMAKE_SOURCE_DIR}/test_resources/xml/stm32h7s.svd"
)

target_compile_features(device_svd_test PRIVATE cxx_std_23)
