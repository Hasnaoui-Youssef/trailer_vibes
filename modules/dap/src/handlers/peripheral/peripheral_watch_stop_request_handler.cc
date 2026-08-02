#include "core/components/device_manager.hpp"
#include "handlers/request_handler.hpp"

namespace dap {

llvm::Error PeripheralWatchStopRequestHandler::Run(const protocol::TrailerPeripheralWatchStopArguments &args) const {
  return context_.Device().StopPeripheralWatch(args.watchId);
}

}  // namespace dap
