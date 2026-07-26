//===-- NextRequestHandler.cpp --------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "core/components/execution_controller.hpp"
#include "dap/protocol/protocol_types.hpp"
#include "handlers/request_handler.hpp"

namespace dap {

/// The request executes one step (in the given granularity) for the specified
/// thread and allows all other threads to run freely by resuming them. If the
/// debug adapter supports single thread execution (see capability
/// `supportsSingleThreadExecutionRequests`), setting the `singleThread`
/// argument to true prevents other suspended threads from resuming. The debug
/// adapter first sends the response and then a `stopped` event (with reason
/// `step`) after the step has completed.
llvm::Error NextRequestHandler::Run(const protocol::NextArguments &args) const {
  return context_.Execution().Next(args);
}

} // namespace dap
