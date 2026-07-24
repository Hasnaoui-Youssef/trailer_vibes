//===-- module_manager.hpp ------------------------------------------------===//
//
// The module component (see CLAUDE.md's DebugContext architecture): source
// resolution, source references and the loaded-module set. Carved out of
// DebugService - see PROJECT_STATUS.md.
//
//===----------------------------------------------------------------------===//

#ifndef TRAILER_CORE_COMPONENTS_MODULE_MANAGER_HPP_
#define TRAILER_CORE_COMPONENTS_MODULE_MANAGER_HPP_

#include <mutex>
#include <optional>
#include <vector>

#include "dap/protocol/protocol_types.hpp"
#include "lldb/API/SBAddress.h"
#include "lldb/API/SBFrame.h"
#include "lldb/API/SBModule.h"
#include "lldb/lldb-types.h"
#include "llvm/ADT/StringSet.h"
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

  /// Converts a loaded module to a protocol::Module description. `id_only`
  /// initializes only the module ID, used when reporting a "removed"
  /// module event.
  std::optional<dap::protocol::Module> CreateModuleDescription(lldb::SBModule &module, bool id_only = false);

  /// The set of module IDs the client has already been told about (see the
  /// `modules` request handler - it reports newly-seen modules and needs to
  /// remember which ones it already reported).
  std::mutex modules_mutex;
  llvm::StringSet<> modules;

private:
  providers::LldbProvider &m_lldb_provider;

  std::vector<lldb::addr_t> m_source_references;
  std::mutex m_source_references_mutex;
};

}  // namespace core

#endif  // TRAILER_CORE_COMPONENTS_MODULE_MANAGER_HPP_
