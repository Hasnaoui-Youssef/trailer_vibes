#include "core/components/device_manager.hpp"
#include "handlers/request_handler.hpp"

namespace dap {

llvm::Expected<protocol::TrailerPeripheralReadResponseBody>
PeripheralReadRequestHandler::Run(const protocol::TrailerPeripheralReadArguments &args) const {
  if (!args.core) context_.Device().EnsureDevicePeripherals().wait();

  llvm::Expected<std::vector<core::RegisterValue>> values =
      context_.Device().ReadPeripheral(args.peripheral, args.safeOnly, args.core);
  if (!values) return values.takeError();

  protocol::TrailerPeripheralReadResponseBody response;
  for (const core::RegisterValue &value : *values)
    response.registers.push_back({value.name, value.value});
  return response;
}

}  // namespace dap
