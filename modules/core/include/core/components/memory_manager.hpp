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
#include "openocd_provider/memory_selector.hpp"
#include "openocd_provider/openocd_provider.hpp"

namespace core {

struct MemoryReadResult {
  std::vector<std::byte> data;
  lldb::addr_t address = LLDB_INVALID_ADDRESS;
  std::optional<uint64_t> unreadable_bytes;
};

struct MemoryWriteResult {
  uint64_t bytes_written = 0;
};

class MemoryAccessStrategy {
public:
  virtual ~MemoryAccessStrategy() = default;

  virtual llvm::Expected<MemoryReadResult>
  Read(lldb::addr_t address, uint64_t count, const providers::MemorySelector &selector) = 0;
  virtual llvm::Expected<MemoryWriteResult>
  Write(lldb::addr_t address, llvm::ArrayRef<char> data, bool allow_partial,
        const providers::MemorySelector &selector) = 0;
};

class ProcessMemoryStrategy final : public MemoryAccessStrategy {
public:
  explicit ProcessMemoryStrategy(providers::LldbProvider &lldb_provider)
      : m_lldb_provider(lldb_provider) {}

  llvm::Expected<MemoryReadResult>
  Read(lldb::addr_t address, uint64_t count, const providers::MemorySelector &selector) override;
  llvm::Expected<MemoryWriteResult>
  Write(lldb::addr_t address, llvm::ArrayRef<char> data, bool allow_partial,
        const providers::MemorySelector &selector) override;

private:
  providers::LldbProvider &m_lldb_provider;
};

class OpenOcdMemoryStrategy final : public MemoryAccessStrategy {
public:
  explicit OpenOcdMemoryStrategy(providers::OpenOcdProvider &openocd_provider)
      : m_openocd_provider(openocd_provider) {}

  llvm::Expected<MemoryReadResult>
  Read(lldb::addr_t address, uint64_t count, const providers::MemorySelector &selector) override;
  llvm::Expected<MemoryWriteResult>
  Write(lldb::addr_t address, llvm::ArrayRef<char> data, bool allow_partial,
        const providers::MemorySelector &selector) override;

private:
  providers::OpenOcdProvider &m_openocd_provider;
};

class MemoryManager {
public:
  explicit MemoryManager(providers::LldbProvider &lldb_provider)
      : m_lldb_provider(lldb_provider),
        m_strategy(std::make_unique<ProcessMemoryStrategy>(lldb_provider)) {}

  MemoryManager(providers::LldbProvider &lldb_provider, providers::OpenOcdProvider &openocd_provider)
      : m_lldb_provider(lldb_provider),
        m_strategy(std::make_unique<OpenOcdMemoryStrategy>(openocd_provider)) {}

  llvm::Expected<MemoryReadResult>
  ReadMemory(lldb::addr_t address, uint64_t count, const providers::MemorySelector &selector = {});
  llvm::Expected<MemoryWriteResult>
  WriteMemory(lldb::addr_t address, llvm::ArrayRef<char> data, bool allow_partial,
              const providers::MemorySelector &selector = {});

private:
  providers::LldbProvider &m_lldb_provider;
  std::unique_ptr<MemoryAccessStrategy> m_strategy;
};

}  // namespace core

#endif  // TRAILER_CORE_COMPONENTS_MEMORY_MANAGER_HPP_
