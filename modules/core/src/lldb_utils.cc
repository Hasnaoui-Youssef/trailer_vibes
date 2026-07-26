#include "core/lldb_utils.hpp"

#include <cstring>
#include <mutex>
#include <system_error>

#include "dap/dap_error.hpp"
#include "lldb/API/SBCommandInterpreter.h"
#include "lldb/API/SBCommandReturnObject.h"
#include "lldb/API/SBStream.h"
#include "lldb/API/SBStringList.h"
#include "lldb/API/SBStructuredData.h"
#include "lldb/API/SBSymbolContext.h"
#include "lldb/API/SBThread.h"
#include "lldb/lldb-defines.h"
#include "lldb/lldb-enumerations.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/ADT/StringSwitch.h"
#include "llvm/Support/JSON.h"
#include "llvm/Support/raw_ostream.h"

namespace core {

namespace {

bool ShouldDisplayAssemblySource(lldb::SBLineEntry line_entry,
                                  lldb::StopDisassemblyType stop_disassembly_display) {
  if (stop_disassembly_display == lldb::eStopDisassemblyTypeNever)
    return false;

  if (stop_disassembly_display == lldb::eStopDisassemblyTypeAlways)
    return true;

  // A line entry of 0 indicates the line is compiler generated i.e. no source
  // file is associated with the frame.
  auto file_spec = line_entry.GetFileSpec();
  if (!file_spec.IsValid() || line_entry.GetLine() == 0 || line_entry.GetLine() == LLDB_INVALID_LINE_NUMBER)
    return true;

  if (stop_disassembly_display == lldb::eStopDisassemblyTypeNoSource && !file_spec.Exists())
    return true;

  return false;
}

}  // namespace

std::string GetSBFileSpecPath(const lldb::SBFileSpec &file_spec) {
  const auto directory_length = ::strlen(file_spec.GetDirectory());
  const auto file_name_length = ::strlen(file_spec.GetFilename());

  std::string path(directory_length + file_name_length + 1, '\0');
  file_spec.GetPath(path.data(), path.length() + 1);
  return path;
}

lldb::SBLineEntry GetLineEntryForAddress(lldb::SBTarget &target, const lldb::SBAddress &address) {
  lldb::SBSymbolContext sc = target.ResolveSymbolContextForAddress(address, lldb::eSymbolContextLineEntry);
  return sc.GetLineEntry();
}

lldb::StopDisassemblyType GetStopDisassemblyDisplay(lldb::SBDebugger &debugger) {
  lldb::StopDisassemblyType result = lldb::StopDisassemblyType::eStopDisassemblyTypeNoDebugInfo;
  lldb::SBStructuredData string_result = debugger.GetSetting("stop-disassembly-display");
  const size_t result_length = string_result.GetStringValue(nullptr, 0);
  if (result_length > 0) {
    std::string result_string(result_length, '\0');
    string_result.GetStringValue(result_string.data(), result_length + 1);

    result = llvm::StringSwitch<lldb::StopDisassemblyType>(result_string)
                 .Case("never", lldb::StopDisassemblyType::eStopDisassemblyTypeNever)
                 .Case("always", lldb::StopDisassemblyType::eStopDisassemblyTypeAlways)
                 .Case("no-source", lldb::StopDisassemblyType::eStopDisassemblyTypeNoSource)
                 .Case("no-debuginfo", lldb::StopDisassemblyType::eStopDisassemblyTypeNoDebugInfo)
                 .Default(lldb::StopDisassemblyType::eStopDisassemblyTypeNoDebugInfo);
  }

  return result;
}

bool DisplayAssemblySource(lldb::SBDebugger &debugger, lldb::SBLineEntry line_entry) {
  const lldb::StopDisassemblyType stop_disassembly_display = GetStopDisassemblyDisplay(debugger);
  return ShouldDisplayAssemblySource(line_entry, stop_disassembly_display);
}

std::string GetLoadAddressString(lldb::addr_t addr) { return "0x" + llvm::utohexstr(addr, false, 16); }

std::optional<dap::protocol::Source> CreateSource(const lldb::SBFileSpec &file) {
  if (!file.IsValid())
    return std::nullopt;

  dap::protocol::Source source;
  if (const char *name = file.GetFilename())
    source.name = name;
  char path[4096] = "";
  if (file.GetPath(path, sizeof(path)) && lldb::SBFileSpec::ResolvePath(path, path, sizeof(path)))
    source.path = path;
  return source;
}

bool IsAssemblySource(const dap::protocol::Source &source) {
  return source.sourceReference.value_or(LLDB_DAP_INVALID_SRC_REF) > LLDB_DAP_INVALID_SRC_REF;
}

namespace {
constexpr uint32_t kThreadIndexShift = 19;
}  // namespace

uint64_t MakeDAPFrameID(lldb::SBFrame &frame) {
  return (static_cast<uint64_t>(frame.GetThread().GetIndexID()) << kThreadIndexShift) | frame.GetFrameID();
}

uint32_t GetLLDBThreadIndexID(uint64_t dap_frame_id) { return dap_frame_id >> kThreadIndexShift; }

uint32_t GetLLDBFrameID(uint64_t dap_frame_id) { return dap_frame_id & ((1u << kThreadIndexShift) - 1); }

bool RunLLDBCommands(lldb::SBDebugger &debugger, llvm::StringRef prefix, const llvm::ArrayRef<std::string> &commands,
                     llvm::raw_ostream &strm, bool parse_command_directives, bool echo_commands) {
  if (commands.empty())
    return true;

  bool did_print_prefix = false;

  // We only need the prompt when echoing commands.
  std::string prompt_string;
  if (echo_commands) {
    prompt_string = "(lldb) ";

    // Get the current prompt from settings.
    if (const lldb::SBStructuredData prompt = debugger.GetSetting("prompt")) {
      const size_t prompt_length = prompt.GetStringValue(nullptr, 0);

      if (prompt_length != 0) {
        prompt_string.resize(prompt_length + 1);
        prompt.GetStringValue(prompt_string.data(), prompt_string.length());
      }
    }
  }

  lldb::SBCommandInterpreter interp = debugger.GetCommandInterpreter();
  for (llvm::StringRef command : commands) {
    lldb::SBCommandReturnObject result;
    bool quiet_on_success = false;
    bool check_error = false;

    while (parse_command_directives) {
      if (command.starts_with("?")) {
        command = command.drop_front();
        quiet_on_success = true;
      } else if (command.starts_with("!")) {
        command = command.drop_front();
        check_error = true;
      } else {
        break;
      }
    }

    {
      // Prevent simultaneous calls to HandleCommand, e.g. the event thread
      // may asynchronously call RunExitCommands when we are already calling
      // RunTerminateCommands.
      static std::mutex handle_command_mutex;
      std::lock_guard<std::mutex> locker(handle_command_mutex);
      interp.HandleCommand(command.str().c_str(), result,
                           /*add_to_history=*/true);
    }

    const bool got_error = !result.Succeeded();
    // The if statement below is assuming we always print out `!` prefixed
    // lines. The only time we don't print is when we have `quiet_on_success ==
    // true` and we don't have an error.
    if (quiet_on_success ? got_error : true) {
      if (!did_print_prefix && !prefix.empty()) {
        strm << prefix << "\n";
        did_print_prefix = true;
      }

      if (echo_commands)
        strm << prompt_string.c_str() << command << '\n';

      auto output_len = result.GetOutputSize();
      if (output_len) {
        const char *output = result.GetOutput();
        strm << output;
      }
      auto error_len = result.GetErrorSize();
      if (error_len) {
        const char *error = result.GetError();
        strm << error;
      }
    }
    if (check_error && got_error)
      return false; // Stop running commands.
  }
  return true;
}

std::string RunLLDBCommands(lldb::SBDebugger &debugger, llvm::StringRef prefix, const llvm::ArrayRef<std::string> &commands,
                            bool &required_command_failed, bool parse_command_directives, bool echo_commands) {
  required_command_failed = false;
  std::string s;
  llvm::raw_string_ostream strm(s);
  required_command_failed =
      !RunLLDBCommands(debugger, prefix, commands, strm,
                       parse_command_directives, echo_commands);
  return s;
}

ScopeSyncMode::ScopeSyncMode(lldb::SBDebugger &debugger)
    : m_debugger(debugger), m_async(m_debugger.GetAsync()) {
  m_debugger.SetAsync(false);
}

ScopeSyncMode::~ScopeSyncMode() { m_debugger.SetAsync(m_async); }

llvm::Error ToError(const lldb::SBError &error, bool show_user) {
  if (error.Success())
    return llvm::Error::success();

  return llvm::make_error<dap::DAPError>(
      /*message=*/error.GetCString(),
      /*EC=*/std::error_code(error.GetError(), std::generic_category()),
      /*show_user=*/show_user);
}

namespace {

// Keep all the top level items from the statistics dump, except for the
// "modules" array. It can be huge and cause delay. Array and dictionary
// values are returned as <key, JSON string> pairs.
void FilterAndGetValueForKey(const lldb::SBStructuredData data, const char *key, llvm::json::Object &out) {
  lldb::SBStructuredData value = data.GetValueForKey(key);
  std::string key_utf8 = llvm::json::fixUTF8(key);
  if (llvm::StringRef(key) == "modules")
    return;
  switch (value.GetType()) {
  case lldb::eStructuredDataTypeFloat:
    out.try_emplace(key_utf8, value.GetFloatValue());
    break;
  case lldb::eStructuredDataTypeUnsignedInteger:
    out.try_emplace(key_utf8, value.GetIntegerValue((uint64_t)0));
    break;
  case lldb::eStructuredDataTypeSignedInteger:
    out.try_emplace(key_utf8, value.GetIntegerValue((int64_t)0));
    break;
  case lldb::eStructuredDataTypeArray: {
    lldb::SBStream contents;
    value.GetAsJSON(contents);
    out.try_emplace(key_utf8, llvm::json::fixUTF8(contents.GetData()));
  } break;
  case lldb::eStructuredDataTypeBoolean:
    out.try_emplace(key_utf8, value.GetBooleanValue());
    break;
  case lldb::eStructuredDataTypeString: {
    // Get the string size before reading
    const size_t str_length = value.GetStringValue(nullptr, 0);
    std::string str(str_length + 1, 0);
    value.GetStringValue(&str[0], str_length);
    out.try_emplace(key_utf8, llvm::json::fixUTF8(str));
  } break;
  case lldb::eStructuredDataTypeDictionary: {
    lldb::SBStream contents;
    value.GetAsJSON(contents);
    out.try_emplace(key_utf8, llvm::json::fixUTF8(contents.GetData()));
  } break;
  case lldb::eStructuredDataTypeNull:
  case lldb::eStructuredDataTypeGeneric:
  case lldb::eStructuredDataTypeInvalid:
    break;
  }
}

}  // namespace

std::string BuildTerminatedStatisticsJSON(lldb::SBTarget &target) {
  lldb::SBStructuredData statistics = target.GetStatistics();
  if (statistics.GetType() != lldb::eStructuredDataTypeDictionary)
    return {};

  llvm::json::Object stats_body;
  lldb::SBStringList keys;
  if (!statistics.GetKeys(keys))
    return {};
  for (size_t i = 0; i < keys.GetSize(); i++) {
    const char *key = keys.GetStringAtIndex(i);
    FilterAndGetValueForKey(statistics, key, stats_body);
  }

  std::string json_str;
  llvm::raw_string_ostream os(json_str);
  os << llvm::json::Value(std::move(stats_body));
  return json_str;
}

}  // namespace core
