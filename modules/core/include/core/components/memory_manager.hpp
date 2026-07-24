//===-- memory_manager.hpp ------------------------------------------------===//
//
// The memory component (see CLAUDE.md's DebugContext architecture):
// strategy pattern over how bytes get read/written. The default strategy
// reads/writes through the LLDB SB process API; a future AP-based or SVD
// peripheral strategy can implement the same interface without
// MemoryManager or its callers changing - see CLAUDE.md's Peripheral View
// section. Carved out of the read/writeMemory request handlers' inline
// logic (there was no DebugService::readMemory/writeMemory method to move -
// see PROJECT_STATUS.md).
//
//===----------------------------------------------------------------------===//

#ifndef TRAILER_CORE_COMPONENTS_MEMORY_MANAGER_HPP_
#define TRAILER_CORE_COMPONENTS_MEMORY_MANAGER_HPP_

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

#include "lldb/lldb-defines.h"
#include "lldb/lldb-types.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/Support/Error.h"
#include "lldb_provider/lldb_provider.hpp"

namespace core {

struct MemoryReadResult {
  std::vector<std::byte> data;
  lldb::addr_t address = LLDB_INVALID_ADDRESS;
  std::optional<uint64_t> unreadable_bytes;
};

struct MemoryWriteResult {
  uint64_t bytes_written = 0;
};

/// Strategy interface: one implementation per way of getting at target
/// memory. MemoryManager holds one of these; it never inlines a specific
/// access mechanism itself. Preconditions like "is the process stopped" are
/// a request-validity concern the caller checks itself (via LldbProvider
/// directly) before calling in - not this strategy's job.
class MemoryAccessStrategy {
public:
  virtual ~MemoryAccessStrategy() = default;

  virtual MemoryReadResult Read(lldb::addr_t address, uint64_t count) = 0;
  virtual llvm::Expected<MemoryWriteResult>
  Write(lldb::addr_t address, llvm::ArrayRef<char> data, bool allow_partial) = 0;
};

/// Default strategy: the live process' memory via the LLDB SB API.
class ProcessMemoryStrategy final : public MemoryAccessStrategy {
public:
  explicit ProcessMemoryStrategy(providers::LldbProvider &lldb_provider)
      : m_lldb_provider(lldb_provider) {}

  MemoryReadResult Read(lldb::addr_t address, uint64_t count) override;
  llvm::Expected<MemoryWriteResult>
  Write(lldb::addr_t address, llvm::ArrayRef<char> data, bool allow_partial) override;

private:
  providers::LldbProvider &m_lldb_provider;
};

class MemoryManager {
public:
  explicit MemoryManager(providers::LldbProvider &lldb_provider)
      : m_strategy(std::make_unique<ProcessMemoryStrategy>(lldb_provider)) {}

  MemoryReadResult ReadMemory(lldb::addr_t address, uint64_t count) {
    return m_strategy->Read(address, count);
  }
  llvm::Expected<MemoryWriteResult>
  WriteMemory(lldb::addr_t address, llvm::ArrayRef<char> data, bool allow_partial) {
    return m_strategy->Write(address, data, allow_partial);
  }

private:
  std::unique_ptr<MemoryAccessStrategy> m_strategy;
};

}  // namespace core

#endif  // TRAILER_CORE_COMPONENTS_MEMORY_MANAGER_HPP_
