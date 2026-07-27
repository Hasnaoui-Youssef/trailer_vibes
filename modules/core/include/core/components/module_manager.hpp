#ifndef TRAILER_CORE_COMPONENTS_MODULE_MANAGER_HPP_
#define TRAILER_CORE_COMPONENTS_MODULE_MANAGER_HPP_

#include <mutex>
#include <optional>
#include <vector>

#include "dap/protocol/protocol_requests.hpp"
#include "dap/protocol/protocol_types.hpp"
#include "lldb/API/SBAddress.h"
#include "lldb/API/SBFrame.h"
#include "lldb/API/SBModule.h"
#include "lldb/lldb-types.h"
#include "llvm/ADT/StringSet.h"
#include "llvm/Support/Error.h"
#include "lldb_provider/lldb_provider.hpp"

namespace core {

class ModuleManager {
public:
  explicit ModuleManager(providers::LldbProvider &lldb_provider) : m_lldb_provider(lldb_provider) {}

  ModuleManager(const ModuleManager &) = delete;
  ModuleManager &operator=(const ModuleManager &) = delete;

  std::optional<dap::protocol::Source> ResolveSource(const lldb::SBFrame &frame);
  std::optional<dap::protocol::Source> ResolveSource(lldb::SBAddress address);
  std::optional<dap::protocol::Source> ResolveAssemblySource(lldb::SBAddress address);

  int32_t CreateSourceReference(lldb::addr_t address);
  std::optional<lldb::addr_t> GetSourceReferenceAddress(int32_t reference);

  std::optional<dap::protocol::Module> CreateModuleDescription(lldb::SBModule &module, bool id_only = false);

  llvm::Expected<dap::protocol::CompileUnitsResponseBody> GetCompileUnitsRequest(
      const std::optional<dap::protocol::CompileUnitsArguments> &args);
  llvm::Expected<dap::protocol::ModulesResponseBody> GetModulesRequest(
      const std::optional<dap::protocol::ModulesArguments> &args);
  llvm::Expected<dap::protocol::ModuleSymbolsResponseBody> GetModuleSymbolsRequest(
      const dap::protocol::ModuleSymbolsArguments &args);

  std::mutex modules_mutex;
  llvm::StringSet<> modules;

private:
  providers::LldbProvider &m_lldb_provider;

  std::vector<lldb::addr_t> m_source_references;
  std::mutex m_source_references_mutex;
};

}  // namespace core

#endif  // TRAILER_CORE_COMPONENTS_MODULE_MANAGER_HPP_
