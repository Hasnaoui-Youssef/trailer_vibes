#include "core/components/watchpoint.hpp"

#include <cstdint>
#include <string>

#include "lldb/API/SBTarget.h"
#include "lldb/lldb-enumerations.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/ADT/StringRef.h"

namespace core {

Watchpoint::Watchpoint(DebugContext &context, const protocol::DataBreakpoint &breakpoint)
    : BreakpointBase(context, breakpoint.condition, breakpoint.hitCondition) {
  llvm::StringRef dataId = breakpoint.dataId;
  auto [addr_str, size_str] = dataId.split('/');
  llvm::to_integer(addr_str, m_addr, 16);
  llvm::to_integer(size_str, m_size);
  m_options.SetWatchpointTypeRead(breakpoint.accessType !=
                                  protocol::eDataBreakpointAccessTypeWrite);
  if (breakpoint.accessType != protocol::eDataBreakpointAccessTypeRead)
    m_options.SetWatchpointTypeWrite(lldb::eWatchpointWriteTypeOnModify);
}

void Watchpoint::SetCondition() { m_wp.SetCondition(m_condition.c_str()); }

void Watchpoint::SetHitCondition() {
  uint64_t hitCount = 0;
  if (llvm::to_integer(m_hit_condition, hitCount))
    m_wp.SetIgnoreCount(hitCount - 1);
}

protocol::Breakpoint Watchpoint::ToProtocolBreakpoint() {
  protocol::Breakpoint breakpoint;
  if (!m_error.IsValid() || m_error.Fail()) {
    breakpoint.verified = false;
    if (m_error.Fail())
      breakpoint.message = m_error.GetCString();
  } else {
    breakpoint.verified = true;
    breakpoint.id = m_wp.GetID();
  }

  return breakpoint;
}

void Watchpoint::SetWatchpoint() {
  m_wp = m_context.Target().WatchpointCreateByAddress(m_addr, m_size, m_options,
                                                       m_error);
  if (!m_condition.empty())
    SetCondition();
  if (!m_hit_condition.empty())
    SetHitCondition();
}

}  // namespace core
