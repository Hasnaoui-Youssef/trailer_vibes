//===-- ProtocolBase.h ----------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file contains POD structs based on the DAP specification at
// https://microsoft.github.io/debug-adapter-protocol/specification
//
// This is not meant to be a complete implementation, new interfaces are added
// when they're needed.
//
// Each struct has a toJSON and fromJSON function, that converts between
// the struct and a JSON representation. (See JSON.h)
//
//===----------------------------------------------------------------------===//

#ifndef TRAILER_DAP_PROTOCOL_PROTOCOL_BASE_HPP_
#define TRAILER_DAP_PROTOCOL_PROTOCOL_BASE_HPP_

#include "llvm/Support/JSON.h"
#include <cstdint>
#include <optional>
#include <string>
#include <variant>

namespace dap::protocol {

// MARK: Base Protocol

using Id = uint64_t;

static constexpr Id kCalculateSeq = UINT64_MAX;

struct Request {
  std::string command;

  std::optional<llvm::json::Value> arguments = std::nullopt;

  Id seq = kCalculateSeq;
};
llvm::json::Value toJSON(const Request &);
bool fromJSON(const llvm::json::Value &, Request &, llvm::json::Path);
bool operator==(const Request &, const Request &);

struct Event {
  std::string event;

  std::optional<llvm::json::Value> body = std::nullopt;

  Id seq = kCalculateSeq;
};
llvm::json::Value toJSON(const Event &);
bool fromJSON(const llvm::json::Value &, Event &, llvm::json::Path);
bool operator==(const Event &, const Event &);

enum ResponseMessage : unsigned {
  eResponseMessageCancelled,
  /// The request may be retried once the adapter is in a 'stopped' state
  eResponseMessageNotStopped,
};

struct Response {
  Id request_seq = 0;

  std::string command;

  bool success = false;

  // FIXME: Migrate usage of fallback string to ErrorMessage
  std::optional<std::variant<ResponseMessage, std::string>> message =
      std::nullopt;

  std::optional<llvm::json::Value> body = std::nullopt;

  Id seq = kCalculateSeq;
};
bool fromJSON(const llvm::json::Value &, Response &, llvm::json::Path);
llvm::json::Value toJSON(const Response &);
bool operator==(const Response &, const Response &);

struct ErrorMessage {
  uint64_t id = 0;

  std::string format;

  std::optional<std::map<std::string, std::string>> variables;

  bool sendTelemetry = false;

  bool showUser = false;

  std::optional<std::string> url;

  std::optional<std::string> urlLabel;
};
bool fromJSON(const llvm::json::Value &, ErrorMessage &, llvm::json::Path);
llvm::json::Value toJSON(const ErrorMessage &);

using Message = std::variant<Request, Response, Event>;
bool fromJSON(const llvm::json::Value &, Message &, llvm::json::Path);
llvm::json::Value toJSON(const Message &);
bool operator==(const Message &, const Message &);

inline llvm::raw_ostream &operator<<(llvm::raw_ostream &OS, const Message &V) {
  OS << toJSON(V);
  return OS;
}

struct ErrorResponseBody {
  std::optional<ErrorMessage> error;
};
llvm::json::Value toJSON(const ErrorResponseBody &);

using EmptyArguments = std::optional<std::monostate>;

using VoidResponse = llvm::Error;

} // namespace dap::protocol

#endif  // TRAILER_DAP_PROTOCOL_PROTOCOL_BASE_HPP_
