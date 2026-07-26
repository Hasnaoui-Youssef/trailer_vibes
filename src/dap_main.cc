#include <cstdlib>
#include <iostream>
#include <memory>
#include <unistd.h>

#include "dap/orchestrator.hpp"
#include "dap/session.hpp"
#include "dap/transport.hpp"
#include "lldb/API/SBDebugger.h"
#include "lldb/API/SBError.h"

int main() {
    const lldb::SBError init_error = lldb::SBDebugger::InitializeWithErrorHandling();
    if (init_error.Fail()) {
        std::cerr << "trailer-dap: failed to initialize LLDB: " << init_error.GetCString() << "\n";
        return EXIT_FAILURE;
    }

    {
        dap::Transport transport(/*in_fd=*/STDIN_FILENO, /*out_fd=*/STDOUT_FILENO);
        dap::Orchestrator orchestrator(std::move(transport));

        std::unique_ptr<dap::Session> session = dap::CreateSession(orchestrator);

        orchestrator.Run();
    }

    lldb::SBDebugger::Terminate();
    return EXIT_SUCCESS;
}
