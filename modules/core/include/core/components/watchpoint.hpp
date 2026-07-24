//===-- watchpoint.hpp -----------------------------------------------------===//
//
// Relocated from debug_service/watchpoint.hpp - see breakpoint_base.hpp.
//
//===----------------------------------------------------------------------===//

#ifndef TRAILER_CORE_COMPONENTS_WATCHPOINT_HPP_
#define TRAILER_CORE_COMPONENTS_WATCHPOINT_HPP_

#include <cstddef>

#include "core/components/breakpoint_base.hpp"
#include "lldb/API/SBError.h"
#include "lldb/API/SBWatchpoint.h"
#include "lldb/API/SBWatchpointOptions.h"
#include "lldb/lldb-types.h"

namespace core {

class Watchpoint : public BreakpointBase {
public:
  Watchpoint(DebugContext &context, const protocol::DataBreakpoint &breakpoint);
  Watchpoint(DebugContext &context, lldb::SBWatchpoint wp) : BreakpointBase(context), m_wp(wp) {}

  void SetCondition() override;
  void SetHitCondition() override;

  protocol::Breakpoint ToProtocolBreakpoint() override;

  void SetWatchpoint();

  lldb::addr_t GetAddress() const { return m_addr; }

protected:
  lldb::addr_t m_addr;
  size_t m_size;
  lldb::SBWatchpointOptions m_options;
  /// The LLDB breakpoint associated wit this watchpoint.
  lldb::SBWatchpoint m_wp;
  lldb::SBError m_error;
};

}  // namespace core

#endif  // TRAILER_CORE_COMPONENTS_WATCHPOINT_HPP_
