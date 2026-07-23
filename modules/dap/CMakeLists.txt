add_library(dap
        src/protocol/protocol_base.cc
        src/protocol/protocol_events.cc
        src/protocol/protocol_types.cc
        src/protocol/protocol_requests.cc
        src/protocol/dap_types.cc
        src/protocol_support.cc
        src/transport.cc
        src/orchestrator.cc

        src/debug_service/breakpoint.cc
        src/debug_service/breakpoint_base.cc
        src/debug_service/client_launcher.cc
        src/debug_service/command_plugins.cc
        src/debug_service/dap_error.cc
        src/debug_service/dap_log.cc
        src/debug_service/debug_service.cc
        src/debug_service/event_helper.cc
        src/debug_service/exception_breakpoint.cc
        src/debug_service/fifo_files.cc
        src/debug_service/function_breakpoint.cc
        src/debug_service/instruction_breakpoint.cc
        src/debug_service/json_utils.cc
        src/debug_service/lldb_utils.cc
        src/debug_service/output_redirector.cc
        src/debug_service/progress_event.cc
        src/debug_service/protocol_utils.cc
        src/debug_service/request_handler.cc
        src/debug_service/response_handler.cc
        src/debug_service/run_in_terminal.cc
        src/debug_service/source_breakpoint.cc
        src/debug_service/variables.cc
        src/debug_service/watchpoint.cc

        src/debug_service/handlers/initialize_request_handler.cc
        src/debug_service/handlers/attach_request_handler.cc
        src/debug_service/handlers/configuration_done_request_handler.cc
        src/debug_service/handlers/threads_request_handler.cc
        src/debug_service/handlers/stack_trace_request_handler.cc
        src/debug_service/handlers/continue_request_handler.cc
        src/debug_service/handlers/pause_request_handler.cc
        src/debug_service/handlers/disconnect_request_handler.cc
        src/debug_service/handlers/unknown_request_handler.cc

        src/debug_service/handlers/breakpoint_locations_handler.cc
        src/debug_service/handlers/cancel_request_handler.cc
        src/debug_service/handlers/compile_units_request_handler.cc
        src/debug_service/handlers/completions_handler.cc
        src/debug_service/handlers/data_breakpoint_info_request_handler.cc
        src/debug_service/handlers/disassemble_request_handler.cc
        src/debug_service/handlers/evaluate_request_handler.cc
        src/debug_service/handlers/exception_info_request_handler.cc
        src/debug_service/handlers/launch_request_handler.cc
        src/debug_service/handlers/locations_request_handler.cc
        src/debug_service/handlers/modules_request_handler.cc
        src/debug_service/handlers/module_symbols_request_handler.cc
        src/debug_service/handlers/next_request_handler.cc
        src/debug_service/handlers/read_memory_request_handler.cc
        src/debug_service/handlers/restart_request_handler.cc
        src/debug_service/handlers/scopes_request_handler.cc
        src/debug_service/handlers/set_breakpoints_request_handler.cc
        src/debug_service/handlers/set_data_breakpoints_request_handler.cc
        src/debug_service/handlers/set_exception_breakpoints_request_handler.cc
        src/debug_service/handlers/set_function_breakpoints_request_handler.cc
        src/debug_service/handlers/set_instruction_breakpoints_request_handler.cc
        src/debug_service/handlers/set_variable_request_handler.cc
        src/debug_service/handlers/source_request_handler.cc
        src/debug_service/handlers/step_in_request_handler.cc
        src/debug_service/handlers/step_in_targets_request_handler.cc
        src/debug_service/handlers/step_out_request_handler.cc
        src/debug_service/handlers/test_get_target_breakpoints_request_handler.cc
        src/debug_service/handlers/variables_request_handler.cc
        src/debug_service/handlers/write_memory_request_handler.cc
)

target_include_directories(dap PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_include_directories(dap PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src/debug_service/include
)

# Unlike modules/disassembler, LLDB/LLVM headers are NOT a private
# implementation detail here: this module's own public headers
# (dap/debug_service_factory.hpp, dap/protocol/*.hpp) directly name lldb::
# typedefs and llvm::json types, so callers of this module need these
# include paths too
# - hence PUBLIC rather than PRIVATE. ${LLVM_INCLUDE_DIRS} and
# LLDB_INCLUDE_DIR come from the top-level find_package(LLVM CONFIG) /
# manual LLDB find_path (LLDB has no CMake package config) - see the root
# CMakeLists for why both are system-installed rather than built from
# source.
target_include_directories(dap PUBLIC
    ${LLVM_INCLUDE_DIRS}
    ${LLDB_INCLUDE_DIR}
)

target_link_libraries(dap PUBLIC
    LLDB::liblldb
    LLVMSupport
)

# Same RTTI boundary discipline as modules/disassembler: LLVM/LLDB are
# built -fno-rtti, and this module links liblldb directly, so it must never
# be linked into a translation unit that also touches OpenCSD (which
# requires RTTI) - see modules/disassembler/CMakeLists.txt for the fuller
# rationale.
target_compile_options(dap PUBLIC
    $<$<COMPILE_LANGUAGE:CXX>:-fno-rtti>
)

target_compile_features(dap PUBLIC cxx_std_23)
