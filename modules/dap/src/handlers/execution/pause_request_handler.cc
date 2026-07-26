//===-- PauseRequestHandler.cpp -------------------------------------------===//
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

/// The request suspenses the debuggee. The debug adapter first sends the
/// PauseResponse and then a StoppedEvent (event type 'pause') after the thread
/// has been paused successfully.
llvm::Error
PauseRequestHandler::Run(const protocol::PauseArguments &) const {
  return context_.Execution().Pause();
}

} // namespace dap
