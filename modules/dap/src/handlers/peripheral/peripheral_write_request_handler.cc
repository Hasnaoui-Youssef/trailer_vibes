#include "core/components/device_manager.hpp"
#include "handlers/request_handler.hpp"

namespace dap {

llvm::Expected<protocol::TrailerPeripheralWriteResponseBody>
PeripheralWriteRequestHandler::Run(const protocol::TrailerPeripheralWriteArguments &args) const {
  if (!args.core) context_.Device().EnsureDevicePeripherals().wait();

  llvm::Expected<std::optional<std::uint32_t>> value =
      context_.Device().WritePeripheralRegister(args.peripheral, args.registerName, args.value, args.core);
  if (!value) return value.takeError();

  protocol::TrailerPeripheralWriteResponseBody response;
  response.value = *value;
  return response;
}

}  // namespace dap
