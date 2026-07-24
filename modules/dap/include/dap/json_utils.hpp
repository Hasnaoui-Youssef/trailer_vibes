//===-- JSONUtils.h ---------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef TRAILER_DAP_JSON_UTILS_HPP_
#define TRAILER_DAP_JSON_UTILS_HPP_

// The LLDB-agnostic half of the original (forked) JSONUtils.h - plain JSON
// helpers plus the DAP response/event envelope builders, none of which touch
// the SB API. The SB-bound half (VariableDescription, CreateThreadStopped,
// etc) stays in debug_service/json_utils.hpp, which includes this header to
// keep existing callers working with a single #include.

#include "llvm/ADT/StringRef.h"
#include "llvm/Support/JSON.h"
#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace dap {

/// Emplace a StringRef in a json::Object after ensuring that the
/// string is valid UTF8. If not, first call llvm::json::fixUTF8
/// before emplacing.
void EmplaceSafeString(llvm::json::Object &obj, llvm::StringRef key, llvm::StringRef str);

/// Extract simple values as a string.
llvm::StringRef GetAsString(const llvm::json::Value &value);

/// Extract the string value for the specified key from the specified object.
std::optional<llvm::StringRef> GetString(const llvm::json::Object &obj, llvm::StringRef key);
std::optional<llvm::StringRef> GetString(const llvm::json::Object *obj, llvm::StringRef key);

/// Extract the integer value for the specified key from the specified object
/// and return it as the specified integer type T.
/// @{
template <typename T>
std::optional<T> GetInteger(const llvm::json::Object &obj, llvm::StringRef key) {
    return obj.getInteger(key);
}

template <typename T>
std::optional<T> GetInteger(const llvm::json::Object *obj, llvm::StringRef key) {
    if (obj != nullptr)
        return GetInteger<T>(*obj, key);
    return std::nullopt;
}
/// @}

/// Extract the boolean value for the specified key from the specified object.
/// @{
std::optional<bool> GetBoolean(const llvm::json::Object &obj, llvm::StringRef key);
std::optional<bool> GetBoolean(const llvm::json::Object *obj, llvm::StringRef key);
/// @}

/// Check if the specified key exists in the specified object.
bool ObjectContainsKey(const llvm::json::Object &obj, llvm::StringRef key);

/// Extract an array of strings for the specified key from an object.
std::vector<std::string> GetStrings(const llvm::json::Object *obj, llvm::StringRef key);

/// Extract an object of key value strings for the specified key from an object.
std::unordered_map<std::string, std::string> GetStringMap(const llvm::json::Object &obj, llvm::StringRef key);

/// Fill a response object given the request object: sets "type"/"seq"/
/// "request_seq"/"command"/"success" per the DAP envelope.
void FillResponse(const llvm::json::Object &request, llvm::json::Object &response);

/// Create an "Event" JSON object using \a event_name as the event name.
llvm::json::Object CreateEventObject(llvm::StringRef event_name);

/// Convert a given JSON object to a string.
std::string JSONToString(const llvm::json::Value &json);

/// Pack a location into a single integer which we can send via the debug
/// adapter protocol.
int64_t PackLocation(int64_t var_ref, bool is_value_location);

/// Reverse of `PackLocation`.
std::pair<int64_t, bool> UnpackLocation(int64_t location_id);

}  // namespace dap

#endif  // TRAILER_DAP_JSON_UTILS_HPP_
