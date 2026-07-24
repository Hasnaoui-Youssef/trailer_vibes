//===-- function_breakpoint.hpp -------------------------------------------===//
//
// Relocated from debug_service/function_breakpoint.hpp - see
// breakpoint_base.hpp.
//
//===----------------------------------------------------------------------===//

#ifndef TRAILER_CORE_COMPONENTS_FUNCTION_BREAKPOINT_HPP_
#define TRAILER_CORE_COMPONENTS_FUNCTION_BREAKPOINT_HPP_

#include "core/components/breakpoint.hpp"

namespace core {

class FunctionBreakpoint : public Breakpoint {
public:
  FunctionBreakpoint(DebugContext &context, const protocol::FunctionBreakpoint &breakpoint);

  /// Set this breakpoint in LLDB as a new breakpoint.
  void SetBreakpoint();

  llvm::StringRef GetFunctionName() const { return m_function_name; }

protected:
  std::string m_function_name;
};

}  // namespace core

#endif  // TRAILER_CORE_COMPONENTS_FUNCTION_BREAKPOINT_HPP_
