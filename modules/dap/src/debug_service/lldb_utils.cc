//===-- LLDBUtils.cpp -------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "debug_service/lldb_utils.hpp"
#include "dap/dap_error.hpp"
#include "debug_service/json_utils.hpp"
#include "lldb/API/SBCommandInterpreter.h"
#include "lldb/API/SBCommandReturnObject.h"
#include "lldb/API/SBDebugger.h"
#include "lldb/API/SBFrame.h"
#include "lldb/API/SBStringList.h"
#include "lldb/API/SBStructuredData.h"
#include "lldb/API/SBThread.h"
#include "lldb/lldb-defines.h"
#include "lldb/lldb-enumerations.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/Support/ConvertUTF.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/JSON.h"
#include "llvm/Support/raw_ostream.h"

#include <cstdint>
#include <cstring>
#include <mutex>
#include <system_error>

namespace dap {

bool ThreadHasStopReason(lldb::SBThread &thread) {
  switch (thread.GetStopReason()) {
  case lldb::eStopReasonTrace:
  case lldb::eStopReasonPlanComplete:
  case lldb::eStopReasonWatchpoint:
  case lldb::eStopReasonInstrumentation:
  case lldb::eStopReasonSignal:
  case lldb::eStopReasonException:
  case lldb::eStopReasonExec:
  case lldb::eStopReasonProcessorTrace:
  case lldb::eStopReasonFork:
  case lldb::eStopReasonVFork:
  case lldb::eStopReasonVForkDone:
  case lldb::eStopReasonInterrupt:
  case lldb::eStopReasonHistoryBoundary:
    return true;
  case lldb::eStopReasonBreakpoint: {
    // Stop reason data for breakpoints consists of breakpoint ID and location
    // ID pairs. Internal breakpoints (identified by their ID) are not
    // considered valid stop reasons.
    const uint64_t data_count = thread.GetStopReasonDataCount();
    if (data_count == 0)
      return true;
    for (uint64_t i = 0; i < data_count; i += 2) {
      const lldb::break_id_t bp_id = thread.GetStopReasonDataAtIndex(i);
      if (!LLDB_BREAK_ID_IS_INTERNAL(bp_id))
        return true;
    }
    return false;
  }
  case lldb::eStopReasonThreadExiting:
  case lldb::eStopReasonInvalid:
  case lldb::eStopReasonNone:
    break;
  }
  return false;
}

static uint32_t constexpr THREAD_INDEX_SHIFT = 19;

uint64_t MakeDAPFrameID(lldb::SBFrame &frame) {
  return ((uint64_t)frame.GetThread().GetIndexID() << THREAD_INDEX_SHIFT) |
         frame.GetFrameID();
}

lldb::SBEnvironment
GetEnvironmentFromArguments(const llvm::json::Object &arguments) {
  lldb::SBEnvironment envs{};
  constexpr llvm::StringRef env_key = "env";
  const llvm::json::Value *raw_json_env = arguments.get(env_key);

  if (!raw_json_env)
    return envs;

  if (raw_json_env->kind() == llvm::json::Value::Object) {
    auto env_map = GetStringMap(arguments, env_key);
    for (const auto &[key, value] : env_map)
      envs.Set(key.c_str(), value.c_str(), true);

  } else if (raw_json_env->kind() == llvm::json::Value::Array) {
    const auto envs_strings = GetStrings(&arguments, env_key);
    lldb::SBStringList entries{};
    for (const auto &env : envs_strings)
      entries.AppendString(env.c_str());

    envs.SetEntries(entries, true);
  }
  return envs;
}

std::string GetStringValue(const lldb::SBStructuredData &data) {
  if (!data.IsValid())
    return "";

  const size_t str_length = data.GetStringValue(nullptr, 0);
  if (!str_length)
    return "";

  std::string str(str_length, 0);
  data.GetStringValue(str.data(), str_length + 1);
  return str;
}

std::optional<size_t> UTF16CodeunitToBytes(llvm::StringRef line,
                                           uint32_t utf16_codeunits) {
  size_t bytes_count = 0;
  size_t utf16_seen_cu = 0;
  size_t idx = 0;
  const size_t line_size = line.size();

  while (idx < line_size && utf16_seen_cu < utf16_codeunits) {
    const char first_char = line[idx];
    const auto num_bytes = llvm::getNumBytesForUTF8(first_char);

    if (num_bytes == 4) {
      utf16_seen_cu += 2;
    } else if (num_bytes < 4) {
      utf16_seen_cu += 1;
    } else {
      // getNumBytesForUTF8 may return bytes greater than 4 this is not valid
      // UTF8
      return std::nullopt;
    }

    idx += num_bytes;
    if (utf16_seen_cu <= utf16_codeunits) {
      bytes_count = idx;
    } else {
      // We are in the middle of a codepoint or the utf16_codeunits ends in the
      // middle of a codepoint.
      return std::nullopt;
    }
  }

  return bytes_count;
}

} // namespace dap
