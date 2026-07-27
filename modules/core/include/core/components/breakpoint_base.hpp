#ifndef TRAILER_CORE_COMPONENTS_BREAKPOINT_BASE_HPP_
#define TRAILER_CORE_COMPONENTS_BREAKPOINT_BASE_HPP_

#include <optional>
#include <string>

#include "core/debug_context.hpp"
#include "dap/protocol/protocol_types.hpp"

namespace core {

class BreakpointBase {
public:
  explicit BreakpointBase(DebugContext &context) : m_context(context) {}
  BreakpointBase(DebugContext &context, const std::optional<std::string> &condition,
                 const std::optional<std::string> &hit_condition);
  virtual ~BreakpointBase() = default;

  virtual void SetCondition() = 0;
  virtual void SetHitCondition() = 0;
  virtual protocol::Breakpoint ToProtocolBreakpoint() = 0;

  void UpdateBreakpoint(const BreakpointBase &request_bp);

  static constexpr const char *kDAPBreakpointLabel = "dap";

protected:
  DebugContext &m_context;

  /// An optional expression for conditional breakpoints.
  std::string m_condition;

  /// An optional expression that controls how many hits are ignored.
  std::string m_hit_condition;
};

}  // namespace core

#endif  // TRAILER_CORE_COMPONENTS_BREAKPOINT_BASE_HPP_
