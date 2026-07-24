//===-- RestartRequestHandler.cpp -----------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "core/components/execution_controller.hpp"
#include "core/components/target_manager.hpp"
#include "debug_service/debug_service.hpp"
#include "dap/dap_error.hpp"
#include "debug_service/lldb_utils.hpp"
#include "dap/protocol/protocol_requests.hpp"
#include "handlers/request_handler.hpp"

using namespace dap;
using namespace dap::protocol;

/// Restarts a debug session. Clients should only call this request if the
/// corresponding capability `supportsRestartRequest` is true.
/// If the capability is missing or has the value false, a typical client
/// emulates `restart` by terminating the debug adapter first and then launching
/// it anew.
llvm::Error
RestartRequestHandler::Run(const std::optional<RestartArguments> &args) const {
  if (!dap.target.GetProcess().IsValid())
    return llvm::make_error<DAPError>(
        "Restart request received but no process was launched.");

  if (args) {
    if (std::holds_alternative<AttachRequestArguments>(args->arguments))
      return llvm::make_error<DAPError>(
          "Restarting an AttachRequest is not supported.");
    if (const auto *arguments =
            std::get_if<LaunchRequestArguments>(&args->arguments);
        arguments) {
      dap.Context().Session().last_launch_request = *arguments;
      // Update DebugService configuration based on the latest copy of the launch
      // arguments.
      dap.Context().Session().SetConfiguration(arguments->configuration, false);
      dap.Context().Session().ConfigureSourceMaps();
    }
  }

  // Keep track of the old PID so when we get a "process exited" event from the
  // killed process we can detect it and not shut down the whole session.
  lldb::SBProcess process = dap.target.GetProcess();
  dap.Context().SetRestartingProcessId(process.GetProcessID());

  // Stop the current process if necessary. The logic here is similar to
  // CommandObjectProcessLaunchOrAttach::StopProcessIfNecessary, except that
  // we don't ask the user for confirmation.
  if (process.IsValid()) {
    ScopeSyncMode scope_sync_mode(dap.debugger);
    lldb::StateType state = process.GetState();
    if (state != lldb::eStateConnected) {
      if (lldb::SBError error = process.Kill(); error.Fail())
        return ToError(error);
    }
    // Clear the list of thread ids to avoid sending "thread exited" events
    // for threads of the process we are terminating.
    dap.Context().Execution().thread_ids.clear();
  }

  // FIXME: Should we run 'preRunCommands'?
  // FIXME: Should we add a 'preRestartCommands'?
  if (llvm::Error error = LaunchProcess(*dap.Context().Session().last_launch_request))
    return error;

  dap.Context().Execution().SendProcessEvent(core::Launch);

  // This is normally done after receiving a "configuration done" request.
  // Because we're restarting, configuration has already happened so we can
  // continue the process right away.
  if (dap.Context().Execution().stop_at_entry)
    return dap.Context().Execution().SendThreadStoppedEvent(/*on_entry=*/true);

  return ToError(dap.target.GetProcess().Continue());
}
