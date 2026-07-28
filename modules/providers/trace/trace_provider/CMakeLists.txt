add_library(trace_provider
        src/trace_session.cc
)

target_include_directories(trace_provider PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

# PRIVATE is mandatory, not hygiene: it keeps OpenCSD's RTTI/-fexceptions
# requirements and include dirs off every consumer of trace_provider (e.g.
# core, which builds -fno-rtti). See CLAUDE.md's RTTI boundary note.
target_link_libraries(trace_provider
    PUBLIC  trace_model
    PRIVATE trace_decoder trace_transform trace_sink OpenCSD::opencsd
)

target_compile_features(trace_provider PUBLIC cxx_std_23)

add_subdirectory(test)
