//===-- RequestHandler.cpp ------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "handlers/request_handler.hpp"
#include "core/components/execution_controller.hpp"
#include "core/components/target_manager.hpp"
#include "debug_service/debug_service.hpp"
#include "dap/response_handler.hpp"
#include "debug_service/json_utils.hpp"
#include "debug_service/lldb_utils.hpp"
#include "dap/protocol/protocol_base.hpp"
#include "dap/protocol/protocol_requests.hpp"
#include "lldb/API/SBDefines.h"
#include "lldb/API/SBEnvironment.h"
#include "llvm/ADT/ScopeExit.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/JSON.h"
#include "llvm/Support/raw_ostream.h"
#include <mutex>

#if !defined(_WIN32)
#include <unistd.h>
#endif

#ifndef LLDB_DAP_README_URL
#define LLDB_DAP_README_URL                                                    \
  "https://lldb.llvm.org/use/lldbdap.html#debug-console"
#endif

using namespace dap::protocol;

namespace dap {

static std::vector<const char *>
MakeArgv(const llvm::ArrayRef<std::string> &strs) {
  // Create and return an array of "const char *", one for each C string in
  // "strs" and terminate the list with a NULL. This can be used for argument
  // vectors (argv) or environment vectors (envp) like those passed to the
  // "main" function in C programs.
  std::vector<const char *> argv;
  for (const auto &s : strs)
    argv.push_back(s.c_str());
  argv.push_back(nullptr);
  return argv;
}

static uint32_t SetLaunchFlag(uint32_t flags, bool flag,
                              lldb::LaunchFlags mask) {
  if (flag)
    flags |= mask;
  else
    flags &= ~mask;

  return flags;
}

static void
SetupIORedirection(const std::vector<std::optional<std::string>> &stdio,
                   lldb::SBLaunchInfo &launch_info) {
  size_t n = std::max(stdio.size(), static_cast<size_t>(3));
  for (size_t i = 0; i < n; i++) {
    std::optional<std::string> path;
    if (stdio.size() <= i)
      path = stdio.back();
    else
      path = stdio[i];
    if (!path)
      continue;
    switch (i) {
    case 0:
      launch_info.AddOpenFileAction(i, path->c_str(), true, false);
      break;
    case 1:
    case 2:
      launch_info.AddOpenFileAction(i, path->c_str(), false, true);
      break;
    default:
      launch_info.AddOpenFileAction(i, path->c_str(), true, true);
      break;
    }
  }
}

// RunInTerminal (spawn a local host process in a visible terminal) is
// deliberately not ported: it solves a problem this project doesn't have
// (launch here means OpenOCD flash+reset against a remote target, not
// spawning a local inferior). LaunchProcess's non-runInTerminal branches
// below are unaffected; the runInTerminal console option just isn't
// supported yet - see project-dap-layer-fork-strategy memory.

void BaseRequestHandler::Run(const Request &request) {
  // Was DebugService::HandleRequest's own bookkeeping, done once per request
  // before dispatching to the owning handler. Now that the Orchestrator
  // dispatches directly to each handler's Run() (see
  // handlers/register_handlers.hpp), this is the one place every request
  // passes through, so the bookkeeping moved here.
  {
    std::lock_guard<std::mutex> guard(dap.m_active_request_mutex);
    dap.m_active_request = &request;
    if (dap.debugger.InterruptRequested()) {
      dap.debugger.CancelInterruptRequest();
    }
  }
  llvm::scope_exit cleanup([&]() {
    std::scoped_lock<std::mutex> active_request_lock(dap.m_active_request_mutex);
    dap.m_active_request = nullptr;
  });

  // If this request was cancelled, send a cancelled response.
  if (dap.IsCancelled(request)) {
    Response cancelled{
        /*request_seq=*/request.seq,
        /*command=*/request.command,
        /*success=*/false,
        /*message=*/eResponseMessageCancelled,
    };
    dap.Send(cancelled);
    return;
  }

  lldb::SBMutex lock = dap.GetAPIMutex();
  std::lock_guard<lldb::SBMutex> guard(lock);

  // FIXME: After all the requests have migrated from LegacyRequestHandler >
  // RequestHandler<> we should be able to move this into
  // RequestHandler<>::operator().
  operator()(request);

  // FIXME: After all the requests have migrated from LegacyRequestHandler >
  // RequestHandler<> we should be able to check `debugger.InterruptRequest` and
  // mark the response as cancelled.
}

llvm::Error BaseRequestHandler::LaunchProcess(
    const protocol::LaunchRequestArguments &arguments) const {
  const std::vector<std::string> &launchCommands = arguments.launchCommands;

  // Instantiate a launch info instance for the target.
  auto launch_info = dap.target.GetLaunchInfo();

  // Grab the current working directory if there is one and set it in the
  // launch info.
  if (!arguments.cwd.empty())
    launch_info.SetWorkingDirectory(arguments.cwd.data());

  // Extract any extra arguments and append them to our program arguments for
  // when we launch
  if (!arguments.args.empty())
    launch_info.SetArguments(MakeArgv(arguments.args).data(), true);

  // Pass any environment variables along that the user specified.
  if (!arguments.env.empty()) {
    lldb::SBEnvironment env;
    for (const auto &kv : arguments.env)
      env.Set(kv.first().data(), kv.second.c_str(), true);
    launch_info.SetEnvironment(env, true);
  }

  if (!arguments.stdio.empty() && !arguments.disableSTDIO)
    SetupIORedirection(arguments.stdio, launch_info);

  launch_info.SetDetachOnError(arguments.detachOnError);
  launch_info.SetShellExpandArguments(arguments.shellExpandArguments);

  auto flags = launch_info.GetLaunchFlags();
  flags =
      SetLaunchFlag(flags, arguments.disableASLR, lldb::eLaunchFlagDisableASLR);
  flags = SetLaunchFlag(flags, arguments.disableSTDIO,
                        lldb::eLaunchFlagDisableSTDIO);
  launch_info.SetLaunchFlags(flags | lldb::eLaunchFlagDebug |
                             lldb::eLaunchFlagStopAtEntry);

  {
    // Perform the launch in synchronous mode so that we don't have to worry
    // about process state changes during the launch.
    ScopeSyncMode scope_sync_mode(dap.debugger);

    if (arguments.console != protocol::eConsoleInternal) {
      return llvm::createStringError(llvm::inconvertibleErrorCode(),
                                     "runInTerminal is not supported yet");
    } else if (launchCommands.empty()) {
      lldb::SBError error;
      dap.target.Launch(launch_info, error);
      if (error.Fail())
        return ToError(error);
    } else {
      // Set the launch info so that run commands can access the configured
      // launch details.
      dap.target.SetLaunchInfo(launch_info);
      if (llvm::Error err = dap.Context().Session().RunLaunchCommands(launchCommands))
        return err;

      // The custom commands might have created a new target so we should use
      // the selected target after these commands are run.
      dap.target = dap.debugger.GetSelectedTarget();
    }
  }

  // Make sure the process is launched and stopped at the entry point before
  // proceeding.
  lldb::SBError error =
      dap.Context().Execution().WaitForProcessToStop(arguments.configuration.timeout);
  if (error.Fail())
    return ToError(error);

  return llvm::Error::success();
}

void BaseRequestHandler::PrintWelcomeMessage() const {
  std::string message;
  llvm::raw_string_ostream OS(message);

#ifdef LLDB_DAP_WELCOME_MESSAGE
  dap.SendOutput(eOutputCategoryConsole, LLDB_DAP_WELCOME_MESSAGE);
#endif

  // Trying to provide a brief but helpful welcome message for users to better
  // understand how the debug console repl works.
  OS << "To get started with the debug console try ";
  switch (dap.Context().Data().repl_mode) {
  case ReplMode::Auto:
    OS << "\"<variable>\", \"<lldb-cmd>\" or \"help [<lldb-cmd>]\"\r\n";
    break;
  case ReplMode::Command:
    OS << "\"<lldb-cmd>\" or \"help [<lldb-cmd>]\".\r\n";
    break;
  case ReplMode::Variable:
    OS << "\"<variable>\" or \"" << dap.Context().Session().configuration.commandEscapePrefix
       << "help [<lldb-cmd>]\".\r\n";
    break;
  }

  OS << "For more information visit " LLDB_DAP_README_URL ".\r\n";

  dap.SendOutput(OutputType::Console, message);
}

void BaseRequestHandler::PrintIntroductionMessage() const {
  std::string msg;
  llvm::raw_string_ostream os(msg);
  if (dap.target && dap.target.GetExecutable()) {
    std::string path = GetSBFileSpecPath(dap.target.GetExecutable());
    os << llvm::formatv("Executable binary set to '{0}' ({1}).\r\n", path,
                        dap.target.GetTriple());
  }
  if (dap.target.GetProcess()) {
    os << llvm::formatv("Attached to process {0}.\r\n",
                        dap.target.GetProcess().GetProcessID());
  }
  dap.SendOutput(OutputType::Console, msg);
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
  // Mark the request as 'cancelled' if the debugger was interrupted while
  // evaluating this handler.
  if (dap.debugger.InterruptRequested()) {
    response.success = false;
    response.message = protocol::eResponseMessageCancelled;
    response.body = std::nullopt;
  }

  dap.Send(response);
}

} // namespace dap
