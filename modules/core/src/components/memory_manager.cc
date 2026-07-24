#include "core/components/memory_manager.hpp"

#include <algorithm>

#include "dap/dap_error.hpp"
#include "lldb/API/SBError.h"
#include "lldb/API/SBMemoryRegionInfo.h"
#include "lldb/API/SBProcess.h"
#include "llvm/ADT/StringExtras.h"

namespace core {

MemoryReadResult ProcessMemoryStrategy::Read(lldb::addr_t address, uint64_t count) {
  const uint64_t count_read = std::max<uint64_t>(count, 1);
  // We also need to support reading 0 bytes - VS Code sends those requests
  // to check if a `memoryReference` can be dereferenced.
  MemoryReadResult result;
  result.data.resize(count_read);

  lldb::SBError error;
  const size_t memory_count = m_lldb_provider.target.GetProcess().ReadMemory(
      address, result.data.data(), result.data.size(), error);

  result.address = address;

  // Reading memory may fail for multiple reasons: memory not readable,
  // reading out of memory range and gaps in memory. Return from the last
  // readable byte.
  if (error.Fail() && (memory_count < count_read))
    result.unreadable_bytes = count_read - memory_count;

  result.data.resize(std::min<size_t>(memory_count, count));
  return result;
}

llvm::Expected<MemoryWriteResult>
ProcessMemoryStrategy::Write(lldb::addr_t address, llvm::ArrayRef<char> data, bool allow_partial) {
  lldb::SBProcess process = m_lldb_provider.target.GetProcess();

  uint64_t bytes_written = 0;
  if (!data.empty()) {
    // If 'allowPartial' is false, a debug adapter should attempt to verify
    // the region is writable before writing, and fail the response if it
    // is not.
    if (!allow_partial) {
      lldb::addr_t start_address = address;
      lldb::addr_t end_address = start_address + data.size() - 1;

      while (start_address <= end_address) {
        lldb::SBMemoryRegionInfo region_info;
        lldb::SBError error = process.GetMemoryRegionInfo(start_address, region_info);
        if (!error.Success() || !region_info.IsWritable()) {
          return llvm::make_error<dap::DAPError>(
              "Memory 0x" + llvm::utohexstr(address) + " region is not writable");
        }
        if (end_address <= region_info.GetRegionEnd())
          break;
        start_address = region_info.GetRegionEnd() + 1;
      }
    }

    lldb::SBError write_error;
    bytes_written = process.WriteMemory(address, static_cast<const void *>(data.data()), data.size(),
                                        write_error);
    if (bytes_written == 0)
      return llvm::make_error<dap::DAPError>(write_error.GetCString());
  }

  return MemoryWriteResult{bytes_written};
}

}  // namespace core
