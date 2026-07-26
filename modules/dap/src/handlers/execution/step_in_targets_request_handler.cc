//===-- StepInTargetsRequestHandler.cpp -----------------------------------===//
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

// This request retrieves the possible step-in targets for the specified stack
// frame.
// These targets can be used in the `stepIn` request.
// Clients should only call this request if the corresponding capability
// `supportsStepInTargetsRequest` is true.
llvm::Expected<protocol::StepInTargetsResponseBody>
StepInTargetsRequestHandler::Run(const protocol::StepInTargetsArguments &args) const {
  return context_.Execution().GetStepInTargets(args);
}

} // namespace dap
