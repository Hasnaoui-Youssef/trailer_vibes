// trailer-dap: the DAP adapter binary. VS Code (or any DAP client) spawns
// this as a subprocess and speaks the Debug Adapter Protocol to it over
// stdin/stdout (see modules/dap/include/dap/transport.hpp for the wire
// framing). stdout carries only DAP frames - all diagnostics go to stderr,
// same "machine-readable channel is sacred" rule the trailer trace CLI
// follows (see CLAUDE.md).
//
// This is a separate executable from `trailer` (the trace-analysis CLI):
// unifying the two under one binary with subcommands is future work, not
// needed for this first plumbing milestone.

#include <cstdlib>
#include <iostream>
#include <memory>
#include <unistd.h>

#include "dap/debug_service_factory.hpp"
#include "dap/orchestrator.hpp"
#include "dap/transport.hpp"
#include "lldb/API/SBDebugger.h"
#include "lldb/API/SBError.h"

int main() {
    // Process-wide LLDB bootstrap: must happen exactly once, before any
    // lldb::SBDebugger is constructed, and be undone with Terminate() once,
    // at shutdown (mirrors lldb-dap's tool/lldb-dap.cpp).
    const lldb::SBError init_error = lldb::SBDebugger::InitializeWithErrorHandling();
    if (init_error.Fail()) {
        std::cerr << "trailer-dap: failed to initialize LLDB: " << init_error.GetCString() << "\n";
        return EXIT_FAILURE;
    }

    {
        dap::Transport transport(/*in_fd=*/STDIN_FILENO, /*out_fd=*/STDOUT_FILENO);
        dap::Orchestrator orchestrator(std::move(transport));

        // The debug service session owns the DebugService instance and
        // every one of its request handlers, registering each directly
        // with the Orchestrator (see dap/debug_service_factory.hpp) -
        // dispatch no longer goes through a dap::Service at all. Future
        // services (instruction trace, peripherals) get their own session
        // object alongside this one.
        std::unique_ptr<dap::DebugServiceSession> debug_service_session =
            dap::CreateDebugServiceSession(orchestrator);

        orchestrator.Run();
    }

    lldb::SBDebugger::Terminate();
    return EXIT_SUCCESS;
}
