# Providers are the backend layer: each one is a concrete integration with
# no abstraction layer beneath it (see CLAUDE.md). disassembler is
# standalone (LLVM ELF/DWARF, -fno-rtti) and reusable by any future
# service. trace/ groups the OpenCSD-based trace-decode pipeline
# (model/sink/decoder/transform/snapshot_parser) - a future TraceManager's
# provider; still built as five independent targets, just grouped by
# directory for now (no wiring/merge in this pass).

add_subdirectory(lldb_provider)
add_subdirectory(disassembler)

add_subdirectory(trace/trace_model)
add_subdirectory(trace/trace_sink)
add_subdirectory(trace/trace_decoder)
add_subdirectory(trace/trace_transform)
add_subdirectory(trace/snapshot_parser)
