#ifndef TRAILER_DAP_SERVICE_HPP_
#define TRAILER_DAP_SERVICE_HPP_

#include <string>
#include <vector>

#include "dap/protocol/protocol_base.hpp"

namespace dap {

// A pluggable backend the Orchestrator routes DAP requests to. Each service
// owns one domain's state (DebugService: the LLDB SB API session; a future
// TraceService: the trace-decode pipeline + an OpenOCD TCL client; etc).
// Services never touch the transport directly - they call back into the
// Orchestrator they were constructed with to send responses/events, so the
// Orchestrator remains the sole owner of the wire.
class Service {
public:
    virtual ~Service() = default;

    // The DAP command names this service handles. The Orchestrator merges
    // every registered service's list into one dispatch table at startup;
    // command names must not collide across services.
    virtual std::vector<std::string> SupportedCommands() const = 0;

    // Handles one already-routed request. No return value: implementations
    // send their own response/events back through the Orchestrator
    // reference they hold, matching how the forked lldb-dap handlers
    // already work (every handler calls `dap.Send(...)` directly).
    virtual void HandleRequest(const protocol::Request &request) = 0;
};

}  // namespace dap

#endif  // TRAILER_DAP_SERVICE_HPP_
