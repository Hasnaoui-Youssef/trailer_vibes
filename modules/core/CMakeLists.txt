# The domain layer (see CLAUDE.md's DebugContext/Components/Providers
# architecture). Owns DebugContext and every domain component (breakpoints,
# memory, disassembly, modules, data, execution, target). Depends on
# providers (the only thing beneath a component) and dap_protocol (the
# shared wire-format value-type vocabulary - see modules/dap/CMakeLists.txt
# for why that specific dependency is not the "core depends on dap"
# coupling the architecture forbids). Never depends on dap_core or
# dap_handlers - core must stay usable without any particular
# communication layer on top of it.

add_library(core
        src/debug_context.cc
        src/variable_description.cc
        src/lldb_utils.cc

        src/components/breakpoint_manager.cc
        src/components/breakpoint_base.cc
        src/components/breakpoint.cc
        src/components/source_breakpoint.cc
        src/components/function_breakpoint.cc
        src/components/instruction_breakpoint.cc
        src/components/exception_breakpoint.cc
        src/components/watchpoint.cc
        src/components/memory_manager.cc
        src/components/disassembly_manager.cc
        src/components/module_manager.cc
        src/components/data_manager.cc
        src/components/execution_controller.cc
        src/components/target_manager.cc
)

target_include_directories(core PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(core PUBLIC
    lldb_provider
    dap_protocol
)

target_compile_options(core PUBLIC
    $<$<COMPILE_LANGUAGE:CXX>:-fno-rtti>
)

target_compile_features(core PUBLIC cxx_std_23)
