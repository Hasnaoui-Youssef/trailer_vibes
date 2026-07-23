//===-- DisconnectRequestHandler.cpp --------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "debug_service/debug_service.hpp"
#include "dap/protocol/protocol_requests.hpp"
#include "debug_service/request_handler.hpp"
#include "llvm/Support/Error.h"
#include <optional>

using namespace llvm;
using namespace dap::protocol;

namespace dap::debug_service {

/// Disconnect request; value of command field is 'disconnect'.
Error DisconnectRequestHandler::Run(
    const std::optional<DisconnectArguments> &arguments) const {
  bool terminateDebuggee = !dap.is_attach;

  if (arguments && arguments->terminateDebuggee)
    terminateDebuggee = *arguments->terminateDebuggee;

  if (Error error = dap.Disconnect(terminateDebuggee))
    return error;

  return Error::success();
}
} // namespace dap::debug_service
