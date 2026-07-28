#include "core/components/trace_manager.hpp"
#include "core/debug_context.hpp"
#include "dap/dap_error.hpp"
#include "handlers/request_handler.hpp"

namespace dap {

llvm::Expected<protocol::TraceStatusResponseBody>
TraceStatusRequestHandler::Run(const protocol::TraceStatusArguments &) const {
  core::TraceManager *trace = context_.Trace();
  if (!trace)
    return llvm::make_error<DAPError>("trace is not available for this session");
  return protocol::TraceStatusResponseBody{trace->enabled()};
}

} // namespace dap
