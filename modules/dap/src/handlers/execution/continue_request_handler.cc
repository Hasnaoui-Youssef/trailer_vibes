//===-- ContinueRequestHandler.cpp ----------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "core/components/execution_controller.hpp"
#include "dap/protocol/protocol_requests.hpp"
#include "handlers/request_handler.hpp"

namespace dap {

/// The request resumes execution of all threads. If the debug adapter supports
/// single thread execution (see capability
/// `supportsSingleThreadExecutionRequests`), setting the `singleThread`
/// argument to true resumes only the specified thread. If not all threads were
/// resumed, the `allThreadsContinued` attribute of the response should be set
/// to false.
llvm::Expected<protocol::ContinueResponseBody>
ContinueRequestHandler::Run(const protocol::ContinueArguments &args) const {
  return context_.Execution().Continue(args);
}

} // namespace dap
