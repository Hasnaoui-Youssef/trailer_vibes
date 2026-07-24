# The dap module builds four targets, matching the layering described in
# CLAUDE.md: dap_protocol (the plain wire-format vocabulary - Request/
# Response/Event/Breakpoint/Source/... value types, JSON conversion, no
# Transport or Orchestrator) is the shared leaf both dap_core and core
# depend on, same role as common; dap_core (transport, orchestrator, the
# generic request-handling seam) depends only on dap_protocol, never on any
# service; debug_service (the forked lldb-dap surface, still LLDB-heavy,
# transitional until the DebugContext/component work absorbs it) depends on
# dap_core; dap_handlers (all concrete DAP request handlers, grouped by
# domain) depends on dap_core, debug_service and core, since handlers are
# the only thing allowed to know about a specific service or component.
# This keeps the dependency graph acyclic: dap_protocol <- dap_core <-
# debug_service <- dap_handlers, dap_protocol <- core <- dap_handlers.
#
# Why core depends on dap_protocol at all, when the plan's "key inversion"
# is core never depending on dap: what core actually must not depend on is
# dap's *behavioral* machinery (Transport/Orchestrator/handler dispatch) -
# that's the coupling that would make core unusable under a different
# frontend. dap_protocol carries none of that; it is pure value types.
# Sharing it avoids maintaining parallel domain DTOs for every one of
# dozens of result shapes across all seven components purely for the sake
# of a purity core does not otherwise need yet. It is a deliberate,
# documented scope reduction, not an oversight - see PROJECT_STATUS.md.
#
# dap_protocol is LLDB-free (Phase 5's protocol purge, PROJECT_STATUS.md
# §7/§12): the handful of lldb:: ID typedefs and LLDB_INVALID_* sentinels
# the wire structs used are now plain fixed-width types + locally-defined
# macros (dap/protocol/dap_defines.hpp), and Symbol.type is dap_protocol's
# own SymbolType enum (dap/protocol/dap_types.hpp) rather than
# lldb::SymbolType - core/providers convert at the LLDB boundary. This is
# the one dap target that never links LLDB or needs the RTTI-boundary
# discipline documented on debug_service below.

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

# --- debug_service ---------------------------------------------------------
# The forked lldb-dap surface. Transitional: still nested under this
# module's src/ rather than a sibling top-level module, since it's meant to
# be absorbed into a future DebugContext/component layer rather than live
# on indefinitely as its own module (see CLAUDE.md).

# Breakpoint node types (breakpoint/breakpoint_base/source_breakpoint/
# function_breakpoint/instruction_breakpoint/exception_breakpoint/
# watchpoint) moved to modules/core/components - see PROJECT_STATUS.md's
# Breakpoint carve. debug_service depends on core now (allowed: dap may
# depend on core, core may never depend on dap - see modules/core's own
# CMakeLists comment) so its remaining SetSourceBreakpoints etc. callers
# can delegate to core::BreakpointManager during the transition.
add_library(debug_service
        src/debug_service/debug_service.cc
        src/debug_service/lldb_utils.cc
)

# Public (not private): dap_handlers links this target and its handler base
# holds a DebugService& directly, so it needs to see debug_service/*.hpp
# too - there's no narrower public surface to carve out yet (see
# debug_service.hpp's own comment on this).
target_include_directories(debug_service PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src/debug_service/include
)

target_link_libraries(debug_service PUBLIC
    dap_core
    common
    lldb_provider
    core
)

# --- dap_handlers ------------------------------------------------------------
# Every concrete DAP request handler, grouped by domain. Handlers are part
# of the communication layer, not the services they call into - see
# handlers/request_handler.hpp - so they live here, not inside
# debug_service, even though today they still hold a DebugService&.

add_library(dap_handlers
        src/handlers/request_handler.cc
        src/handlers/register_handlers.cc
        src/handlers/debug_service_session.cc

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

        src/handlers/unknown_request_handler.cc
)

# PRIVATE: nothing outside this target includes "handlers/*.hpp" - the one
# thing dap_main.cc needs (DebugServiceSession) is declared in dap_core's
# public dap/debug_service_factory.hpp instead.
target_include_directories(dap_handlers PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(dap_handlers PUBLIC
    dap_core
    debug_service
    core
)
