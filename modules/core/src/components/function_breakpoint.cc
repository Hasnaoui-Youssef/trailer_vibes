#include "core/components/function_breakpoint.hpp"



namespace core {

FunctionBreakpoint::FunctionBreakpoint(DebugContext &context,
                                        const protocol::FunctionBreakpoint &breakpoint)
    : Breakpoint(context, breakpoint.condition, breakpoint.hitCondition),
      m_function_name(breakpoint.name) {}

void FunctionBreakpoint::SetBreakpoint() {
  m_context.WithTarget([&]() {
    if (m_function_name.empty())
      return;
    m_bp = m_context.Target().BreakpointCreateByName(m_function_name.c_str());
    Breakpoint::SetBreakpoint();
  });
}

}  // namespace core
