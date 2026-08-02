#include "peripheral_translation.hpp"

namespace dap {

namespace {

std::string ToString(providers::device::MemoryKind kind) {
  switch (kind) {
  case providers::device::MemoryKind::kRam: return "ram";
  case providers::device::MemoryKind::kRom: return "rom";
  case providers::device::MemoryKind::kExternal: return "external";
  }
  return "external";
}

std::string ToString(device_xml::AccessKind kind) {
  switch (kind) {
  case device_xml::AccessKind::kUnspecified: return "unspecified";
  case device_xml::AccessKind::kReadOnly: return "read-only";
  case device_xml::AccessKind::kWriteOnly: return "write-only";
  case device_xml::AccessKind::kReadWrite: return "read-write";
  case device_xml::AccessKind::kWriteOnce: return "write-once";
  case device_xml::AccessKind::kReadWriteOnce: return "read-write-once";
  }
  return "unspecified";
}

std::string ToString(device_xml::ReadActionKind kind) {
  switch (kind) {
  case device_xml::ReadActionKind::kNone: return "";
  case device_xml::ReadActionKind::kClear: return "clear";
  case device_xml::ReadActionKind::kSet: return "set";
  case device_xml::ReadActionKind::kModify: return "modify";
  case device_xml::ReadActionKind::kModifyExternal: return "modify-external";
  }
  return "";
}

}  // namespace

protocol::TrailerMemoryRegion ToTrailerMemoryRegion(const providers::device::MemoryRegion &region) {
  protocol::TrailerMemoryRegion out;
  out.name = region.name;
  out.start = region.start;
  out.size = region.size;
  out.readable = region.readable;
  out.writable = region.writable;
  out.executable = region.executable;
  out.kind = ToString(region.kind);
  return out;
}

protocol::TrailerPeripheralSummary ToTrailerPeripheralSummary(const device_xml::Peripheral &peripheral) {
  protocol::TrailerPeripheralSummary out;
  out.name = peripheral.name;
  out.description = peripheral.description;
  out.groupName = peripheral.group_name;
  out.baseAddress = peripheral.base_address;
  out.addressBlockSize = peripheral.address_block.size;
  out.registerCount = peripheral.registers.size();
  return out;
}

protocol::TrailerEnumeratedValue ToTrailerEnumeratedValue(const device_xml::EnumeratedValue &value) {
  protocol::TrailerEnumeratedValue out;
  out.name = value.name;
  out.description = value.description;
  out.value = value.value;
  out.isDefault = value.is_default;
  return out;
}

protocol::TrailerRegisterField ToTrailerRegisterField(const device_xml::RegisterField &field) {
  protocol::TrailerRegisterField out;
  out.name = field.name;
  out.description = field.description;
  out.bitOffset = field.bit_offset;
  out.bitWidth = field.bit_width;
  out.access = ToString(field.access);
  out.readAction = ToString(field.read_action);
  for (const device_xml::EnumeratedValue &value : field.enumerated_values)
    out.enumeratedValues.push_back(ToTrailerEnumeratedValue(value));
  return out;
}

protocol::TrailerRegister ToTrailerRegister(const device_xml::Register &reg) {
  protocol::TrailerRegister out;
  out.name = reg.name;
  out.description = reg.description;
  out.addressOffset = reg.address_offset;
  out.sizeBits = reg.size_bits;
  out.access = ToString(reg.access);
  out.readAction = ToString(reg.read_action);
  out.readSafe = reg.ReadSafe();
  out.resetValue = reg.reset_value;
  out.resetMask = reg.reset_mask;
  for (const device_xml::RegisterField &field : reg.fields)
    out.fields.push_back(ToTrailerRegisterField(field));
  return out;
}

protocol::TrailerInterrupt ToTrailerInterrupt(const device_xml::Interrupt &interrupt) {
  protocol::TrailerInterrupt out;
  out.name = interrupt.name;
  out.description = interrupt.description;
  out.value = interrupt.value;
  return out;
}

}  // namespace dap
