//===-- RequestHandler.cpp ------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "handlers/request_handler.hpp"
#include "core/components/data_manager.hpp"
#include "core/components/target_manager.hpp"
#include "dap/protocol/protocol_base.hpp"
#include "dap/protocol/protocol_requests.hpp"
#include "llvm/Support/Error.h"
#include "llvm/Support/JSON.h"
#include "llvm/Support/raw_ostream.h"

#if !defined(_WIN32)
#include <unistd.h>
#endif

#ifndef LLDB_DAP_README_URL
#define LLDB_DAP_README_URL                                                    \
  "https://lldb.llvm.org/use/lldbdap.html#debug-console"
#endif

using namespace dap::protocol;

namespace dap {

// RunInTerminal (spawn a local host process in a visible terminal) is
// deliberately not ported: it solves a problem this project doesn't have
void BaseRequestHandler::Run(const Request &request) {
  if (orchestrator_.IsCancelled(request)) {
    Response cancelled{
        /*request_seq=*/request.seq,
        /*command=*/request.command,
        /*success=*/false,
        /*message=*/eResponseMessageCancelled,
    };
    orchestrator_.Send(cancelled);
    return;
  }
  context_.CancelInterruptRequest();

  // No API-mutex lock here: every core method a handler calls below (via
  // operator()) locks internally for its own duration - see
  // core/debug_context.hpp's class comment on the universal internal-
  // locking rule.
  operator()(request);
}

void BaseRequestHandler::PrintWelcomeMessage() const {
  std::string message;
  llvm::raw_string_ostream OS(message);

#ifdef LLDB_DAP_WELCOME_MESSAGE
  context_.SendOutput(core::OutputCategory::Console, LLDB_DAP_WELCOME_MESSAGE);
#endif

  // Trying to provide a brief but helpful welcome message for users to better
  // understand how the debug console repl works.
  OS << "To get started with the debug console try ";
  switch (context_.Data().repl_mode) {
  case core::ReplMode::Auto:
    OS << "\"<variable>\", \"<lldb-cmd>\" or \"help [<lldb-cmd>]\"\r\n";
    break;
  case core::ReplMode::Command:
    OS << "\"<lldb-cmd>\" or \"help [<lldb-cmd>]\".\r\n";
    break;
  case core::ReplMode::Variable:
    OS << "\"<variable>\" or \"" << context_.Session().configuration.commandEscapePrefix
       << "help [<lldb-cmd>]\".\r\n";
    break;
  }

  OS << "For more information visit " LLDB_DAP_README_URL ".\r\n";

  context_.SendOutput(core::OutputCategory::Console, message);
}

void BaseRequestHandler::PrintIntroductionMessage() const {
  std::string msg;
  llvm::raw_string_ostream os(msg);
  std::string exe_path = context_.ExecutablePath();
  if (!exe_path.empty()) {
    os << llvm::formatv("Executable binary set to '{0}' ({1}).\r\n", exe_path, context_.TargetTriple());
  }
  if (std::optional<uint64_t> pid = context_.ProcessId()) {
    os << llvm::formatv("Attached to process {0}.\r\n", *pid);
  }
  context_.SendOutput(core::OutputCategory::Console, msg);
}

bool BaseRequestHandler::HasInstructionGranularity(
    const llvm::json::Object &arguments) const {
  if (std::optional<llvm::StringRef> value = arguments.getString("granularity"))
    return value == "instruction";
  return false;
}

void BaseRequestHandler::BuildErrorResponse(
    llvm::Error err, protocol::Response &response) const {
  // Handle the ErrorSuccess case.
  if (!err) {
    response.success = true;
    return;
  }

  response.success = false;

  llvm::handleAllErrors(
      std::move(err),
      [&](const NotStoppedError &err) {
        response.message = dap::protocol::eResponseMessageNotStopped;
      },
      [&](const DAPError &err) {
        protocol::ErrorMessage error_message;
        error_message.sendTelemetry = false;
        error_message.format = err.getMessage();
        error_message.showUser = err.getShowUser();
        error_message.id = err.convertToErrorCode().value();
        error_message.url = err.getURL();
        error_message.urlLabel = err.getURLLabel();
        protocol::ErrorResponseBody body;
        body.error = error_message;

        response.body = body;
      },
      [&](const llvm::ErrorInfoBase &err) {
        protocol::ErrorMessage error_message;
        error_message.showUser = true;
        error_message.sendTelemetry = false;
        error_message.format = err.message();
        error_message.id = err.convertToErrorCode().value();
        protocol::ErrorResponseBody body;
        body.error = error_message;

        response.body = body;
      });
}

void BaseRequestHandler::SendError(llvm::Error err,
                                   protocol::Response &response) const {
  BuildErrorResponse(std::move(err), response);
  Send(response);
}

void BaseRequestHandler::SendSuccess(
    protocol::Response &response, std::optional<llvm::json::Value> body) const {
  response.success = true;
  if (body)
    response.body = std::move(*body);

  Send(response);
}

void BaseRequestHandler::Send(protocol::Response &response) const {
  if (context_.IsInterruptRequested()) {
    response.success = false;
    response.message = protocol::eResponseMessageCancelled;
    response.body = std::nullopt;
  }

  orchestrator_.Send(response);
}

} // namespace dap
