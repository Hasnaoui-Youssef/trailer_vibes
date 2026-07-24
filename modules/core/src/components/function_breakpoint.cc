#include "core/components/function_breakpoint.hpp"

#include <mutex>

#include "lldb/API/SBMutex.h"

namespace core {

FunctionBreakpoint::FunctionBreakpoint(DebugContext &context,
                                        const protocol::FunctionBreakpoint &breakpoint)
    : Breakpoint(context, breakpoint.condition, breakpoint.hitCondition),
      m_function_name(breakpoint.name) {}

void FunctionBreakpoint::SetBreakpoint() {
  lldb::SBMutex lock = m_context.GetAPIMutex();
  std::lock_guard<lldb::SBMutex> guard(lock);

  if (m_function_name.empty())
    return;
  m_bp = m_context.Target().BreakpointCreateByName(m_function_name.c_str());
  Breakpoint::SetBreakpoint();
}

}  // namespace core
