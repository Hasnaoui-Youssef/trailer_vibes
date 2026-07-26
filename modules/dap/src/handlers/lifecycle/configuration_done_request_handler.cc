//===-- ConfigurationDoneRequestHandler..cpp ------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "core/components/execution_controller.hpp"
#include "dap/protocol/protocol_requests.hpp"
#include "handlers/request_handler.hpp"

using namespace llvm;
using namespace dap::protocol;

namespace dap {

/// This request indicates that the client has finished initialization of the
/// debug adapter.
///
/// So it is the last request in the sequence of configuration requests (which
/// was started by the `initialized` event).
///
/// Clients should only call this request if the corresponding capability
/// `supportsConfigurationDoneRequest` is true.
llvm::Error
ConfigurationDoneRequestHandler::Run(const ConfigurationDoneArguments &) const {
  Error err = context_.Execution().ConfigurationDone();
  // Printed regardless of the outcome above, matching the original
  // ordering: the introduction message only depends on the target/process
  // already being set up (done during launch/attach), not on anything
  // ConfigurationDone() itself does.
  PrintIntroductionMessage();
  return err;
}

void ConfigurationDoneRequestHandler::PostRun() const {
  // Runs (and clears) the launch/attach handler's deferred response, if one
  // is pending - see DelayedResponseRequestHandler in
  // handlers/request_handler.hpp. A no-op if none is pending.
  orchestrator_.RunDeferredConfigurationResponse();
}

} // namespace dap
