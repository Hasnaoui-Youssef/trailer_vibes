#include "core/components/breakpoint_manager.hpp"

#include <string>

#include "lldb/API/SBDebugger.h"
#include "lldb/API/SBLanguageRuntime.h"
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

}  // namespace core
