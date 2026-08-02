#include "core/components/watch_manager.hpp"
#include "handlers/request_handler.hpp"

namespace dap {

llvm::Error WatchStopRequestHandler::Run(const protocol::TrailerWatchStopArguments &args) const {
  return context_.Watch().Stop(args.watchId);
}

}  // namespace dap
