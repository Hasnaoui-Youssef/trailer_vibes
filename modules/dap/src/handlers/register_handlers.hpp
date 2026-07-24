#ifndef TRAILER_DAP_HANDLERS_REGISTER_HANDLERS_HPP_
#define TRAILER_DAP_HANDLERS_REGISTER_HANDLERS_HPP_

#include <memory>
#include <vector>

#include "handlers/request_handler.hpp"

namespace dap {
class Orchestrator;
}  // namespace dap

namespace dap {

// Constructs every debug-service request handler and registers each with
// `orchestrator` by command name (see dap::Orchestrator::RegisterHandler) -
// the Orchestrator dispatches to them directly from then on, without going
// through DebugService at all. Returns ownership of the handlers so the
// caller can keep them alive for as long as `orchestrator` runs.
std::vector<std::unique_ptr<BaseRequestHandler>> RegisterDebugHandlers(Orchestrator &orchestrator, DebugService &dap);

}  // namespace dap

#endif  // TRAILER_DAP_HANDLERS_REGISTER_HANDLERS_HPP_
