//===-- breakpoint_base.hpp ----------------------------------------------===//
//
// Relocated from debug_service/breakpoint_base.hpp (forked from LLVM's
// lldb-dap BreakpointBase.h, Apache-2.0 WITH LLVM-exception) as part of the
// Breakpoint carve - see PROJECT_STATUS.md. Holds core::DebugContext&
// instead of DebugService& now that breakpoints are a core component; see
// debug_context.hpp for why that's a mediator reference, not a provider
// reference.
//
//===----------------------------------------------------------------------===//

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

  /// Breakpoints in LLDB can have names added to them which are kind of like
  /// labels or categories. All breakpoints that are set through the DAP
  /// layer get sent through the various set*Breakpoint packets, and these
  /// breakpoints will be labeled with this name so if breakpoint update
  /// events come in for breakpoints that the client doesn't know about, like
  /// if a breakpoint is set manually using the debugger console, we won't
  /// report any updates on them and confuse the client. This label gets
  /// added by all of the breakpoint classes after they set breakpoints to
  /// mark a breakpoint as a DAP breakpoint. We can later check a
  /// lldb::SBBreakpoint object that comes in via LLDB breakpoint changed
  /// events and check the breakpoint by calling
  /// "bool lldb::SBBreakpoint::MatchesName(const char *)" to check if a
  /// breakpoint is one of the DAP breakpoints that we should report changes
  /// for.
  static constexpr const char *kDAPBreakpointLabel = "dap";

protected:
  /// Associated engine runtime context.
  DebugContext &m_context;

  /// An optional expression for conditional breakpoints.
  std::string m_condition;

  /// An optional expression that controls how many hits of the breakpoint are
  /// ignored. The backend is expected to interpret the expression as needed
  std::string m_hit_condition;
};

}  // namespace core

#endif  // TRAILER_CORE_COMPONENTS_BREAKPOINT_BASE_HPP_
