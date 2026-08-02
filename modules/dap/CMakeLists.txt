# Three targets: dap_protocol (wire-format value types, JSON conversion, no
# Transport/Orchestrator) is the shared leaf dap_core and core both depend
# on; dap_core (transport, orchestrator, request dispatch) depends only on
# dap_protocol; dap_handlers (concrete DAP request handlers, grouped by
# domain) depends on dap_core and core - the only layer allowed to know
# about a specific component. Acyclic: dap_protocol <- dap_core <-
# dap_handlers, dap_protocol <- core <- dap_handlers.
#
# core depends on dap_protocol (not dap_core): what core must never depend
# on is dap's *behavioral* machinery (Transport/Orchestrator/dispatch) -
# dap_protocol is pure value types, so sharing it avoids parallel domain
# DTOs for every component's result shape.
#
# dap_protocol is LLDB-free: the lldb:: ID typedefs/LLDB_INVALID_* sentinels
# the wire structs use are plain fixed-width types + local macros
# (dap/protocol/dap_defines.hpp); Symbol.type is dap_protocol's own
# SymbolType enum (dap/protocol/dap_types.hpp), not lldb::SymbolType -
# core/providers convert at the LLDB boundary.

add_library(dap_protocol
        src/protocol/protocol_base.cc
        src/protocol/protocol_events.cc
        src/protocol/protocol_types.cc
        src/protocol/protocol_requests.cc
        src/protocol/dap_types.cc
        src/protocol_support.cc
        src/dap_error.cc
        src/json_utils.cc
)

target_include_directories(dap_protocol PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

# LLVM (not LLDB) include dirs are PUBLIC here: dap/protocol/*.hpp and
# dap/protocol_support.hpp use llvm::json/StringRef/DenseSet etc.
target_include_directories(dap_protocol PUBLIC
    ${LLVM_INCLUDE_DIRS}
)

target_link_libraries(dap_protocol PUBLIC
    LLVMSupport
    common
)

target_compile_options(dap_protocol PUBLIC
    $<$<COMPILE_LANGUAGE:CXX>:-fno-rtti>
)

target_compile_features(dap_protocol PUBLIC cxx_std_23)

# --- dap_core ----------------------------------------------------------

add_library(dap_core
        src/transport.cc
        src/orchestrator.cc
        src/dap_log.cc
        src/response_handler.cc
        src/progress_event.cc
)

target_include_directories(dap_core PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(dap_core PUBLIC
    dap_protocol
)

target_compile_features(dap_core PUBLIC cxx_std_23)

add_subdirectory(test)

# --- dap_handlers ------------------------------------------------------------
# Every concrete DAP request handler, grouped by domain.

add_library(dap_handlers
        src/handlers/request_handler.cc
        src/handlers/register_handlers.cc
        src/handlers/session.cc
        src/handlers/capabilities.cc

        src/handlers/lifecycle/initialize_request_handler.cc
        src/handlers/lifecycle/attach_request_handler.cc
        src/handlers/lifecycle/configuration_done_request_handler.cc
        src/handlers/lifecycle/disconnect_request_handler.cc
        src/handlers/lifecycle/cancel_request_handler.cc
        src/handlers/lifecycle/restart_request_handler.cc
        src/handlers/lifecycle/launch_request_handler.cc

        src/handlers/breakpoints/set_breakpoints_request_handler.cc
        src/handlers/breakpoints/set_function_breakpoints_request_handler.cc
        src/handlers/breakpoints/set_data_breakpoints_request_handler.cc
        src/handlers/breakpoints/data_breakpoint_info_request_handler.cc
        src/handlers/breakpoints/set_instruction_breakpoints_request_handler.cc
        src/handlers/breakpoints/set_exception_breakpoints_request_handler.cc
        src/handlers/breakpoints/breakpoint_locations_handler.cc
        src/handlers/breakpoints/test_get_target_breakpoints_request_handler.cc

        src/handlers/execution/continue_request_handler.cc
        src/handlers/execution/pause_request_handler.cc
        src/handlers/execution/next_request_handler.cc
        src/handlers/execution/step_in_request_handler.cc
        src/handlers/execution/step_out_request_handler.cc
        src/handlers/execution/step_in_targets_request_handler.cc

        src/handlers/memory/read_memory_request_handler.cc
        src/handlers/memory/write_memory_request_handler.cc

        src/handlers/disassembly/disassemble_request_handler.cc

        src/handlers/inspection/scopes_request_handler.cc
        src/handlers/inspection/variables_request_handler.cc
        src/handlers/inspection/set_variable_request_handler.cc
        src/handlers/inspection/evaluate_request_handler.cc
        src/handlers/inspection/completions_handler.cc
        src/handlers/inspection/threads_request_handler.cc
        src/handlers/inspection/stack_trace_request_handler.cc
        src/handlers/inspection/exception_info_request_handler.cc

        src/handlers/symbols/modules_request_handler.cc
        src/handlers/symbols/module_symbols_request_handler.cc
        src/handlers/symbols/compile_units_request_handler.cc
        src/handlers/symbols/source_request_handler.cc
        src/handlers/symbols/locations_request_handler.cc

        src/handlers/trace/trace_enable_request_handler.cc
        src/handlers/trace/trace_disable_request_handler.cc
        src/handlers/trace/trace_status_request_handler.cc

        src/handlers/watch/watch_start_request_handler.cc
        src/handlers/watch/watch_stop_request_handler.cc

        src/handlers/peripheral/peripheral_translation.cc
        src/handlers/peripheral/device_info_request_handler.cc
        src/handlers/peripheral/peripheral_detail_request_handler.cc
        src/handlers/peripheral/peripheral_read_request_handler.cc
        src/handlers/peripheral/peripheral_write_request_handler.cc
        src/handlers/peripheral/peripheral_watch_start_request_handler.cc
        src/handlers/peripheral/peripheral_watch_stop_request_handler.cc

        src/handlers/unknown_request_handler.cc
)

target_include_directories(dap_handlers PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(dap_handlers PUBLIC
    dap_core
    core
)
