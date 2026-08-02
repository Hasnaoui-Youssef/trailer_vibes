add_subdirectory(xml)

add_library(device_provider
        src/device_index.cc
        src/memory_map.cc
        src/rzone_parser.cc
        src/resource_paths.cc
        src/device_pack.cc
)

target_include_directories(device_provider PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(device_provider PUBLIC
    device_xml
)

target_link_libraries(device_provider PRIVATE
    pugixml::pugixml
)

target_compile_options(device_provider PUBLIC
    $<$<COMPILE_LANGUAGE:CXX>:-fno-rtti>
)

target_compile_features(device_provider PUBLIC cxx_std_23)

add_subdirectory(test)
