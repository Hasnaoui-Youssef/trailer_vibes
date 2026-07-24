#include "core/components/breakpoint_base.hpp"

namespace core {

BreakpointBase::BreakpointBase(DebugContext &context,
                                const std::optional<std::string> &condition,
                                const std::optional<std::string> &hit_condition)
    : m_context(context), m_condition(condition.value_or("")),
      m_hit_condition(hit_condition.value_or("")) {}

void BreakpointBase::UpdateBreakpoint(const BreakpointBase &request_bp) {
  if (m_condition != request_bp.m_condition) {
    m_condition = request_bp.m_condition;
    SetCondition();
  }
  if (m_hit_condition != request_bp.m_hit_condition) {
    m_hit_condition = request_bp.m_hit_condition;
    SetHitCondition();
  }
}

}  // namespace core
