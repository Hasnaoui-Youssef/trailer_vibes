//===-- DisconnectRequestHandler.cpp --------------------------------------===//
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
#include <optional>

using namespace llvm;
using namespace dap::protocol;

namespace dap {

Error DisconnectRequestHandler::Run(
    const std::optional<DisconnectArguments> &arguments) const {
  bool terminateDebuggee = !context_.Session().is_attach;

  if (arguments && arguments->terminateDebuggee)
    terminateDebuggee = *arguments->terminateDebuggee;

  if (Error error = context_.Session().Disconnect(terminateDebuggee))
    return error;

  return Error::success();
}
} // namespace dap
