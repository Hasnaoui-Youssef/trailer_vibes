#ifndef TRAILER_CORE_COMPONENTS_EXCEPTION_BREAKPOINT_HPP_
#define TRAILER_CORE_COMPONENTS_EXCEPTION_BREAKPOINT_HPP_

#include <string>
#include <utility>

#include "core/debug_context.hpp"
#include "dap/protocol/protocol_types.hpp"
#include "lldb/API/SBBreakpoint.h"
#include "lldb/lldb-enumerations.h"
#include "llvm/ADT/StringRef.h"

namespace core {

enum ExceptionKind : unsigned {
  eExceptionKindCatch,
  eExceptionKindThrow,
};

class ExceptionBreakpoint {
public:
  ExceptionBreakpoint(DebugContext &context, std::string f, std::string l,
                       lldb::LanguageType lang, ExceptionKind kind)
      : m_context(context), m_filter(std::move(f)), m_label(std::move(l)),
        m_language(lang), m_kind(kind), m_bp() {}

  protocol::Breakpoint SetBreakpoint() { return SetBreakpoint(""); };
  protocol::Breakpoint SetBreakpoint(llvm::StringRef condition);
  void ClearBreakpoint();

  lldb::break_id_t GetID() const { return m_bp.GetID(); }
  llvm::StringRef GetFilter() const { return m_filter; }
  llvm::StringRef GetLabel() const { return m_label; }

  static constexpr bool kDefaultValue = false;
  static constexpr const char *kDAPBreakpointLabel = "dap";

protected:
  DebugContext &m_context;
  std::string m_filter;
  std::string m_label;
  lldb::LanguageType m_language;
  ExceptionKind m_kind;
  lldb::SBBreakpoint m_bp;
};

/// Creates a `protocol::ExceptionBreakpointsFilter` describing `bp`, for
/// advertising in the adapter's capabilities.
protocol::ExceptionBreakpointsFilter CreateExceptionBreakpointFilter(const ExceptionBreakpoint &bp);

}  // namespace core

#endif  // TRAILER_CORE_COMPONENTS_EXCEPTION_BREAKPOINT_HPP_
