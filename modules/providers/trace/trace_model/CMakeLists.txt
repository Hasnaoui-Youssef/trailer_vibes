add_library(trace_model INTERFACE)

target_include_directories(trace_model INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_compile_features(trace_model INTERFACE cxx_std_20)
