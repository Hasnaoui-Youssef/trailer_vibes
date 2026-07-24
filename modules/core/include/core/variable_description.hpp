//===-- variable_description.hpp -------------------------------------===//
//
// Relocated from debug_service/json_utils.hpp (see
// project-dap-layer-fork-strategy memory): this is the SBValue-to-display
// formatting half of the original (forked) JSONUtils.h. It only ever
// touched lldb::SBValue plus plain data - no DebugService coupling - so it
// moves into core as a shared utility usable by any component (Breakpoint
// needs it for source-breakpoint log messages; Data will own the
// variables/evaluate surface that also calls this).
//
//===----------------------------------------------------------------------===//

#ifndef TRAILER_CORE_VARIABLE_DESCRIPTION_HPP_
#define TRAILER_CORE_VARIABLE_DESCRIPTION_HPP_

#include <optional>
#include <string>

#include "dap/protocol/protocol_requests.hpp"
#include "lldb/API/SBType.h"
#include "lldb/API/SBValue.h"
#include "llvm/ADT/StringRef.h"

namespace core {

/// \return
///     The variable name of \a value or a default placeholder.
llvm::StringRef GetNonNullVariableName(lldb::SBValue &value);

/// VSCode can't display two variables with the same name, so we need to
/// distinguish them by using a suffix.
///
/// If the source and line information is present, we use it as the suffix.
/// Otherwise, we fallback to the variable address or register location.
std::string CreateUniqueVariableNameForDisplay(lldb::SBValue &v,
                                                bool is_name_duplicated);

/// Helper struct that parses the metadata of an \a lldb::SBValue and produces
/// a canonical set of properties that can be sent to DAP clients.
struct VariableDescription {
  // The error message if SBValue.GetValue() fails.
  std::optional<std::string> error;
  // The display description to show on the IDE.
  std::string display_value;
  // The display name to show on the IDE.
  std::string name;
  // The variable path for this variable.
  std::string evaluate_name;
  // The output of SBValue.GetValue() if it doesn't fail. It might be empty.
  llvm::StringRef value;
  // The summary string of this variable. It might be empty.
  llvm::StringRef summary;
  // The auto summary if using `enableAutoVariableSummaries`.
  std::optional<std::string> auto_summary;
  // The type of this variable.
  lldb::SBType type_obj;
  // The display type name of this variable.
  llvm::StringRef display_type_name;
  /// The SBValue for this variable.
  lldb::SBValue val;

  VariableDescription(lldb::SBValue v, bool auto_variable_summaries,
                       bool format_hex = false, bool is_name_duplicated = false,
                       std::optional<llvm::StringRef> custom_name = {});

  /// Returns a description of the value appropriate for the specified context.
  std::string GetResult(dap::protocol::EvaluateContext context);
};

/// Does the given variable have an associated value location?
bool ValuePointsToCode(lldb::SBValue v);

}  // namespace core

#endif  // TRAILER_CORE_VARIABLE_DESCRIPTION_HPP_
