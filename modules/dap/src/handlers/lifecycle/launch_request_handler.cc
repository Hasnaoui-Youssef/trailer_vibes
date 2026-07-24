//===-- LaunchRequestHandler.cpp ------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "core/components/target_manager.hpp"
#include "debug_service/debug_service.hpp"
#include "debug_service/lldb_utils.hpp"
#include "dap/protocol/protocol_requests.hpp"
#include "handlers/request_handler.hpp"
#include "llvm/Support/Error.h"
#include "llvm/Support/FileSystem.h"

using namespace llvm;
using namespace dap::protocol;

namespace dap {

/// Launch request; value of command field is 'launch'.
Error LaunchRequestHandler::Run(const LaunchRequestArguments &arguments) const {
  // Initialize DebugService debugger.
  if (Error err = dap.Context().Session().InitializeDebugger())
    return err;

  dap.Context().Session().SetConfiguration(arguments.configuration, /*is_attach=*/false);
  dap.Context().Session().last_launch_request = arguments;

  PrintWelcomeMessage();

  // This is a hack for loading DWARF in .o files on Mac where the .o files
  // in the debug map of the main executable have relative paths which
  // require the lldb-dap binary to have its working directory set to that
  // relative root for the .o files in order to be able to load debug info.
  if (!dap.Context().Session().configuration.debuggerRoot.empty())
    sys::fs::set_current_path(dap.Context().Session().configuration.debuggerRoot);

  // Run any initialize LLDB commands the user specified in the launch.json.
  // This is run before target is created, so commands can't do anything with
  // the targets - preRunCommands are run with the target.
  if (Error err = dap.Context().Session().RunInitCommands())
    return err;

  dap.Context().Session().ConfigureSourceMaps();

  lldb::SBError error;
  lldb::SBTarget target = dap.Context().Session().CreateTarget(error);
  if (error.Fail())
    return ToError(error);

  dap.Context().Session().SetTarget(target);

  // Run any pre run LLDB commands the user specified in the launch.json
  if (Error err = dap.Context().Session().RunPreRunCommands())
    return err;

  if (Error err = LaunchProcess(arguments))
    return err;

  dap.Context().Session().RunPostRunCommands();

  return Error::success();
}

} // namespace dap
