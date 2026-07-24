//===-- NextRequestHandler.cpp --------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "core/components/execution_controller.hpp"
#include "debug_service/debug_service.hpp"
#include "debug_service/lldb_utils.hpp"
#include "dap/protocol/protocol_types.hpp"
#include "handlers/request_handler.hpp"
#include "llvm/Support/Error.h"

using namespace llvm;
using namespace lldb;
using namespace dap::protocol;

namespace dap {

/// The request executes one step (in the given granularity) for the specified
/// thread and allows all other threads to run freely by resuming them. If the
/// debug adapter supports single thread execution (see capability
/// `supportsSingleThreadExecutionRequests`), setting the `singleThread`
/// argument to true prevents other suspended threads from resuming. The debug
/// adapter first sends the response and then a `stopped` event (with reason
/// `step`) after the step has completed.
Error NextRequestHandler::Run(const NextArguments &args) const {
  lldb::SBThread thread = dap.Context().GetLLDBThread(args.threadId);
  if (!thread.IsValid())
    return make_error<DAPError>("invalid thread");

  if (!SBDebugger::StateIsStoppedState(dap.target.GetProcess().GetState()))
    return make_error<NotStoppedError>();

  // Remember the thread ID that caused the resume so we can set the
  // "threadCausedFocus" boolean value in the "stopped" events.
  dap.Context().Execution().focus_tid = thread.GetThreadID();
  lldb::SBError error;
  if (args.granularity == eSteppingGranularityInstruction) {
    thread.StepInstruction(/*step_over=*/true, error);
  } else {
    thread.StepOver(args.singleThread ? eOnlyThisThread : eOnlyDuringStepping,
                    error);
  }

  return ToError(error);
}

} // namespace dap
