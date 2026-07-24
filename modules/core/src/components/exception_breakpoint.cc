#include "core/components/exception_breakpoint.hpp"

#include <mutex>

#include "lldb/API/SBMutex.h"
#include "lldb/API/SBTarget.h"

namespace core {

protocol::Breakpoint ExceptionBreakpoint::SetBreakpoint(llvm::StringRef condition) {
  lldb::SBMutex lock = m_context.GetAPIMutex();
  std::lock_guard<lldb::SBMutex> guard(lock);

  if (!m_bp.IsValid()) {
    m_bp = m_context.Target().BreakpointCreateForException(
        m_language, m_kind == eExceptionKindCatch,
        m_kind == eExceptionKindThrow);
    m_bp.AddName(kDAPBreakpointLabel);
  }

  m_bp.SetCondition(condition.data());

  protocol::Breakpoint breakpoint;
  breakpoint.id = m_bp.GetID();
  breakpoint.verified = m_bp.IsValid();
  return breakpoint;
}

void ExceptionBreakpoint::ClearBreakpoint() {
  if (!m_bp.IsValid())
    return;
  m_context.Target().BreakpointDelete(m_bp.GetID());
  m_bp = lldb::SBBreakpoint();
}

protocol::ExceptionBreakpointsFilter CreateExceptionBreakpointFilter(const ExceptionBreakpoint &bp) {
  protocol::ExceptionBreakpointsFilter filter;
  filter.filter = bp.GetFilter();
  filter.label = bp.GetLabel();
  filter.description = bp.GetLabel();
  filter.defaultState = ExceptionBreakpoint::kDefaultValue;
  filter.supportsCondition = true;
  return filter;
}

}  // namespace core
