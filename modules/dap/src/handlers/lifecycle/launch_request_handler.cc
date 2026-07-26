//===-- LaunchRequestHandler.cpp ------------------------------------------===//
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

/// Launch request; value of command field is 'launch'.
Error LaunchRequestHandler::Run(const LaunchRequestArguments &arguments) const {
  Error err = context_.Session().Launch(arguments);
  // Printed regardless of the outcome above, matching the original
  // ordering: the welcome message only depends on the configuration having
  // been applied (early in Launch()), not on the launch succeeding.
  PrintWelcomeMessage();
  return err;
}

} // namespace dap
