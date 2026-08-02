#include "core/components/device_manager.hpp"
#include "handlers/request_handler.hpp"

namespace dap {

llvm::Expected<protocol::TrailerPeripheralWatchStartResponseBody>
PeripheralWatchStartRequestHandler::Run(const protocol::TrailerPeripheralWatchStartArguments &args) const {
  if (!args.core) context_.Device().EnsureDevicePeripherals().wait();

  core::DeviceManager::PeripheralWatchStartArgs start_args;
  start_args.peripheral = args.peripheral;
  start_args.interval_ms = args.intervalMs;
  start_args.safe_only = args.safeOnly;
  start_args.core = args.core;

  llvm::Expected<int64_t> watch_id = context_.Device().StartPeripheralWatch(start_args);
  if (!watch_id) return watch_id.takeError();

  protocol::TrailerPeripheralWatchStartResponseBody response;
  response.watchId = *watch_id;
  return response;
}

}  // namespace dap
