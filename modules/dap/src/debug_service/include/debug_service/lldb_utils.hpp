//===-- LLDBUtils.h ---------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_TOOLS_LLDB_DAP_LLDBUTILS_H
#define LLDB_TOOLS_LLDB_DAP_LLDBUTILS_H

#include "core/lldb_utils.hpp"
#include "debug_service/dap_forward.hpp"
#include "lldb/API/SBDebugger.h"
#include "lldb/API/SBEnvironment.h"
#include "lldb/API/SBError.h"
#include "lldb/API/SBFileSpec.h"
#include "lldb/API/SBLineEntry.h"
#include "lldb/API/SBTarget.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/JSON.h"
#include "llvm/Support/ScopedPrinter.h"
#include "llvm/Support/raw_ostream.h"
#include <chrono>
#include <string>

namespace dap {

// GetSBFileSpecPath, GetLineEntryForAddress and GetStopDisassemblyDisplay
// moved to core/lldb_utils.hpp (see PROJECT_STATUS.md's Module carve);
// RunLLDBCommands and ScopeSyncMode moved there too with the Target carve -
// re-exported here so existing callers keep working unqualified.
using core::GetLineEntryForAddress;
using core::GetSBFileSpecPath;
using core::GetStopDisassemblyDisplay;
using core::RunLLDBCommands;
using core::ScopeSyncMode;
using core::ToError;

/// Check if a thread has a stop reason.
///
/// \param[in] thread
///     The LLDB thread object to check
///
/// \return
///     \b True if the thread has a valid stop reason, \b false
///     otherwise.
bool ThreadHasStopReason(lldb::SBThread &thread);

/// Given a LLDB frame, make a frame ID that is unique to a specific
/// thread and frame.
///
/// DebugService requires a Stackframe "id" to be unique, so we use the frame
/// index in the lower 32 bits and the thread index ID in the upper 32
/// bits.
///
/// \param[in] frame
///     The LLDB stack frame object generate the ID for
///
/// \return
///     A unique integer that allows us to easily find the right
///     stack frame within a thread on subsequent VS code requests.
uint64_t MakeDAPFrameID(lldb::SBFrame &frame);

// GetLLDBThreadIndexID/GetLLDBFrameID (the decode side of the encoding
// MakeDAPFrameID above implements) moved to core/lldb_utils.hpp with
// DebugContext::GetLLDBFrame - see PROJECT_STATUS.md's Data carve. Not
// re-exported here: this header's own remaining caller (MakeDAPFrameID,
// used by the not-yet-carved stack-trace handler) never needed the decode
// direction.

/// Gets all the environment variables from the json object depending on if the
/// kind is an object or an array.
///
/// \param[in] arguments
///     The json object with the launch options
///
/// \return
///     The environment variables stored in the env key
lldb::SBEnvironment
GetEnvironmentFromArguments(const llvm::json::Object &arguments);

/// Helper for sending telemetry to lldb server, if client-telemetry is enabled.
class TelemetryDispatcher {
public:
  TelemetryDispatcher(lldb::SBDebugger *debugger) {
    m_telemetry_json = llvm::json::Object();
    m_telemetry_json.try_emplace(
        "start_time",
        std::chrono::steady_clock::now().time_since_epoch().count());
    this->debugger = debugger;
  }

  void Set(std::string key, std::string value) {
    m_telemetry_json.try_emplace(key, value);
  }

  void Set(std::string key, int64_t value) {
    m_telemetry_json.try_emplace(key, value);
  }

  ~TelemetryDispatcher() {
    m_telemetry_json.try_emplace(
        "end_time",
        std::chrono::steady_clock::now().time_since_epoch().count());

    lldb::SBStructuredData telemetry_entry;
    llvm::json::Value val(std::move(m_telemetry_json));

    std::string string_rep = llvm::to_string(val);
    telemetry_entry.SetFromJSON(string_rep.c_str());
    debugger->DispatchClientTelemetry(telemetry_entry);
  }

private:
  llvm::json::Object m_telemetry_json;
  lldb::SBDebugger *debugger;
};

/// Provides the string value if this data structure is a string type.
std::string GetStringValue(const lldb::SBStructuredData &data);

/// Converts UTF16 column codeunits to bytes.
/// we are recieving utf8 from the specification.
/// UTF16 codunit size => 2 bytes.
/// UTF8 codunit size => 1 byte.
/// Example
/// | info     | info  | utf16_cu | size in bytes |
/// | fake f   | ƒ     | 1        | 2             |
/// | fake c   | ç     | 1        | 3             |
/// | poop char| 💩    | 2        | 4             |
///
/// so with inputs string of
/// (`ƒ💩`, 3) we have 3 utf16_u and ( 2 + 4 ) bytes.
/// (`ƒ💩`, 2) we have 3 utf16_u and ( 2 + 4 ) bytes but the position is in
///  between the 💩 char so we return null since the codepoint is not complete.
///
/// see https://utf8everywhere.org/#characters for more info.
std::optional<size_t> UTF16CodeunitToBytes(llvm::StringRef line,
                                           uint32_t utf16_codeunits);
} // namespace dap

#endif
