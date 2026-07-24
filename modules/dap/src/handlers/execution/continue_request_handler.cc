//===-- ContinueRequestHandler.cpp ----------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "debug_service/debug_service.hpp"
#include "handlers/request_handler.hpp"
#include "debug_service/lldb_utils.hpp"
#include "dap/protocol/protocol_requests.hpp"
#include "lldb/API/SBError.h"
#include "lldb/API/SBProcess.h"
#include "llvm/Support/Error.h"

using namespace llvm;
using namespace lldb;
using namespace dap::protocol;

namespace dap {

/// The request resumes execution of all threads. If the debug adapter supports
/// single thread execution (see capability
/// `supportsSingleThreadExecutionRequests`), setting the `singleThread`
/// argument to true resumes only the specified thread. If not all threads were
/// resumed, the `allThreadsContinued` attribute of the response should be set
/// to false.
Expected<ContinueResponseBody>
ContinueRequestHandler::Run(const ContinueArguments &args) const {
  SBProcess process = dap.target.GetProcess();
  SBError error;

  if (!SBDebugger::StateIsStoppedState(process.GetState()))
    return make_error<NotStoppedError>();

  if (args.singleThread)
    dap.Context().GetLLDBThread(args.threadId).Resume(error);
  else
    error = process.Continue();

  if (error.Fail())
    return ToError(error);

  ContinueResponseBody body;
  body.allThreadsContinued = !args.singleThread;
  return body;
}

} // namespace dap
