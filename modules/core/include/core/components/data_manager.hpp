#ifndef TRAILER_CORE_COMPONENTS_DATA_MANAGER_HPP_
#define TRAILER_CORE_COMPONENTS_DATA_MANAGER_HPP_

#include <map>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "dap/protocol/protocol_requests.hpp"
#include "dap/protocol/protocol_types.hpp"
#include "lldb/API/SBFormat.h"
#include "lldb/API/SBFrame.h"
#include "lldb/API/SBValue.h"
#include "lldb/API/SBValueList.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/Error.h"

namespace core {

namespace protocol = dap::protocol;

class DebugContext;

enum ScopeKind : unsigned { eScopeKindLocals, eScopeKindGlobals, eScopeKindRegisters };

protocol::Scope CreateScope(ScopeKind kind, int64_t variablesReference, int64_t namedVariables,
                            bool expensive);

protocol::Variable CreateVariable(lldb::SBValue v, int64_t var_ref, bool format_hex, bool auto_variable_summaries,
                                  bool synthetic_child_debugging, bool is_name_duplicated,
                                  std::optional<llvm::StringRef> custom_name = {});

struct ScopeData {
  ScopeKind kind;
  lldb::SBValueList scope;
};

struct FrameScopes {
  lldb::SBValueList locals;
  lldb::SBValueList globals;
  lldb::SBValueList registers;

  lldb::SBValueList *GetScope(ScopeKind kind) {
    switch (kind) {
    case eScopeKindLocals:
      return &locals;
    case eScopeKindGlobals:
      return &globals;
    case eScopeKindRegisters:
      return &registers;
    }
    llvm_unreachable("unknown scope kind");
  }
};

struct Variables {
  static bool IsPermanentVariableReference(int64_t var_ref);

  int64_t GetNewVariableReference(bool is_permanent);

  lldb::SBValue GetVariable(int64_t var_ref) const;

  lldb::SBValueList *GetScope(uint64_t dap_frame_id, ScopeKind kind);

  int64_t InsertVariable(lldb::SBValue variable, bool is_permanent);

  std::optional<ScopeData> GetTopLevelScope(int64_t variablesReference);

  lldb::SBValue FindVariable(uint64_t variablesReference, llvm::StringRef name);

  std::vector<protocol::Scope> CreateScopes(uint64_t dap_frame_id, lldb::SBFrame &frame);

  void Clear();

private:
  static constexpr int64_t TemporaryVariableStartIndex = 1;

  static constexpr int64_t PermanentVariableStartIndex = (1ll << 32);

  int64_t m_next_permanent_var_ref{PermanentVariableStartIndex};
  int64_t m_next_temporary_var_ref{TemporaryVariableStartIndex};

  std::map<int64_t, std::pair<ScopeKind, uint64_t>> m_scope_kinds;

  llvm::DenseMap<int64_t, lldb::SBValue> m_referencedvariables;

  llvm::DenseMap<int64_t, lldb::SBValue> m_referencedpermanent_variables;

  std::map<uint64_t, FrameScopes> m_frames;
};

enum class ReplMode { Variable = 0, Command, Auto };

class DataManager {
public:
  explicit DataManager(DebugContext &context) : m_context(context) {}

  DataManager(const DataManager &) = delete;
  DataManager &operator=(const DataManager &) = delete;

  Variables variables;
  ReplMode repl_mode = ReplMode::Auto;
  std::string last_nonempty_var_expression;
  lldb::SBFormat frame_format;
  lldb::SBFormat thread_format;

  ReplMode DetectReplMode(lldb::SBFrame &frame, std::string &expression, bool partial_expression);

  void SetFrameFormat(llvm::StringRef format);
  void SetThreadFormat(llvm::StringRef format);

  llvm::Expected<protocol::StackTraceResponseBody> GetStackTraceRequest(
      const protocol::StackTraceArguments &args);

  protocol::ScopesResponseBody GetScopesRequest(const protocol::ScopesArguments &args);
  protocol::VariablesResponseBody GetVariablesRequest(const protocol::VariablesArguments &args);
  llvm::Expected<protocol::SetVariableResponseBody> SetVariableRequest(
      const protocol::SetVariableArguments &args);
  llvm::Expected<protocol::EvaluateResponseBody> GetEvaluateRequest(
      const protocol::EvaluateArguments &args);
  protocol::CompletionsResponseBody GetCompletionsRequest(const protocol::CompletionsArguments &args);
  llvm::Expected<protocol::LocationsResponseBody> GetLocationsRequest(
      const protocol::LocationsArguments &args);

private:
  DebugContext &m_context;
};

}  // namespace core

#endif  // TRAILER_CORE_COMPONENTS_DATA_MANAGER_HPP_
