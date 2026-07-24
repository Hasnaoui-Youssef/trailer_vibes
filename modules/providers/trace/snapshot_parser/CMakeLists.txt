add_library(snapshot_parser
        src/device_info.cpp
        src/device_parser.cpp
        src/snapshot_parser.cpp
        src/snapshot_parser_util.cpp
        src/snapshot_reader.cpp
        src/ss_to_dcdtree.cpp
)

target_include_directories(snapshot_parser PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include/snapshot_parser
)

target_link_libraries(snapshot_parser PRIVATE
    OpenCSD::opencsd
)
