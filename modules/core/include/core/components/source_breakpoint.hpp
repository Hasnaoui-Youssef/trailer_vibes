//===-- source_breakpoint.hpp ----------------------------------------------===//
//
// Relocated from debug_service/source_breakpoint.hpp - see
// breakpoint_base.hpp.
//
//===----------------------------------------------------------------------===//

#ifndef TRAILER_CORE_COMPONENTS_SOURCE_BREAKPOINT_HPP_
#define TRAILER_CORE_COMPONENTS_SOURCE_BREAKPOINT_HPP_

#include <cstdint>
#include <string>
#include <vector>

#include "core/components/breakpoint.hpp"
#include "dap/protocol/dap_types.hpp"
#include "lldb/API/SBError.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/Error.h"

namespace core {

class SourceBreakpoint : public Breakpoint {
public:
  SourceBreakpoint(DebugContext &context, const protocol::SourceBreakpoint &breakpoint);

  // Set this breakpoint in LLDB as a new breakpoint
  llvm::Error SetBreakpoint(const protocol::Source &source);
  void UpdateBreakpoint(const SourceBreakpoint &request_bp);

  void SetLogMessage();
  // Format \param text and return formatted text in \param formatted.
  // \return any formatting failures.
  lldb::SBError FormatLogText(llvm::StringRef text, std::string &formatted);
  lldb::SBError AppendLogMessagePart(llvm::StringRef part, bool is_expr);
  void NotifyLogMessageError(llvm::StringRef error);

  static bool BreakpointHitCallback(void *baton, lldb::SBProcess &process,
                                    lldb::SBThread &thread,
                                    lldb::SBBreakpointLocation &location);

  inline bool operator<(const SourceBreakpoint &rhs) {
    if (m_line == rhs.m_line)
      return m_column < rhs.m_column;
    return m_line < rhs.m_line;
  }

  uint32_t GetLine() const { return m_line; }
  uint32_t GetColumn() const { return m_column; }

protected:
  void CreatePathBreakpoint(const protocol::Source &source);
  llvm::Error
  CreateAssemblyBreakpointWithSourceReference(int64_t source_reference);
  llvm::Error CreateAssemblyBreakpointWithPersistenceData(
      const protocol::PersistenceData &persistence_data);

  // logMessage part can be either a raw text or an expression.
  struct LogMessagePart {
    LogMessagePart(llvm::StringRef text, bool is_expr)
        : text(text), is_expr(is_expr) {}
    std::string text;
    bool is_expr;
  };
  // If this attribute exists and is non-empty, the backend must not 'break'
  // (stop) but log the message instead. Expressions within {} are
  // interpolated.
  std::string m_log_message;
  std::vector<LogMessagePart> m_log_message_parts;

  uint32_t m_line;   ///< The source line of the breakpoint or logpoint
  uint32_t m_column; ///< An optional source column of the breakpoint
};

}  // namespace core

#endif  // TRAILER_CORE_COMPONENTS_SOURCE_BREAKPOINT_HPP_
