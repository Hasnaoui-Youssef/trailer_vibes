//===-- JSONUtils.h ---------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_TOOLS_LLDB_DAP_JSONUTILS_H
#define LLDB_TOOLS_LLDB_DAP_JSONUTILS_H

// The SB-bound half of the original (forked) JSONUtils.h - everything here
// serializes live SBValue/SBType/SBThread/SBTarget objects. The plain JSON
// helpers (EmplaceSafeString, GetString, FillResponse, CreateEventObject,
// etc) moved to dap/json_utils.hpp (dap_core, LLDB-free); re-included below
// so existing callers of this header keep seeing both halves.
#include "dap/json_utils.hpp"

#include "core/lldb_utils.hpp"
#include "core/variable_description.hpp"
#include "debug_service/dap_forward.hpp"
#include "dap/protocol/protocol_requests.hpp"
#include "lldb/API/SBCompileUnit.h"
#include "lldb/API/SBFormat.h"
#include "lldb/API/SBType.h"
#include "lldb/API/SBValue.h"
#include "lldb/lldb-types.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/JSON.h"
#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace dap {

// GetNonNullVariableName, CreateUniqueVariableNameForDisplay,
// VariableDescription and ValuePointsToCode moved to
// core/variable_description.hpp - they only ever touched lldb::SBValue and
// plain data, no DebugService coupling, so they're now a shared core
// utility (Breakpoint's log-message formatting needs it too, not just the
// variables/evaluate handlers). Re-exported into this namespace so
// existing callers keep working unqualified.
using core::CreateUniqueVariableNameForDisplay;
using core::GetNonNullVariableName;
using core::ValuePointsToCode;
using core::VariableDescription;

// CreateTerminatedEventObject (+ its statistics-dump helpers) moved to
// core/lldb_utils.hpp - no DebugService coupling, just an SBTarget ->
// JSON converter. Re-exported so debug_service.cc's call site is unchanged.
using core::CreateTerminatedEventObject;

/// Convert a given JSON object to a string.
std::string JSONToString(const llvm::json::Value &json);

} // namespace dap

#endif
