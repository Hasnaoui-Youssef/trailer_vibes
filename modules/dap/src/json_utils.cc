//===-- JSONUtils.cpp -------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "dap/json_utils.hpp"

#include "dap/protocol/protocol_base.hpp"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/ScopedPrinter.h"
#include "llvm/Support/raw_ostream.h"

namespace dap {

void EmplaceSafeString(llvm::json::Object &obj, llvm::StringRef key, llvm::StringRef str) {
    if (LLVM_LIKELY(llvm::json::isUTF8(str)))
        obj.try_emplace(key, str.str());
    else
        obj.try_emplace(key, llvm::json::fixUTF8(str));
}

llvm::StringRef GetAsString(const llvm::json::Value &value) {
    if (auto s = value.getAsString())
        return *s;
    return llvm::StringRef();
}

std::optional<llvm::StringRef> GetString(const llvm::json::Object &obj, llvm::StringRef key) {
    return obj.getString(key);
}

std::optional<llvm::StringRef> GetString(const llvm::json::Object *obj, llvm::StringRef key) {
    if (obj == nullptr)
        return std::nullopt;
    return GetString(*obj, key);
}

std::optional<bool> GetBoolean(const llvm::json::Object &obj, llvm::StringRef key) {
    if (auto value = obj.getBoolean(key))
        return *value;
    if (auto value = obj.getInteger(key))
        return *value != 0;
    return std::nullopt;
}

std::optional<bool> GetBoolean(const llvm::json::Object *obj, llvm::StringRef key) {
    if (obj != nullptr)
        return GetBoolean(*obj, key);
    return std::nullopt;
}

bool ObjectContainsKey(const llvm::json::Object &obj, llvm::StringRef key) { return obj.find(key) != obj.end(); }

std::vector<std::string> GetStrings(const llvm::json::Object *obj, llvm::StringRef key) {
    std::vector<std::string> strs;
    const auto *json_array = obj->getArray(key);
    if (!json_array)
        return strs;
    for (const auto &value : *json_array) {
        switch (value.kind()) {
            case llvm::json::Value::String:
                strs.push_back(value.getAsString()->str());
                break;
            case llvm::json::Value::Number:
            case llvm::json::Value::Boolean:
                strs.push_back(llvm::to_string(value));
                break;
            case llvm::json::Value::Null:
            case llvm::json::Value::Object:
            case llvm::json::Value::Array:
                break;
        }
    }
    return strs;
}

std::unordered_map<std::string, std::string> GetStringMap(const llvm::json::Object &obj, llvm::StringRef key) {
    std::unordered_map<std::string, std::string> strs;
    const auto *const json_object = obj.getObject(key);
    if (!json_object)
        return strs;

    for (const auto &[key, value] : *json_object) {
        switch (value.kind()) {
            case llvm::json::Value::String:
                strs.emplace(key.str(), value.getAsString()->str());
                break;
            case llvm::json::Value::Number:
            case llvm::json::Value::Boolean:
                strs.emplace(key.str(), llvm::to_string(value));
                break;
            case llvm::json::Value::Null:
            case llvm::json::Value::Object:
            case llvm::json::Value::Array:
                break;
        }
    }
    return strs;
}

void FillResponse(const llvm::json::Object &request, llvm::json::Object &response) {
    // Fill in all of the needed response fields to a "request" and set
    // "success" to true by default.
    response.try_emplace("type", "response");
    response.try_emplace("seq", protocol::kCalculateSeq);
    EmplaceSafeString(response, "command", GetString(request, "command").value_or(""));
    const uint64_t seq = GetInteger<uint64_t>(request, "seq").value_or(0);
    response.try_emplace("request_seq", seq);
    response.try_emplace("success", true);
}

llvm::json::Object CreateEventObject(const llvm::StringRef event_name) {
    llvm::json::Object event;
    event.try_emplace("seq", protocol::kCalculateSeq);
    event.try_emplace("type", "event");
    EmplaceSafeString(event, "event", event_name);
    return event;
}

std::string JSONToString(const llvm::json::Value &json) {
    std::string data;
    llvm::raw_string_ostream os(data);
    os << json;
    return data;
}

int64_t PackLocation(int64_t var_ref, bool is_value_location) { return var_ref << 1 | is_value_location; }

std::pair<int64_t, bool> UnpackLocation(int64_t location_id) { return std::pair{location_id >> 1, location_id & 1}; }

}  // namespace dap
