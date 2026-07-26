//===-- memory_manager.hpp ------------------------------------------------===//
//
// Strategy pattern over memory access: swap ProcessMemoryStrategy for an
// AP-based/SVD peripheral strategy without callers changing.
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

// One implementation per access mechanism. Preconditions like "is the
// process stopped" are MemoryManager's job, not the strategy's.
class MemoryAccessStrategy {
public:
  virtual ~MemoryAccessStrategy() = default;

  virtual MemoryReadResult Read(lldb::addr_t address, uint64_t count) = 0;
  virtual llvm::Expected<MemoryWriteResult>
  Write(lldb::addr_t address, llvm::ArrayRef<char> data, bool allow_partial) = 0;
};

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
      : m_lldb_provider(lldb_provider),
        m_strategy(std::make_unique<ProcessMemoryStrategy>(lldb_provider)) {}

  // Checks the process is stopped (under the API mutex) before delegating.
  llvm::Expected<MemoryReadResult> ReadMemory(lldb::addr_t address, uint64_t count);
  llvm::Expected<MemoryWriteResult>
  WriteMemory(lldb::addr_t address, llvm::ArrayRef<char> data, bool allow_partial);

private:
  providers::LldbProvider &m_lldb_provider;
  std::unique_ptr<MemoryAccessStrategy> m_strategy;
};

}  // namespace core

#endif  // TRAILER_CORE_COMPONENTS_MEMORY_MANAGER_HPP_
