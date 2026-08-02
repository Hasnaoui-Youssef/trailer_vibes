#include "core/components/device_manager.hpp"
#include "dap/dap_error.hpp"
#include "handlers/request_handler.hpp"
#include "peripheral_translation.hpp"

namespace dap {

llvm::Expected<protocol::TrailerPeripheralDetailResponseBody>
PeripheralDetailRequestHandler::Run(const protocol::TrailerPeripheralDetailArguments &args) const {
  if (!args.core) {
    context_.Device().EnsureDevicePeripherals().wait();
    if (!context_.Device().DevicePeripheralsError().empty())
      return llvm::make_error<dap::DAPError>(context_.Device().DevicePeripheralsError());
  }

  const device_xml::Peripheral *peripheral = context_.Device().FindPeripheral(args.peripheral, args.core);
  if (!peripheral)
    return llvm::make_error<dap::DAPError>("no such peripheral: " + args.peripheral);

  protocol::TrailerPeripheralDetailResponseBody response;
  response.name = peripheral->name;
  response.description = peripheral->description;
  response.groupName = peripheral->group_name;
  response.baseAddress = peripheral->base_address;
  response.addressBlockOffset = peripheral->address_block.offset;
  response.addressBlockSize = peripheral->address_block.size;
  for (const device_xml::Register &reg : peripheral->registers)
    response.registers.push_back(ToTrailerRegister(reg));
  for (const device_xml::Interrupt &interrupt : peripheral->interrupts)
    response.interrupts.push_back(ToTrailerInterrupt(interrupt));

  return response;
}

}  // namespace dap
