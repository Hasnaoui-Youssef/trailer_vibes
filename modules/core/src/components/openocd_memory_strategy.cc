#include "core/components/memory_manager.hpp"
#include "dap/dap_error.hpp"

namespace core {

llvm::Expected<MemoryReadResult>
OpenOcdMemoryStrategy::Read(lldb::addr_t address, uint64_t count, const providers::MemorySelector &selector) {
  auto result = m_openocd_provider.ReadMemory(selector, address, static_cast<uint32_t>(count));
  if (!result) return llvm::make_error<dap::DAPError>(result.error());

  MemoryReadResult read_result;
  read_result.address = address;
  read_result.data.resize(result->size());
  std::memcpy(read_result.data.data(), result->data(), result->size());
  return read_result;
}

llvm::Expected<MemoryWriteResult>
OpenOcdMemoryStrategy::Write(lldb::addr_t address, llvm::ArrayRef<char> data, bool /*allow_partial*/,
                              const providers::MemorySelector &selector) {
  std::vector<std::byte> bytes(data.size());
  std::memcpy(bytes.data(), data.data(), data.size());

  auto result = m_openocd_provider.WriteMemory(selector, address, bytes);
  if (!result) return llvm::make_error<dap::DAPError>(result.error());

  return MemoryWriteResult{data.size()};
}

}  // namespace core
