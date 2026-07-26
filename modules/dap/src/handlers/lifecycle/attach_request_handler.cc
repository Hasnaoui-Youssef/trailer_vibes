//===-- AttachRequestHandler.cpp ------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "core/components/target_manager.hpp"
#include "dap/protocol/protocol_requests.hpp"
#include "handlers/request_handler.hpp"
#include "llvm/Support/Error.h"

using namespace llvm;
using namespace dap::protocol;

namespace dap {

/// The `attach` request is sent from the client to the debug adapter to attach
/// to a debuggee that is already running.
///
/// Since attaching is debugger/runtime specific, the arguments for this request
/// are not part of this specification.
Error AttachRequestHandler::Run(const AttachRequestArguments &args) const {
  Error err = context_.Session().Attach(args);
  // Printed regardless of the outcome above, matching the original
  // ordering: the welcome message only depends on the configuration having
  // been applied (the first thing Attach() does), not on attach succeeding.
  PrintWelcomeMessage();
  return err;
}

} // namespace dap
