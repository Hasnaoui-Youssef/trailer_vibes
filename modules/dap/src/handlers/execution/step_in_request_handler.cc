//===-- StepInRequestHandler.cpp ------------------------------------------===//
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

// The request resumes the given thread to step into a function/method and
// allows all other threads to run freely by resuming them. If the debug adapter
// supports single thread execution (see capability
// `supportsSingleThreadExecutionRequests`), setting the `singleThread` argument
// to true prevents other suspended threads from resuming. If the request cannot
// step into a target, `stepIn` behaves like the `next` request. The debug
// adapter first sends the response and then a `stopped` event (with reason
// `step`) after the step has completed. If there are multiple function/method
// calls (or other targets) on the source line, the argument `targetId` can be
// used to control into which target the `stepIn` should occur. The list of
// possible targets for a given source line can be retrieved via the
// `stepInTargets` request.
llvm::Error StepInRequestHandler::Run(const protocol::StepInArguments &args) const {
  return context_.Execution().StepIn(args);
}

} // namespace dap
