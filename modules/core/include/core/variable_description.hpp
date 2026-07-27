#ifndef TRAILER_CORE_VARIABLE_DESCRIPTION_HPP_
#define TRAILER_CORE_VARIABLE_DESCRIPTION_HPP_

#include <optional>
#include <string>

#include "dap/protocol/protocol_requests.hpp"
#include "lldb/API/SBType.h"
#include "lldb/API/SBValue.h"
#include "llvm/ADT/StringRef.h"

namespace core {
llvm::StringRef GetNonNullVariableName(lldb::SBValue &value);

std::string CreateUniqueVariableNameForDisplay(lldb::SBValue &v,
                                                bool is_name_duplicated);

struct VariableDescription {
  std::optional<std::string> error;
  std::string display_value;
  std::string name;
  std::string evaluate_name;
  llvm::StringRef value;
  llvm::StringRef summary;
  std::optional<std::string> auto_summary;
  lldb::SBType type_obj;
  llvm::StringRef display_type_name;
  lldb::SBValue val;

  VariableDescription(lldb::SBValue v, bool auto_variable_summaries,
                       bool format_hex = false, bool is_name_duplicated = false,
                       std::optional<llvm::StringRef> custom_name = {});

  std::string GetResult(dap::protocol::EvaluateContext context);
};

bool ValuePointsToCode(lldb::SBValue v);

}  // namespace core

#endif  // TRAILER_CORE_VARIABLE_DESCRIPTION_HPP_
