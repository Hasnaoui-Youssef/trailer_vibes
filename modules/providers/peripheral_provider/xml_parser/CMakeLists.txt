add_library(xml_parser
        src/svd_parser.cc
)

target_include_directories(xml_parser PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(xml_parser PUBLIC
    pugixml::pugixml
)

target_compile_features(xml_parser PRIVATE cxx_std_23)

