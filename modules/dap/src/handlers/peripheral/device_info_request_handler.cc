#include "core/components/device_manager.hpp"
#include "dap/dap_error.hpp"
#include "handlers/request_handler.hpp"
#include "peripheral_translation.hpp"

namespace dap {

llvm::Expected<protocol::TrailerDeviceInfoResponseBody>
DeviceInfoRequestHandler::Run(const protocol::TrailerDeviceInfoArguments &args) const {
  context_.Device().EnsureDevicePeripherals().wait();
  if (!context_.Device().DevicePeripheralsError().empty())
    return llvm::make_error<dap::DAPError>(context_.Device().DevicePeripheralsError());

  protocol::TrailerDeviceInfoResponseBody response;

  if (context_.Device().CorePeripherals()) {
    response.core = context_.Device().CorePeripherals()->name;
    for (const device_xml::Peripheral &peripheral : context_.Device().CorePeripherals()->peripherals)
      response.corePeripherals.push_back(ToTrailerPeripheralSummary(peripheral));
  }

  for (const providers::device::MemoryRegion &region : context_.Device().Memory().Regions())
    response.memoryRegions.push_back(ToTrailerMemoryRegion(region));

  if (context_.Device().DevicePeripherals()) {
    response.deviceName = context_.Device().DevicePeripherals()->name;
    for (const device_xml::Peripheral &peripheral : context_.Device().DevicePeripherals()->peripherals)
      response.peripherals.push_back(ToTrailerPeripheralSummary(peripheral));
  }

  return response;
}

}  // namespace dap
