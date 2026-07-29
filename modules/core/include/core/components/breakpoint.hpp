#ifndef TRAILER_CORE_COMPONENTS_BREAKPOINT_HPP_
#define TRAILER_CORE_COMPONENTS_BREAKPOINT_HPP_

#include "core/components/breakpoint_base.hpp"
#include "lldb/API/SBBreakpoint.h"

namespace core {

class Breakpoint : public BreakpointBase {
public:
  Breakpoint(DebugContext &context, const std::optional<std::string> &condition,
             const std::optional<std::string> &hit_condition)
      : BreakpointBase(context, condition, hit_condition) {}
  Breakpoint(DebugContext &context, lldb::SBBreakpoint bp) : BreakpointBase(context), m_bp(bp) {}

  lldb::break_id_t GetID() const { return m_bp.GetID(); }

  void SetCondition() override;
  void SetHitCondition() override;
  protocol::Breakpoint ToProtocolBreakpoint() override;

  bool MatchesName(const char *name);
  void SetBreakpoint();

protected:
  /// Disables locations not on a decoded instruction (e.g. a literal pool
  /// aliased onto the same line), so they aren't forced to hardware too.
  void PruneNonCodeLocations();

  /// The LLDB breakpoint associated wit this source breakpoint.
  lldb::SBBreakpoint m_bp;

  /// Set if SetBreakpoint() couldn't make m_bp hardware (e.g. no free FPB
  /// comparators); only matters if the address is actually in flash.
  std::string m_hardware_error;
};

}  // namespace core

#endif  // TRAILER_CORE_COMPONENTS_BREAKPOINT_HPP_
