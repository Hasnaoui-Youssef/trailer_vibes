add_library(config_parser
        src/config_parser.cc
        src/config_parser_json.cc
)

target_include_directories(config_parser PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(config_parser PRIVATE
    nlohmann_json::nlohmann_json
)

target_compile_features(config_parser PRIVATE cxx_std_23)
