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

  llvm::Error SetBreakpoint(const protocol::Source &source);
  void UpdateBreakpoint(const SourceBreakpoint &request_bp);

  void SetLogMessage();
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
  llvm::Error CreatePathBreakpoint(const protocol::Source &source);
  llvm::Error
  CreateAssemblyBreakpointWithSourceReference(int64_t source_reference);
  llvm::Error CreateAssemblyBreakpointWithPersistenceData(
      const protocol::PersistenceData &persistence_data);

  struct LogMessagePart {
    LogMessagePart(llvm::StringRef text, bool is_expr)
        : text(text), is_expr(is_expr) {}
    std::string text;
    bool is_expr;
  };
  std::string m_log_message;
  std::vector<LogMessagePart> m_log_message_parts;

  uint32_t m_line;
  uint32_t m_column;
};

}  // namespace core

#endif  // TRAILER_CORE_COMPONENTS_SOURCE_BREAKPOINT_HPP_
