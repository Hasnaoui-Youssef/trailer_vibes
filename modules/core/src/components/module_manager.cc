#include "core/components/module_manager.hpp"

#include <algorithm>
#include <mutex>

#include <array>

#include <climits>  // PATH_MAX - lldb/Host/PosixApi.h is lldb_private, off-limits here

#include "core/lldb_utils.hpp"
#include "dap/dap_error.hpp"
#include "lldb/API/SBCompileUnit.h"
#include "lldb/API/SBFileSpec.h"
#include "lldb/API/SBModule.h"
#include "lldb/API/SBModuleSpec.h"
#include "lldb/API/SBSection.h"
#include "lldb/API/SBSymbol.h"
#include "lldb/API/SBTarget.h"
#include "lldb/lldb-defines.h"
#include "llvm/ADT/SmallVector.h"
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

// Decodes UUID bytes from a hex string (optionally '-'-separated), stopping
// at the first byte pair that can't be decoded and returning the leftover
// suffix - same contract as lldb_private::UUID::DecodeUUIDBytesFromString,
// reimplemented locally since lldb_private is off-limits here.
llvm::StringRef DecodeUUIDBytesFromString(llvm::StringRef str, llvm::SmallVectorImpl<uint8_t> &uuid_bytes) {
  auto hex_value = [](char c) -> int {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
  };
  while (!str.empty()) {
    if (str.front() == '-') {
      str = str.drop_front();
      continue;
    }
    if (str.size() < 2)
      break;
    const int hi = hex_value(str[0]);
    const int lo = hex_value(str[1]);
    if (hi < 0 || lo < 0)
      break;
    uuid_bytes.push_back(static_cast<uint8_t>((hi << 4) | lo));
    str = str.drop_front(2);
  }
  return str;
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

llvm::Expected<protocol::CompileUnitsResponseBody>
ModuleManager::GetCompileUnitsRequest(const std::optional<protocol::CompileUnitsArguments> &args) {
  return m_lldb_provider.WithTarget([&]() {
    std::vector<protocol::CompileUnit> units;
    const int num_modules = m_lldb_provider.target.GetNumModules();
    for (int i = 0; i < num_modules; i++) {
      lldb::SBModule curr_module = m_lldb_provider.target.GetModuleAtIndex(i);
      if (args->moduleId == llvm::StringRef(curr_module.GetUUIDString())) {
        const int num_units = curr_module.GetNumCompileUnits();
        for (int j = 0; j < num_units; j++) {
          lldb::SBCompileUnit curr_unit = curr_module.GetCompileUnitAtIndex(j);
          char unit_path_arr[PATH_MAX];
          curr_unit.GetFileSpec().GetPath(unit_path_arr, sizeof(unit_path_arr));
          units.emplace_back(protocol::CompileUnit{std::string(unit_path_arr)});
        }
        break;
      }
    }
    return protocol::CompileUnitsResponseBody{std::move(units)};
  });
}

llvm::Expected<protocol::ModulesResponseBody>
ModuleManager::GetModulesRequest(const std::optional<protocol::ModulesArguments> &) {
  // modules_mutex is nested inside WithTarget()'s lock, matching the fixed
  // order ExecutionController::HandleTargetEvent uses for the same pair -
  // see the comment there.
  return m_lldb_provider.WithTarget([&]() {
    std::lock_guard<std::mutex> guard(modules_mutex);

    protocol::ModulesResponseBody response;
    const uint32_t total_modules = m_lldb_provider.target.GetNumModules();
    response.totalModules = total_modules;

    response.modules.reserve(total_modules);
    for (uint32_t i = 0; i < total_modules; i++) {
      lldb::SBModule module = m_lldb_provider.target.GetModuleAtIndex(i);
      std::optional<protocol::Module> result = CreateModuleDescription(module);
      if (result && !result->id.empty()) {
        modules.insert(result->id);
        response.modules.emplace_back(std::move(result).value());
      }
    }
    return response;
  });
}

llvm::Expected<protocol::ModuleSymbolsResponseBody>
ModuleManager::GetModuleSymbolsRequest(const protocol::ModuleSymbolsArguments &args) {
  return m_lldb_provider.WithTarget([&]() -> llvm::Expected<protocol::ModuleSymbolsResponseBody> {
    protocol::ModuleSymbolsResponseBody response;

    lldb::SBModuleSpec module_spec;
    if (!args.moduleId.empty()) {
      llvm::SmallVector<uint8_t, 20> uuid_bytes;
      if (!DecodeUUIDBytesFromString(args.moduleId, uuid_bytes).empty())
        return llvm::make_error<dap::DAPError>("invalid module ID");
      module_spec.SetUUIDBytes(uuid_bytes.data(), uuid_bytes.size());
    }

    if (!args.moduleName.empty()) {
      lldb::SBFileSpec file_spec;
      file_spec.SetFilename(args.moduleName.c_str());
      module_spec.SetFileSpec(file_spec);
    }

    if (!module_spec.IsValid())
      return response;

    lldb::SBModule module = m_lldb_provider.target.FindModule(module_spec);
    if (!module.IsValid())
      return llvm::make_error<dap::DAPError>("module not found");

    std::vector<protocol::Symbol> &symbols = response.symbols;
    const size_t num_symbols = module.GetNumSymbols();
    const size_t start_index = args.startIndex.value_or(0);
    const size_t end_index = std::min(start_index + args.count.value_or(num_symbols), num_symbols);
    for (size_t i = start_index; i < end_index; ++i) {
      lldb::SBSymbol symbol = module.GetSymbolAtIndex(i);
      if (!symbol.IsValid())
        continue;

      protocol::Symbol dap_symbol;
      dap_symbol.id = symbol.GetID();
      // dap::protocol::SymbolType mirrors lldb::SymbolType value-for-value, so
      // this cast is exact.
      dap_symbol.type = static_cast<protocol::SymbolType>(symbol.GetType());
      dap_symbol.isDebug = symbol.IsDebug();
      dap_symbol.isSynthetic = symbol.IsSynthetic();
      dap_symbol.isExternal = symbol.IsExternal();

      lldb::SBAddress start_address = symbol.GetStartAddress();
      if (start_address.IsValid()) {
        if (lldb::addr_t file_address = start_address.GetFileAddress(); file_address != LLDB_INVALID_ADDRESS)
          dap_symbol.fileAddress = file_address;

        if (lldb::addr_t load_address = start_address.GetLoadAddress(m_lldb_provider.target);
            load_address != LLDB_INVALID_ADDRESS)
          dap_symbol.loadAddress = load_address;
      }

      dap_symbol.size = symbol.GetSize();
      if (const char *symbol_name = symbol.GetName())
        dap_symbol.name = symbol_name;
      symbols.push_back(std::move(dap_symbol));
    }

    return response;
  });
}

}  // namespace core
