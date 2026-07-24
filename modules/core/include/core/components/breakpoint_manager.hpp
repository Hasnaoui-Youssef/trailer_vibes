//===-- breakpoint_manager.hpp --------------------------------------------===//
//
// The breakpoint component (see CLAUDE.md's DebugContext architecture):
// composite owner of every breakpoint kind (source/function/instruction/
// exception/watchpoint), carved out of debug_service::DebugService's
// breakpoint surface - see PROJECT_STATUS.md. Backed by LldbProvider only
// via DebugContext (the mediator); has no reference to any other component
// or to debug_service.
//
//===----------------------------------------------------------------------===//

#ifndef TRAILER_CORE_COMPONENTS_BREAKPOINT_MANAGER_HPP_
#define TRAILER_CORE_COMPONENTS_BREAKPOINT_MANAGER_HPP_

#include <map>
#include <optional>
#include <utility>
#include <vector>

#include "core/components/exception_breakpoint.hpp"
#include "core/components/function_breakpoint.hpp"
#include "core/components/instruction_breakpoint.hpp"
#include "core/components/source_breakpoint.hpp"
#include "core/debug_context.hpp"
#include "lldb/API/SBThread.h"
#include "lldb/lldb-types.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/StringMap.h"
#include "llvm/Support/Threading.h"

namespace core {

using SourceBreakpointMap = std::map<std::pair<uint32_t, uint32_t>, SourceBreakpoint>;
using FunctionBreakpointMap = llvm::StringMap<FunctionBreakpoint>;
using InstructionBreakpointMap = llvm::DenseMap<lldb::addr_t, InstructionBreakpoint>;

class BreakpointManager {
public:
  explicit BreakpointManager(DebugContext &context) : m_context(context) {}

  BreakpointManager(const BreakpointManager &) = delete;
  BreakpointManager &operator=(const BreakpointManager &) = delete;

  /// Sets the given protocol `breakpoints` in the given `source`, while
  /// removing any existing breakpoints in the given source if they are
  /// not in `breakpoints`. \return the breakpoints that were set.
  std::vector<protocol::Breakpoint> SetSourceBreakpoints(
      const protocol::Source &source,
      const std::optional<std::vector<protocol::SourceBreakpoint>> &breakpoints);

  void PopulateExceptionBreakpoints();
  ExceptionBreakpoint *GetExceptionBreakpoint(llvm::StringRef filter);
  ExceptionBreakpoint *GetExceptionBreakpoint(lldb::break_id_t bp_id);
  ExceptionBreakpoint *GetExceptionBPFromStopReason(lldb::SBThread &thread);

  DebugContext &Context() { return m_context; }

  // Public collections, mirroring today's DebugService fields exactly -
  // handlers manipulate these directly (try_emplace/erase/iterate) rather
  // than through narrower accessors, matching the existing call sites this
  // carve must not disturb.
  FunctionBreakpointMap function_breakpoints;
  InstructionBreakpointMap instruction_breakpoints;
  std::vector<ExceptionBreakpoint> exception_breakpoints;
  llvm::once_flag init_exception_breakpoints_flag;

private:
  std::vector<protocol::Breakpoint> SetSourceBreakpoints(
      const protocol::Source &source,
      const std::optional<std::vector<protocol::SourceBreakpoint>> &breakpoints,
      SourceBreakpointMap &existing_breakpoints);

  DebugContext &m_context;
  llvm::StringMap<SourceBreakpointMap> m_source_breakpoints;
  llvm::DenseMap<int64_t, SourceBreakpointMap> m_source_assembly_breakpoints;
};

}  // namespace core

#endif  // TRAILER_CORE_COMPONENTS_BREAKPOINT_MANAGER_HPP_
