#include "core/components/watch_manager.hpp"
#include "handlers/request_handler.hpp"

namespace dap {

llvm::Expected<protocol::TrailerWatchStartResponseBody>
WatchStartRequestHandler::Run(const protocol::TrailerWatchStartArguments &args) const {
  core::WatchManager::StartArgs start_args;
  start_args.address = args.address;
  start_args.size = args.size;
  start_args.interval_ms = args.intervalMs;
  start_args.target_name = args.target;

  llvm::Expected<int64_t> watch_id = context_.Watch().Start(start_args);
  if (!watch_id)
    return watch_id.takeError();

  protocol::TrailerWatchStartResponseBody response;
  response.watchId = *watch_id;
  return response;
}

}  // namespace dap
