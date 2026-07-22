add_library(trace_transform INTERFACE)

target_include_directories(trace_transform INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(trace_transform INTERFACE
    trace_model
    trace_sink
    # trace_sink only exposes OpenCSD::opencsd PRIVATE (its own headers
    # already need OpenCSD's include dir, but that isn't propagated to
    # consumers) - trace_transform/trace_record.hpp itself includes
    # <opencsd.h>, so every consumer needs this re-linked, same as
    # trace_decoder and the old instr_reconstruct did.
    OpenCSD::opencsd
)

target_compile_features(trace_transform INTERFACE cxx_std_23)

add_subdirectory(test)
