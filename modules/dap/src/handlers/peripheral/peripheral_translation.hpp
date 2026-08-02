#ifndef TRAILER_DAP_HANDLERS_PERIPHERAL_PERIPHERAL_TRANSLATION_HPP_
#define TRAILER_DAP_HANDLERS_PERIPHERAL_PERIPHERAL_TRANSLATION_HPP_

#include "dap/protocol/protocol_requests.hpp"
#include "device_provider/memory_map.hpp"
#include "device_xml/svd_model.hpp"

namespace dap {

protocol::TrailerMemoryRegion ToTrailerMemoryRegion(const providers::device::MemoryRegion &region);
protocol::TrailerPeripheralSummary ToTrailerPeripheralSummary(const device_xml::Peripheral &peripheral);
protocol::TrailerEnumeratedValue ToTrailerEnumeratedValue(const device_xml::EnumeratedValue &value);
protocol::TrailerRegisterField ToTrailerRegisterField(const device_xml::RegisterField &field);
protocol::TrailerRegister ToTrailerRegister(const device_xml::Register &reg);
protocol::TrailerInterrupt ToTrailerInterrupt(const device_xml::Interrupt &interrupt);

}  // namespace dap

#endif  // TRAILER_DAP_HANDLERS_PERIPHERAL_PERIPHERAL_TRANSLATION_HPP_
