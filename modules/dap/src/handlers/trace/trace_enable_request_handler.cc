#include "core/components/trace_manager.hpp"
#include "core/debug_context.hpp"
#include "dap/dap_error.hpp"
#include "handlers/request_handler.hpp"

namespace dap {

llvm::Expected<protocol::TraceStatusResponseBody>
TraceEnableRequestHandler::Run(const protocol::TraceEnableArguments &) const {
  core::TraceManager *trace = context_.Trace();
  if (!trace)
    return llvm::make_error<DAPError>("trace is not available for this session");
  if (llvm::Error err = trace->Enable())
    return std::move(err);
  return protocol::TraceStatusResponseBody{trace->enabled()};
}

} // namespace dap
