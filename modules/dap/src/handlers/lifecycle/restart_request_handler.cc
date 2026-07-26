//===-- RestartRequestHandler.cpp -----------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "core/components/target_manager.hpp"
#include "dap/protocol/protocol_requests.hpp"
#include "handlers/request_handler.hpp"

using namespace dap;
using namespace dap::protocol;

/// Restarts a debug session. Clients should only call this request if the
/// corresponding capability `supportsRestartRequest` is true.
/// If the capability is missing or has the value false, a typical client
/// emulates `restart` by terminating the debug adapter first and then launching
/// it anew.
llvm::Error
RestartRequestHandler::Run(const std::optional<RestartArguments> &args) const {
  return context_.Session().Restart(args);
}
