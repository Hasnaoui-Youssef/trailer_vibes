//===-- data_manager.hpp ---------------------------------------------------===//
//
// The data component (see CLAUDE.md's DebugContext architecture): variables/
// scopes/evaluate support and the REPL-mode heuristic. Named "Data" rather
// than "Inspection" because it also owns core registers (exposed through
// the same Variables registry, via the eScopeKindRegisters scope) - not
// just source-level variables. Carved out of DebugService - see
// PROJECT_STATUS.md.
//
// frame_format/thread_format stay on DebugService for now even though the
// original decomposition table put them here: they're read by handlers
// spanning Target/lifecycle (configurationDone), thread listing and this
// component's own stack-trace formatting, and DebugService's
// SetConfiguration (a TargetManager-domain method not carved yet) is what
// sets them - moving them now would mean picking an owner before
// TargetManager exists. Revisit when TargetManager is carved.
//
//===----------------------------------------------------------------------===//

#ifndef TRAILER_CORE_COMPONENTS_DATA_MANAGER_HPP_
#define TRAILER_CORE_COMPONENTS_DATA_MANAGER_HPP_

#include <map>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "dap/protocol/protocol_types.hpp"
#include "lldb/API/SBFrame.h"
#include "lldb/API/SBValue.h"
#include "lldb/API/SBValueList.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/StringRef.h"

namespace core {

namespace protocol = dap::protocol;

class DebugContext;

enum ScopeKind : unsigned { eScopeKindLocals, eScopeKindGlobals, eScopeKindRegisters };

/// Creates a `protocol::Scope` struct.
protocol::Scope CreateScope(ScopeKind kind, int64_t variablesReference, int64_t namedVariables,
                            bool expensive);

/// Creates a `protocol::Variable` describing `v`. `var_ref` is the
/// variableReference already assigned to `v` (0 if it has none - see
/// Variables::InsertVariable). See core::VariableDescription for what each
/// of the other parameters controls.
protocol::Variable CreateVariable(lldb::SBValue v, int64_t var_ref, bool format_hex, bool auto_variable_summaries,
                                  bool synthetic_child_debugging, bool is_name_duplicated,
                                  std::optional<llvm::StringRef> custom_name = {});

struct ScopeData {
  ScopeKind kind;
  lldb::SBValueList scope;
};

/// Stores the three scope variable lists for a single stack frame.
struct FrameScopes {
  lldb::SBValueList locals;
  lldb::SBValueList globals;
  lldb::SBValueList registers;

  /// Returns a pointer to the scope corresponding to the given kind.
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

/// Tracks every variable reference handed out to the client (scopes,
/// expandable values, REPL-persisted values) for the current stop, plus
/// core registers (as the eScopeKindRegisters scope of each frame).
struct Variables {
  /// Check if \p var_ref points to a variable that should persist for the
  /// entire duration of the debug session, e.g. repl expandable variables
  static bool IsPermanentVariableReference(int64_t var_ref);

  /// \return a new variableReference.
  /// Specify is_permanent as true for variable that should persist entire
  /// debug session.
  int64_t GetNewVariableReference(bool is_permanent);

  /// \return the expandable variable corresponding with variableReference
  /// value of \p value.
  /// If \p var_ref is invalid an empty SBValue is returned.
  lldb::SBValue GetVariable(int64_t var_ref) const;

  lldb::SBValueList *GetScope(uint64_t dap_frame_id, ScopeKind kind);

  /// Insert a new \p variable.
  /// \return variableReference assigned to this expandable variable.
  int64_t InsertVariable(lldb::SBValue variable, bool is_permanent);

  std::optional<ScopeData> GetTopLevelScope(int64_t variablesReference);

  lldb::SBValue FindVariable(uint64_t variablesReference, llvm::StringRef name);

  /// Initialize a frame if it hasn't been already, otherwise do nothing
  std::vector<protocol::Scope> CreateScopes(uint64_t dap_frame_id, lldb::SBFrame &frame);

  /// Clear all scope variables and non-permanent expandable variables.
  void Clear();

private:
  /// Variable reference start index of temporary variables.
  static constexpr int64_t TemporaryVariableStartIndex = 1;

  /// Variable reference start index of permanent expandable variable.
  static constexpr int64_t PermanentVariableStartIndex = (1ll << 32);

  int64_t m_next_permanent_var_ref{PermanentVariableStartIndex};
  int64_t m_next_temporary_var_ref{TemporaryVariableStartIndex};

  // Variable Reference,                 dap_frame_id
  std::map<int64_t, std::pair<ScopeKind, uint64_t>> m_scope_kinds;

  /// Variables that are alive in this stop state.
  /// Will be cleared when debuggee resumes.
  llvm::DenseMap<int64_t, lldb::SBValue> m_referencedvariables;

  /// Variables that persist across entire debug session.
  /// These are the variables evaluated from debug console REPL.
  llvm::DenseMap<int64_t, lldb::SBValue> m_referencedpermanent_variables;

  /// Key = dap_frame_id (encodes both thread index ID and frame ID)
  /// Value = scopes for the frame (locals, globals, registers)
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

  /// Decides whether `expression` (from the debug console REPL) should be
  /// run as an LLDB command or evaluated as a variable/expression. May
  /// strip a leading command-escape prefix from `expression` in place.
  ReplMode DetectReplMode(lldb::SBFrame &frame, std::string &expression, bool partial_expression);

private:
  DebugContext &m_context;
};

}  // namespace core

#endif  // TRAILER_CORE_COMPONENTS_DATA_MANAGER_HPP_
