#include "core/components/breakpoint_manager.hpp"

#include <algorithm>
#include <limits>
#include <set>
#include <string>

#include "core/components/data_manager.hpp"
#include "core/components/watchpoint.hpp"
#include "dap/dap_error.hpp"
#include "lldb/API/SBAddress.h"
#include "lldb/API/SBCompileUnit.h"
#include "lldb/API/SBData.h"
#include "lldb/API/SBDebugger.h"
#include "lldb/API/SBFileSpec.h"
#include "lldb/API/SBFrame.h"
#include "lldb/API/SBInstructionList.h"
#include "lldb/API/SBLanguageRuntime.h"
#include "lldb/API/SBLineEntry.h"
#include "lldb/API/SBMemoryRegionInfo.h"
#include "lldb/API/SBMutex.h"
#include "lldb/API/SBProcess.h"
#include "lldb/API/SBSymbol.h"
#include "lldb/API/SBSymbolContext.h"
#include "lldb/API/SBTarget.h"
#include "lldb/API/SBValue.h"
#include "lldb/lldb-defines.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/Support/Error.h"

namespace core {

namespace {

std::string Capitalize(llvm::StringRef s) {
  std::string result = s.str();
  if (!result.empty())
    result[0] = std::toupper(static_cast<unsigned char>(result[0]));
  return result;
}

}  // namespace

void BreakpointManager::PopulateExceptionBreakpoints() {
  if (lldb::SBDebugger::SupportsLanguage(lldb::eLanguageTypeC_plus_plus)) {
    exception_breakpoints.emplace_back(m_context, "cpp_catch", "C++ Catch",
                                        lldb::eLanguageTypeC_plus_plus, eExceptionKindCatch);
    exception_breakpoints.emplace_back(m_context, "cpp_throw", "C++ Throw",
                                        lldb::eLanguageTypeC_plus_plus, eExceptionKindThrow);
  }

  // Besides the hardcoded C++ case above, try to find any other languages
  // that support exception breakpoints using the SB API.
  for (int raw_lang = lldb::eLanguageTypeUnknown; raw_lang < lldb::eNumLanguageTypes; ++raw_lang) {
    lldb::LanguageType lang = static_cast<lldb::LanguageType>(raw_lang);

    if (lldb::SBLanguageRuntime::LanguageIsCFamily(lang))
      continue;
    if (!lldb::SBDebugger::SupportsLanguage(lang))
      continue;

    const char *name = lldb::SBLanguageRuntime::GetNameForLanguageType(lang);
    if (!name)
      continue;
    std::string raw_lang_name = name;
    std::string capitalized_lang_name = Capitalize(name);

    if (lldb::SBLanguageRuntime::SupportsExceptionBreakpointsOnThrow(lang)) {
      const char *raw_throw_keyword = lldb::SBLanguageRuntime::GetThrowKeywordForLanguage(lang);
      std::string throw_keyword = raw_throw_keyword ? raw_throw_keyword : "throw";
      exception_breakpoints.emplace_back(m_context, raw_lang_name + "_" + throw_keyword,
                                          capitalized_lang_name + " " + Capitalize(throw_keyword),
                                          lang, eExceptionKindThrow);
    }

    if (lldb::SBLanguageRuntime::SupportsExceptionBreakpointsOnCatch(lang)) {
      const char *raw_catch_keyword = lldb::SBLanguageRuntime::GetCatchKeywordForLanguage(lang);
      std::string catch_keyword = raw_catch_keyword ? raw_catch_keyword : "catch";
      exception_breakpoints.emplace_back(m_context, raw_lang_name + "_" + catch_keyword,
                                          capitalized_lang_name + " " + Capitalize(catch_keyword),
                                          lang, eExceptionKindCatch);
    }
  }
}

ExceptionBreakpoint *BreakpointManager::GetExceptionBreakpoint(llvm::StringRef filter) {
  for (auto &bp : exception_breakpoints) {
    if (bp.GetFilter() == filter)
      return &bp;
  }
  return nullptr;
}

ExceptionBreakpoint *BreakpointManager::GetExceptionBreakpoint(const lldb::break_id_t bp_id) {
  for (auto &bp : exception_breakpoints) {
    if (bp.GetID() == bp_id)
      return &bp;
  }
  return nullptr;
}

ExceptionBreakpoint *BreakpointManager::GetExceptionBPFromStopReason(lldb::SBThread &thread) {
  const auto num = thread.GetStopReasonDataCount();
  ExceptionBreakpoint *exc_bp = nullptr;
  for (size_t i = 0; i < num; i += 2) {
    lldb::break_id_t bp_id = thread.GetStopReasonDataAtIndex(i);
    exc_bp = GetExceptionBreakpoint(bp_id);
    if (exc_bp == nullptr)
      return nullptr;
  }
  return exc_bp;
}

std::vector<protocol::Breakpoint> BreakpointManager::SetSourceBreakpoints(
    const protocol::Source &source,
    const std::optional<std::vector<protocol::SourceBreakpoint>> &breakpoints) {
  lldb::SBMutex lock = m_context.GetAPIMutex();
  std::lock_guard<lldb::SBMutex> guard(lock);

  std::vector<protocol::Breakpoint> response_breakpoints;
  if (source.sourceReference) {
    // Breakpoint set by assembly source.
    auto &existing_breakpoints = m_source_assembly_breakpoints[*source.sourceReference];
    response_breakpoints = SetSourceBreakpoints(source, breakpoints, existing_breakpoints);
  } else {
    // Breakpoint set by a regular source file.
    const auto path = source.path.value_or("");
    auto &existing_breakpoints = m_source_breakpoints[path];
    response_breakpoints = SetSourceBreakpoints(source, breakpoints, existing_breakpoints);
  }

  return response_breakpoints;
}

std::vector<protocol::Breakpoint> BreakpointManager::SetSourceBreakpoints(
    const protocol::Source &source,
    const std::optional<std::vector<protocol::SourceBreakpoint>> &breakpoints,
    SourceBreakpointMap &existing_breakpoints) {
  std::vector<protocol::Breakpoint> response_breakpoints;

  SourceBreakpointMap request_breakpoints;
  if (breakpoints) {
    for (const auto &bp : *breakpoints) {
      SourceBreakpoint src_bp(m_context, bp);
      std::pair<uint32_t, uint32_t> bp_pos(src_bp.GetLine(), src_bp.GetColumn());
      request_breakpoints.try_emplace(bp_pos, src_bp);

      const auto [iv, inserted] = existing_breakpoints.try_emplace(bp_pos, src_bp);
      // We check if this breakpoint already exists to update it.
      if (inserted) {
        if (llvm::Error error = iv->second.SetBreakpoint(source)) {
          protocol::Breakpoint invalid_breakpoint;
          invalid_breakpoint.message = llvm::toString(std::move(error));
          invalid_breakpoint.verified = false;
          response_breakpoints.push_back(std::move(invalid_breakpoint));
          existing_breakpoints.erase(iv);
          continue;
        }
      } else {
        iv->second.UpdateBreakpoint(src_bp);
      }

      protocol::Breakpoint response_breakpoint = iv->second.ToProtocolBreakpoint();

      if (!response_breakpoint.source)
        response_breakpoint.source = source;
      if (!response_breakpoint.line && src_bp.GetLine() != LLDB_INVALID_LINE_NUMBER)
        response_breakpoint.line = src_bp.GetLine();
      if (!response_breakpoint.column && src_bp.GetColumn() != LLDB_INVALID_COLUMN_NUMBER)
        response_breakpoint.column = src_bp.GetColumn();
      response_breakpoints.push_back(std::move(response_breakpoint));
    }
  }

  // Delete any breakpoints in this source file that aren't in the
  // request_bps set. There is no call to remove breakpoints other than
  // calling this function with a smaller or empty "breakpoints" list.
  for (auto it = existing_breakpoints.begin(); it != existing_breakpoints.end();) {
    auto request_pos = request_breakpoints.find(it->first);
    if (request_pos == request_breakpoints.end()) {
      // This breakpoint no longer exists in this source file, delete it
      m_context.Target().BreakpointDelete(it->second.GetID());
      it = existing_breakpoints.erase(it);
    } else {
      ++it;
    }
  }

  return response_breakpoints;
}

namespace {

std::vector<std::pair<uint32_t, uint32_t>> GetSourceBreakpointLocations(
    lldb::SBTarget &target, const std::string &path, uint32_t start_line, uint32_t start_column, uint32_t end_line,
    uint32_t end_column) {
  std::vector<std::pair<uint32_t, uint32_t>> locations;
  lldb::SBFileSpec file_spec(path.c_str(), true);
  lldb::SBSymbolContextList compile_units = target.FindCompileUnits(file_spec);

  for (uint32_t c_idx = 0, c_limit = compile_units.GetSize(); c_idx < c_limit; ++c_idx) {
    const lldb::SBCompileUnit &compile_unit = compile_units.GetContextAtIndex(c_idx).GetCompileUnit();
    if (!compile_unit.IsValid())
      continue;
    lldb::SBFileSpec primary_file_spec = compile_unit.GetFileSpec();

    // Go through the line table and find all matching lines / columns
    for (uint32_t l_idx = 0, l_limit = compile_unit.GetNumLineEntries(); l_idx < l_limit; ++l_idx) {
      lldb::SBLineEntry line_entry = compile_unit.GetLineEntryAtIndex(l_idx);

      // Filter by line / column
      uint32_t line = line_entry.GetLine();
      if (line < start_line || line > end_line)
        continue;
      uint32_t column = line_entry.GetColumn();
      if (column == LLDB_INVALID_COLUMN_NUMBER)
        continue;
      if (line == start_line && column < start_column)
        continue;
      if (line == end_line && column > end_column)
        continue;

      // Make sure we are in the right file.
      // We might have a match on line & column range and still
      // be in the wrong file, e.g. for included files.
      // Given that the involved pointers point into LLDB's string pool,
      // we can directly compare the `const char*` pointers.
      if (line_entry.GetFileSpec().GetFilename() != primary_file_spec.GetFilename() ||
          line_entry.GetFileSpec().GetDirectory() != primary_file_spec.GetDirectory())
        continue;

      locations.emplace_back(line, column);
    }
  }

  return locations;
}

std::vector<std::pair<uint32_t, uint32_t>> GetAssemblyBreakpointLocations(lldb::SBTarget &target,
                                                                          int64_t source_reference,
                                                                          uint32_t start_line, uint32_t end_line) {
  std::vector<std::pair<uint32_t, uint32_t>> locations;
  lldb::SBAddress address(source_reference, target);
  if (!address.IsValid())
    return locations;

  lldb::SBSymbol symbol = address.GetSymbol();
  if (!symbol.IsValid())
    return locations;

  // start_line is relative to the symbol's start address.
  lldb::SBInstructionList insts = symbol.GetInstructions(target);
  if (insts.GetSize() > (start_line - 1))
    locations.reserve(insts.GetSize() - (start_line - 1));
  for (uint32_t i = start_line - 1; i < insts.GetSize() && i <= (end_line - 1); ++i) {
    locations.emplace_back(i, 1);
  }

  return locations;
}

}  // namespace

protocol::BreakpointLocationsResponseBody BreakpointManager::GetBreakpointLocations(
    const protocol::BreakpointLocationsArguments &args) {
  lldb::SBMutex lock = m_context.GetAPIMutex();
  std::lock_guard<lldb::SBMutex> guard(lock);

  uint32_t start_line = args.line;
  uint32_t start_column = args.column.value_or(LLDB_INVALID_COLUMN_NUMBER);
  uint32_t end_line = args.endLine.value_or(start_line);
  uint32_t end_column = args.endColumn.value_or(std::numeric_limits<uint32_t>::max());

  // Find all relevant lines & columns.
  std::vector<std::pair<uint32_t, uint32_t>> locations;
  if (args.source.sourceReference) {
    locations = GetAssemblyBreakpointLocations(m_context.Target(), *args.source.sourceReference, start_line, end_line);
  } else {
    std::string path = args.source.path.value_or("");
    locations = GetSourceBreakpointLocations(m_context.Target(), path, start_line, start_column, end_line, end_column);
  }

  // The line entries are sorted by addresses, but we must return the list
  // ordered by line / column position.
  std::sort(locations.begin(), locations.end());
  locations.erase(llvm::unique(locations), locations.end());

  std::vector<protocol::BreakpointLocation> breakpoint_locations;
  for (auto &l : locations)
    breakpoint_locations.push_back({l.first, l.second, std::nullopt, std::nullopt});

  return protocol::BreakpointLocationsResponseBody{/*breakpoints=*/std::move(breakpoint_locations)};
}

protocol::SetInstructionBreakpointsResponseBody BreakpointManager::SetInstructionBreakpoints(
    const protocol::SetInstructionBreakpointsArguments &args) {
  lldb::SBMutex lock = m_context.GetAPIMutex();
  std::lock_guard<lldb::SBMutex> guard(lock);

  std::vector<protocol::Breakpoint> response_breakpoints;

  // Disable any instruction breakpoints that aren't in this request.
  // There is no call to remove instruction breakpoints other than calling this
  // function with a smaller or empty "breakpoints" list.
  llvm::DenseSet<lldb::addr_t> seen(llvm::from_range, llvm::make_first_range(instruction_breakpoints));

  for (const auto &bp : args.breakpoints) {
    // Read instruction breakpoint request.
    InstructionBreakpoint inst_bp(m_context, bp);
    const auto [iv, inserted] =
        instruction_breakpoints.try_emplace(inst_bp.GetInstructionAddressReference(), m_context, bp);
    if (inserted)
      iv->second.SetBreakpoint();
    else
      iv->second.UpdateBreakpoint(inst_bp);
    response_breakpoints.push_back(iv->second.ToProtocolBreakpoint());
    seen.erase(inst_bp.GetInstructionAddressReference());
  }

  for (const auto &addr : seen) {
    auto inst_bp = instruction_breakpoints.find(addr);
    if (inst_bp == instruction_breakpoints.end())
      continue;
    m_context.Target().BreakpointDelete(inst_bp->second.GetID());
    instruction_breakpoints.erase(addr);
  }

  return protocol::SetInstructionBreakpointsResponseBody{std::move(response_breakpoints)};
}

protocol::SetDataBreakpointsResponseBody BreakpointManager::SetDataBreakpoints(
    const protocol::SetDataBreakpointsArguments &args) {
  lldb::SBMutex lock = m_context.GetAPIMutex();
  std::lock_guard<lldb::SBMutex> guard(lock);

  std::vector<protocol::Breakpoint> response_breakpoints;

  m_context.Target().DeleteAllWatchpoints();
  std::vector<Watchpoint> watchpoints;
  for (const auto &bp : args.breakpoints)
    watchpoints.emplace_back(m_context, bp);

  // If two watchpoints start at the same address, the latter overwrite the
  // former. So, we only enable those at first-seen addresses when iterating
  // backward.
  std::set<lldb::addr_t> addresses;
  for (auto iter = watchpoints.rbegin(); iter != watchpoints.rend(); ++iter) {
    if (addresses.count(iter->GetAddress()) == 0) {
      iter->SetWatchpoint();
      addresses.insert(iter->GetAddress());
    }
  }
  for (auto wp : watchpoints)
    response_breakpoints.push_back(wp.ToProtocolBreakpoint());

  return protocol::SetDataBreakpointsResponseBody{std::move(response_breakpoints)};
}

namespace {

bool IsReadableOrWritable(lldb::SBTarget &target, lldb::addr_t load_addr) {
  if (!lldb::SBAddress(load_addr, target).IsValid())
    return false;
  lldb::SBMemoryRegionInfo region;
  lldb::SBError err = target.GetProcess().GetMemoryRegionInfo(load_addr, region);
  // Only lldb-server supports "qMemoryRegionInfo". So, don't fail this
  // request if SBProcess::GetMemoryRegionInfo returns error.
  if (err.Success() && !(region.IsReadable() || region.IsWritable()))
    return false;
  return true;
}

}  // namespace

llvm::Expected<protocol::DataBreakpointInfoResponseBody> BreakpointManager::GetDataBreakpointInfo(
    const protocol::DataBreakpointInfoArguments &args) {
  lldb::SBMutex lock = m_context.GetAPIMutex();
  std::lock_guard<lldb::SBMutex> guard(lock);

  protocol::DataBreakpointInfoResponseBody response;
  lldb::SBValue variable = m_context.Data().variables.FindVariable(args.variablesReference.value_or(0), args.name);
  std::string addr, size;

  bool is_data_ok = true;
  if (variable.IsValid()) {
    lldb::addr_t load_addr = variable.GetLoadAddress();
    size_t byte_size = variable.GetByteSize();
    if (load_addr == LLDB_INVALID_ADDRESS) {
      is_data_ok = false;
      response.description = "does not exist in memory, its location is " + std::string(variable.GetLocation());
    } else if (byte_size == 0) {
      is_data_ok = false;
      response.description = "variable size is 0";
    } else {
      addr = llvm::utohexstr(load_addr);
      size = llvm::utostr(byte_size);
    }
  } else if (lldb::SBFrame frame = m_context.GetLLDBFrame(args.frameId);
             args.variablesReference.value_or(0) == 0 && frame.IsValid()) {
    lldb::SBValue value = frame.EvaluateExpression(args.name.c_str());
    if (value.GetError().Fail()) {
      lldb::SBError error = value.GetError();
      const char *error_cstr = error.GetCString();
      is_data_ok = false;
      response.description = error_cstr && error_cstr[0] ? std::string(error_cstr) : "evaluation failed";
    } else {
      uint64_t load_addr = value.GetValueAsUnsigned();
      lldb::SBData data = value.GetPointeeData();
      if (data.IsValid()) {
        size = llvm::utostr(data.GetByteSize());
        addr = llvm::utohexstr(load_addr);
        if (!IsReadableOrWritable(m_context.Target(), load_addr)) {
          is_data_ok = false;
          response.description = "memory region for address " + addr + " has no read or write permissions";
        }
      } else {
        is_data_ok = false;
        response.description = "unable to get byte size for expression: " + args.name;
      }
    }
  } else if (args.asAddress) {
    size = llvm::utostr(args.bytes.value_or(m_context.Target().GetAddressByteSize()));
    lldb::addr_t load_addr = LLDB_INVALID_ADDRESS;
    if (llvm::StringRef(args.name).getAsInteger<lldb::addr_t>(0, load_addr))
      return llvm::make_error<dap::DAPError>(args.name + " is not a valid address", llvm::inconvertibleErrorCode(),
                                             false);
    addr = llvm::utohexstr(load_addr);
    if (!IsReadableOrWritable(m_context.Target(), load_addr))
      return llvm::make_error<dap::DAPError>("memory region for address " + addr + " has no read or write permissions",
                                             llvm::inconvertibleErrorCode(), false);
  } else {
    is_data_ok = false;
    response.description = "variable not found: " + args.name;
  }

  if (is_data_ok) {
    response.dataId = addr + "/" + size;
    response.accessTypes = {protocol::eDataBreakpointAccessTypeRead, protocol::eDataBreakpointAccessTypeWrite,
                            protocol::eDataBreakpointAccessTypeReadWrite};
    if (args.asAddress)
      response.description = size + " bytes at " + addr;
    else
      response.description = size + " bytes at " + addr + " " + args.name;
  }

  return response;
}

}  // namespace core
