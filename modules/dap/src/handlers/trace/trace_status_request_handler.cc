#include "core/debug_context.hpp"
#include "handlers/request_handler.hpp"

namespace dap {

llvm::Expected<protocol::TraceStatusResponseBody>
TraceStatusRequestHandler::Run(const protocol::TraceStatusArguments &) const {
  return context_.TraceStatus();
}

} // namespace dap
