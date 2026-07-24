#include "core/components/module_manager.hpp"

#include <algorithm>
#include <mutex>

#include <array>

#include "core/lldb_utils.hpp"
#include "lldb/API/SBFileSpec.h"
#include "lldb/API/SBModule.h"
#include "lldb/API/SBSection.h"
#include "lldb/API/SBSymbol.h"
#include "lldb/API/SBTarget.h"
#include "lldb/lldb-defines.h"
#include "llvm/Support/FormatVariadic.h"

namespace core {

namespace protocol = dap::protocol;

namespace {

uint64_t GetDebugInfoSizeInSection(lldb::SBSection section) {
  uint64_t debug_info_size = 0;
  const llvm::StringRef section_name(section.GetName());
  if (section_name.starts_with(".debug") || section_name.starts_with("__debug") ||
      section_name.starts_with(".apple") || section_name.starts_with("__apple"))
    debug_info_size += section.GetFileByteSize();

  const size_t num_sub_sections = section.GetNumSubSections();
  for (size_t i = 0; i < num_sub_sections; i++)
    debug_info_size += GetDebugInfoSizeInSection(section.GetSubSectionAtIndex(i));

  return debug_info_size;
}

uint64_t GetDebugInfoSize(lldb::SBModule module) {
  uint64_t debug_info_size = 0;
  const size_t num_sections = module.GetNumSections();
  for (size_t i = 0; i < num_sections; i++)
    debug_info_size += GetDebugInfoSizeInSection(module.GetSectionAtIndex(i));

  return debug_info_size;
}

}  // namespace

std::optional<protocol::Source> ModuleManager::ResolveSource(const lldb::SBFrame &frame) {
  if (!frame.IsValid())
    return std::nullopt;
  const lldb::SBLineEntry frame_line_entry = frame.GetLineEntry();
  if (DisplayAssemblySource(m_lldb_provider.debugger, frame_line_entry))
    return ResolveAssemblySource(frame.GetPCAddress());
  return CreateSource(frame_line_entry.GetFileSpec());
}

std::optional<protocol::Source> ModuleManager::ResolveSource(lldb::SBAddress address) {
  lldb::SBLineEntry line_entry = GetLineEntryForAddress(m_lldb_provider.target, address);
  if (DisplayAssemblySource(m_lldb_provider.debugger, line_entry))
    return ResolveAssemblySource(address);
  if (!line_entry.IsValid())
    return std::nullopt;
  return CreateSource(line_entry.GetFileSpec());
}

std::optional<protocol::Source> ModuleManager::ResolveAssemblySource(lldb::SBAddress address) {
  lldb::SBSymbol symbol = address.GetSymbol();
  lldb::addr_t load_addr = LLDB_INVALID_ADDRESS;
  std::string name;
  if (symbol.IsValid()) {
    load_addr = symbol.GetStartAddress().GetLoadAddress(m_lldb_provider.target);
    name = symbol.GetName();
  } else {
    load_addr = address.GetLoadAddress(m_lldb_provider.target);
    name = GetLoadAddressString(load_addr);
  }

  if (load_addr == LLDB_INVALID_ADDRESS)
    return std::nullopt;

  protocol::Source source;
  source.sourceReference = CreateSourceReference(load_addr);
  lldb::SBModule module = address.GetModule();
  if (module.IsValid()) {
    lldb::SBFileSpec file_spec = module.GetFileSpec();
    if (file_spec.IsValid()) {
      std::string path = GetSBFileSpecPath(file_spec);
      if (!path.empty())
        source.path = path + '`' + name;
    }
  }
  source.name = std::move(name);
  source.presentationHint = protocol::Source::eSourcePresentationHintDeemphasize;
  return source;
}

int32_t ModuleManager::CreateSourceReference(lldb::addr_t address) {
  std::lock_guard<std::mutex> guard(m_source_references_mutex);
  auto iter = std::find(m_source_references.begin(), m_source_references.end(), address);
  if (iter != m_source_references.end())
    return static_cast<int32_t>(std::distance(m_source_references.begin(), iter) + 1);
  m_source_references.emplace_back(address);
  return static_cast<int32_t>(m_source_references.size());
}

std::optional<lldb::addr_t> ModuleManager::GetSourceReferenceAddress(int32_t reference) {
  std::lock_guard<std::mutex> guard(m_source_references_mutex);
  if (reference <= LLDB_DAP_INVALID_SRC_REF)
    return std::nullopt;
  if (static_cast<size_t>(reference) > m_source_references.size())
    return std::nullopt;
  return m_source_references[reference - 1];
}

// Mirrors debug_service/protocol_utils.cc's CreateModule exactly (see
// PROJECT_STATUS.md - not relocated/shared since debug_service's
// `modules` request handler isn't carved and still needs its own copy).
std::optional<protocol::Module> ModuleManager::CreateModuleDescription(lldb::SBModule &module, bool id_only) {
  const lldb::SBTarget &target = m_lldb_provider.target;
  if (!target.IsValid() || !module.IsValid())
    return std::nullopt;

  const llvm::StringRef uuid = module.GetUUIDString();
  if (uuid.empty())
    return std::nullopt;

  protocol::Module p_module;
  p_module.id = uuid;

  if (id_only)
    return p_module;

  constexpr size_t kPathBufferSize = 4096;
  std::array<char, kPathBufferSize> path_buffer{};
  if (const lldb::SBFileSpec file_spec = module.GetFileSpec()) {
    p_module.name = file_spec.GetFilename();

    const uint32_t path_size = file_spec.GetPath(path_buffer.data(), path_buffer.size());
    p_module.path = std::string(path_buffer.data(), path_size);
  }

  if (const uint32_t num_compile_units = module.GetNumCompileUnits(); num_compile_units > 0) {
    p_module.symbolStatus = "Symbols loaded.";

    p_module.debugInfoSizeBytes = GetDebugInfoSize(module);

    if (const lldb::SBFileSpec symbol_fspec = module.GetSymbolFileSpec()) {
      const uint32_t path_size = symbol_fspec.GetPath(path_buffer.data(), path_buffer.size());
      p_module.symbolFilePath = std::string(path_buffer.data(), path_size);
    }
  } else {
    p_module.symbolStatus = "Symbols not found.";
  }

  const auto load_address = module.GetObjectFileHeaderAddress();
  if (const lldb::addr_t raw_address = load_address.GetLoadAddress(target);
      raw_address != LLDB_INVALID_ADDRESS)
    p_module.addressRange = llvm::formatv("{0:x}", raw_address);

  std::array<uint32_t, 3> version_nums{};
  const uint32_t num_versions = module.GetVersion(version_nums.data(), version_nums.size());
  if (num_versions > 0) {
    p_module.version =
        llvm::formatv("{:$[.]}", llvm::make_range(version_nums.begin(), version_nums.begin() + num_versions));
  }

  return p_module;
}

}  // namespace core
