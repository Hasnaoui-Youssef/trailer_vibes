#include "core/variable_description.hpp"

#include <chrono>
#include <cstddef>

#include "lldb/API/SBDeclaration.h"
#include "lldb/API/SBLineEntry.h"
#include "lldb/API/SBStream.h"
#include "lldb/API/SBTarget.h"
#include "lldb/lldb-defines.h"
#include "lldb/lldb-enumerations.h"
#include "llvm/Support/FormatVariadic.h"
#include "llvm/Support/raw_ostream.h"

namespace core {

namespace {

constexpr const char *kNoTypeName = "<no-type>";

bool IsClassStructOrUnionType(lldb::SBType t) {
  return (t.GetTypeClass() & (lldb::eTypeClassUnion | lldb::eTypeClassStruct |
                              lldb::eTypeClassArray)) != 0;
}

/// Create a short summary for a container that contains the summary of its
/// first children, so that the user can get a glimpse of its contents at a
/// glance.
std::optional<std::string> TryCreateAutoSummaryForContainer(lldb::SBValue &v) {
  if (!v.MightHaveChildren())
    return std::nullopt;
  /// As this operation can be potentially slow, we limit the total time spent
  /// fetching children to a few ms.
  const auto max_evaluation_time = std::chrono::milliseconds(10);
  /// We don't want to generate a extremely long summary string, so we limit its
  /// length.
  const size_t max_length = 32;

  auto start = std::chrono::steady_clock::now();
  std::string summary;
  llvm::raw_string_ostream os(summary);
  os << "{";

  llvm::StringRef separator = "";

  for (size_t i = 0, e = v.GetNumChildren(); i < e; ++i) {
    // If we reached the time limit or exceeded the number of characters, we
    // dump `...` to signal that there are more elements in the collection.
    if (summary.size() > max_length ||
        (std::chrono::steady_clock::now() - start) > max_evaluation_time) {
      os << separator << "...";
      break;
    }
    lldb::SBValue child = v.GetChildAtIndex(i);

    if (llvm::StringRef name = child.GetName(); !name.empty()) {
      llvm::StringRef desc;
      if (llvm::StringRef summary = child.GetSummary(); !summary.empty())
        desc = summary;
      else if (llvm::StringRef value = child.GetValue(); !value.empty())
        desc = value;
      else if (IsClassStructOrUnionType(child.GetType()))
        desc = "{...}";
      else
        continue;

      // If the child is an indexed entry, we don't show its index to save
      // characters.
      if (name.starts_with("["))
        os << separator << desc;
      else
        os << separator << name << ":" << desc;
      separator = ", ";
    }
  }
  os << "}";

  if (summary == "{...}" || summary == "{}")
    return std::nullopt;
  return summary;
}

/// Try to create a summary string for the given value that doesn't have a
/// summary of its own.
std::optional<std::string> TryCreateAutoSummary(lldb::SBValue &value) {
  // We use the dereferenced value for generating the summary.
  if (value.GetType().IsPointerType() || value.GetType().IsReferenceType())
    value = value.Dereference();

  // We only support auto summaries for containers.
  return TryCreateAutoSummaryForContainer(value);
}

}  // namespace

llvm::StringRef GetNonNullVariableName(lldb::SBValue &v) {
  const llvm::StringRef name = v.GetName();
  return !name.empty() ? name : "<null>";
}

std::string CreateUniqueVariableNameForDisplay(lldb::SBValue &v,
                                                bool is_name_duplicated) {
  std::string unique_name{};
  llvm::raw_string_ostream name_builder(unique_name);
  name_builder << GetNonNullVariableName(v);
  if (is_name_duplicated) {
    const lldb::SBDeclaration declaration = v.GetDeclaration();
    const llvm::StringRef file_name = declaration.GetFileSpec().GetFilename();
    const uint32_t line = declaration.GetLine();

    if (!file_name.empty() && line != 0 && line != LLDB_INVALID_LINE_NUMBER)
      name_builder << llvm::formatv(" @ {}:{}", file_name, line);
    else if (llvm::StringRef location = v.GetLocation(); !location.empty())
      name_builder << llvm::formatv(" @ {}", location);
  }
  return unique_name;
}

VariableDescription::VariableDescription(
    lldb::SBValue val, bool auto_variable_summaries, bool format_hex,
    bool is_name_duplicated, std::optional<llvm::StringRef> custom_name)
    : val(val) {
  name = custom_name.value_or(
      CreateUniqueVariableNameForDisplay(val, is_name_duplicated));

  type_obj = val.GetType();
  const llvm::StringRef type_name = type_obj.GetDisplayTypeName();
  display_type_name = type_name.empty() ? kNoTypeName : type_name;

  // Only format hex/default if there is no existing special format.
  if (const lldb::Format current_format = val.GetFormat();
      current_format == lldb::eFormatDefault ||
      current_format == lldb::eFormatHex) {

    val.SetFormat(format_hex ? lldb::eFormatHex : lldb::eFormatDefault);
  }

  llvm::raw_string_ostream os_display_value(display_value);

  if (lldb::SBError sb_error = val.GetError(); sb_error.Fail()) {
    error = sb_error.GetCString();
    os_display_value << "<error: " << error << ">";
  } else {
    value = val.GetValue();
    summary = val.GetSummary();
    if (summary.empty() && auto_variable_summaries)
      auto_summary = TryCreateAutoSummary(val);

    llvm::StringRef display_summary = auto_summary ? *auto_summary : summary;
    const bool has_summary = !display_summary.empty();

    if (!value.empty()) {
      os_display_value << value;
      if (has_summary)
        os_display_value << " " << display_summary;
    } else if (has_summary) {
      os_display_value << display_summary;

    } else if (!type_name.empty()) {
      // As last resort, we print its type if available.
      os_display_value << type_name;
    }
  }

  lldb::SBStream evaluateStream;
  val.GetExpressionPath(evaluateStream);
  evaluate_name = llvm::StringRef(evaluateStream.GetData()).str();
}

std::string VariableDescription::GetResult(dap::protocol::EvaluateContext context) {
  // In repl and clipboard contexts, the results can be displayed as multiple
  // lines so more detailed descriptions can be returned.
  if (context != dap::protocol::eEvaluateContextRepl &&
      context != dap::protocol::eEvaluateContextClipboard)
    return display_value;

  if (!val.IsValid())
    return display_value;

  // Try the SBValue::GetDescription(), which may call into language runtime
  // specific formatters (see ValueObjectPrinter).
  lldb::SBStream stream;
  if (context == dap::protocol::eEvaluateContextRepl)
    val.GetDescription(stream, lldb::eDescriptionLevelFull);
  else
    val.GetDescription(stream, lldb::eDescriptionLevelBrief);
  llvm::StringRef description = stream.GetData();
  return description.trim().str();
}

bool ValuePointsToCode(lldb::SBValue v) {
  if (!v.GetType().GetPointeeType().IsFunctionType())
    return false;

  lldb::addr_t addr = v.GetValueAsAddress();
  lldb::SBLineEntry line_entry =
      v.GetTarget().ResolveLoadAddress(addr).GetLineEntry();

  return line_entry.IsValid();
}

}  // namespace core
